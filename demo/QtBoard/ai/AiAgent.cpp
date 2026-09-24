#include "AiAgent.h"

#include <QJsonDocument>
#include <QRectF>
#include <QStringList>

#include "AiTools.h"
#include "BoardView.h"
#include "QtBoardData.h"
#include "SketchBuilder.h"

namespace ai {

namespace {

QJsonObject ToolError(const QString& message) {
    QJsonObject o;
    o[QStringLiteral("ok")] = false;
    o[QStringLiteral("error")] = message;
    return o;
}

QString RoleOf(const QJsonValue& message) {
    return message.toObject().value(QStringLiteral("role")).toString();
}

}  // namespace

AiAgent::AiAgent(BoardView& view, QtBoardData& data, QObject* parent)
    : QObject(parent),
      view_(view),
      data_(data),
      executor_(new AiExecutor(view, data)),
      client_(new AiClient()) {}

AiAgent::~AiAgent() = default;

void AiAgent::Send(const QString& userText) {
    if (busy_) {
        emit errorOccurred(QStringLiteral("上一条请求尚未完成，请稍候"));
        return;
    }
    const QString text = userText.trimmed();
    if (text.isEmpty())
        return;
    StartRound(text);
}

void AiAgent::BeautifySelection() {
    if (busy_) {
        emit errorOccurred(QStringLiteral("上一条请求尚未完成，请稍候"));
        return;
    }
    const QStringList ids = view_.selectedElementIds();
    if (ids.isEmpty()) {
        emit errorOccurred(QStringLiteral("请先在白板上选中要美化的笔迹"));
        return;
    }

    // 选中数据直接嵌入用户消息（省一次 get_board_state 往返）
    QJsonArray strokes;
    for (const QString& id : ids) {
        const auto snapshot = data_.GetElementSnapshot(id.toStdString());
        if (!snapshot || snapshot->GetType() != "Stroke")
            continue;
        const auto& stroke = static_cast<const whiteboard::Stroke&>(*snapshot);
        QJsonObject item;
        item[QStringLiteral("id")] = id;
        const whiteboard::Rect bbox = stroke.bounding.ToRect();
        item[QStringLiteral("bbox")] = QJsonArray{ bbox.x, bbox.y, bbox.width, bbox.height };
        const std::vector<whiteboard::Point>& src =
            stroke.rawPoints.empty() ? stroke.points : stroke.rawPoints;
        QJsonArray points;
        for (const whiteboard::Point& p : SimplifyPoints(src, 32))
            points.append(QJsonArray{ p.x, p.y });
        item[QStringLiteral("points")] = points;
        strokes.append(item);
    }
    if (strokes.isEmpty()) {
        emit errorOccurred(QStringLiteral("选中项中没有可美化的笔迹（仅笔迹支持美化）"));
        return;
    }

    const QRectF vp = view_.visibleBoardRect();
    const QString viewportText =
        QStringLiteral("x=%1, y=%2, 宽=%3, 高=%4，中心=(%5,%6)")
            .arg(qRound(vp.x()))
            .arg(qRound(vp.y()))
            .arg(qRound(vp.width()))
            .arg(qRound(vp.height()))
            .arg(qRound(vp.center().x()))
            .arg(qRound(vp.center().y()));

    QJsonObject payload;
    payload[QStringLiteral("strokes")] = strokes;
    const QString content =
        QStringLiteral("用户请求：美化我选中的笔迹。请识别每（组）笔迹最接近的标准图形，"
                       "调用 replace_with_shape 把它替换为规则图形（矩形/圆/椭圆/三角/"
                       "五边形/五角星优先；都不接近就用 polygon 或 line 子路径按原轨迹拟合）。"
                       "可分辨出多个图形时分别多次调用 replace_with_shape。\n"
                       "当前视口：%1\n选中笔迹数据（id、包围盒 [x,y,宽,高]、抽稀点 [x,y]）：%2")
            .arg(viewportText,
                 QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    StartRound(content);
}

void AiAgent::Cancel() {
    if (!busy_)
        return;
    cancelled_ = true;
    if (client_->IsBusy()) {
        client_->Cancel();  // 回调 OnReply（cancelled_ 收尾）
    } else {
        FinishRound(QString());
    }
}

void AiAgent::ClearHistory() {
    if (busy_)
        return;
    history_ = QJsonArray();
    pendingReplyText_.clear();
}

void AiAgent::StartRound(const QString& userContent) {
    QJsonObject userMessage;
    userMessage[QStringLiteral("role")] = QStringLiteral("user");
    userMessage[QStringLiteral("content")] = userContent;
    history_.append(userMessage);
    TrimHistory();

    busy_ = true;
    cancelled_ = false;
    iterations_ = 0;
    pendingReplyText_.clear();
    calledKeys_.clear();
    emit busyChanged(true);
    RequestNext();
}

QJsonArray AiAgent::BuildMessages() const {
    QJsonArray messages;
    QJsonObject system;
    system[QStringLiteral("role")] = QStringLiteral("system");
    system[QStringLiteral("content")] = SystemPrompt();
    messages.append(system);
    for (const QJsonValue& message : history_)
        messages.append(message);
    return messages;
}

QString AiAgent::SystemPrompt() const {
    const QRectF vp = view_.visibleBoardRect();
    const QString viewportText =
        QStringLiteral("x=%1, y=%2, 宽=%3, 高=%4，中心=(%5,%6)")
            .arg(qRound(vp.x()))
            .arg(qRound(vp.y()))
            .arg(qRound(vp.width()))
            .arg(qRound(vp.height()))
            .arg(qRound(vp.center().x()))
            .arg(qRound(vp.center().y()));
    const QString centerText = QStringLiteral("[%1,%2]")
                                   .arg(qRound(vp.center().x()))
                                   .arg(qRound(vp.center().y()));

    return QStringLiteral(
               "你是白板 AI 助手，运行在一个可自由书写绘画的白板应用里（深色黑板背景）。"
               "用中文简短回复你做了什么（一两句话），不要输出多余解释。\n"
               "\n"
               "【坐标系】画布为无边界平面，左上角为原点，单位为像素，坐标可为负。"
               "当前可视区域：%1。用户说“这里/中间/屏幕上”指可视区域中心；"
               "说“大一点/小一点”时相对建议尺寸调整。\n"
               "\n"
               "【工具纪律】\n"
               "1. 涉及位置、识别已有内容、删除或修改前，先调用 get_board_state 查看元素 id 与包围盒；\n"
               "2. 画图/写字/加小工具直接调用对应工具；一次 draw_sketch 最多 60 条笔迹，不够就分多次调用；\n"
               "3. 颜色用 #RRGGBB 格式；不确定颜色时用 #FFFFFF（白色，黑板背景可见）；\n"
               "4. 各工具的完整参数说明以 JSON Schema 为准。\n"
               "\n"
               "【draw_sketch 归一化坐标】frame 给出绘制区域（center 中心 + size 尺寸，像素），"
               "每条 shape 用 (0,0)=区域左上、(1,1)=区域右下 的归一化坐标描述：\n"
               " - polyline：折线，points 至少 2 个点；closed=true 首尾闭合（用于三角形、耳朵等）；\n"
               " - quad / cubic：二次/三次贝塞尔，points 恰为 3 / 4 个控制点（画光滑曲线，如尾巴、云朵）；\n"
               " - ellipse：椭圆，center + rx（相对区域宽）/ ry（相对区域高）。\n"
               "示例（在视口中心画一只黄色小狗，frame size 480x420）：\n"
               "{\"frame\":{\"center\":%2,\"size\":[480,420]},\"shapes\":["
               "{\"prim\":\"ellipse\",\"center\":[0.38,0.58],\"rx\":0.26,\"ry\":0.20,\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"ellipse\",\"center\":[0.70,0.34],\"rx\":0.13,\"ry\":0.15,\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"polyline\",\"points\":[[0.64,0.20],[0.60,0.08],[0.72,0.14]],\"closed\":true,\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"ellipse\",\"center\":[0.74,0.31],\"rx\":0.02,\"ry\":0.02,\"color\":\"#222222\",\"width\":3},"
               "{\"prim\":\"polyline\",\"points\":[[0.50,0.72],[0.50,0.92]],\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"polyline\",\"points\":[[0.60,0.72],[0.60,0.92]],\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"polyline\",\"points\":[[0.22,0.72],[0.22,0.92]],\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"polyline\",\"points\":[[0.32,0.72],[0.32,0.92]],\"color\":\"#E6B422\",\"width\":4},"
               "{\"prim\":\"cubic\",\"points\":[[0.12,0.52],[0.00,0.42],[0.00,0.26],[0.10,0.22]],\"color\":\"#E6B422\",\"width\":4}"
               "]}\n"
               "三角形示例：{\"prim\":\"polyline\",\"points\":[[0.5,0.05],[0.05,0.95],[0.95,0.95]],\"closed\":true}；"
               "圆示例：{\"prim\":\"ellipse\",\"center\":[0.5,0.5],\"rx\":0.45,\"ry\":0.45}。\n"
               "\n"
               "【美化笔迹】用户说“美化/把我画的变成标准图形/我画了个三角形帮我生成三角形”时："
               "用 get_board_state（或消息中已附带的选中笔迹数据）识别相关 stroke_ids，调用 "
               "replace_with_shape 替换为规则图形：像矩形/圆/椭圆/三角/五边形/五角星就用对应 "
               "kind + box；都不接近就用 polygon/line + subpaths 按原轨迹拟合。\n"
               "\n"
               "【回复】只描述你实际调用工具完成的操作；工具报错时如实告知用户原因。")
        .arg(viewportText, centerText);
}

void AiAgent::RequestNext() {
    if (!busy_)
        return;
    if (iterations_ >= kMaxIterations) {
        FinishRound(pendingReplyText_.isEmpty()
                        ? QStringLiteral("（操作步骤过多，已停止；已完成的部分仍保留在画布上）")
                        : pendingReplyText_);
        return;
    }
    ++iterations_;
    emit statusChanged(QStringLiteral("正在思考…（第 %1 步）").arg(iterations_));
    const AiConfig config = AiConfig::Load();
    client_->SendChat(BuildMessages(), executor_->ToolSchemas(), true, config,
                      [this](bool ok, const AiReply& reply, const QString& error) {
                          OnReply(ok, reply, error);
                      });
}

void AiAgent::OnReply(bool ok, const AiReply& reply, const QString& error) {
    if (!busy_)
        return;
    if (cancelled_) {
        emit statusChanged(QStringLiteral("已取消"));
        FinishRound(QString());
        return;
    }
    if (!ok) {
        FinishWithError(error);
        return;
    }
    if (reply.toolCalls.isEmpty()) {
        FinishRound(reply.content);
        return;
    }
    if (!reply.content.isEmpty())
        pendingReplyText_ = reply.content;

    // assistant 消息（含 tool_calls）回填历史
    QJsonObject assistant;
    assistant[QStringLiteral("role")] = QStringLiteral("assistant");
    assistant[QStringLiteral("content")] = reply.content;
    assistant[QStringLiteral("tool_calls")] = reply.toolCalls;
    history_.append(assistant);

    if (!ExecuteToolCalls(reply.toolCalls)) {
        FinishRound(pendingReplyText_.isEmpty()
                        ? QStringLiteral("（已停止；已完成的部分仍保留在画布上）")
                        : pendingReplyText_);
        return;
    }
    RequestNext();
}

// 逐个执行工具并回填结果；返回 false 表示需中止循环（重复调用）
bool AiAgent::ExecuteToolCalls(const QJsonArray& toolCalls) {
    for (const QJsonValue& value : toolCalls) {
        const QJsonObject call = value.toObject();
        const QString callId = call.value(QStringLiteral("id")).toString();
        const QJsonObject function = call.value(QStringLiteral("function")).toObject();
        const QString name = function.value(QStringLiteral("name")).toString();
        const QString argsRaw = function.value(QStringLiteral("arguments")).toString();

        QJsonObject args;
        const QJsonDocument doc = QJsonDocument::fromJson(argsRaw.toUtf8());
        if (doc.isObject())
            args = doc.object();

        const QString key = name + QLatin1Char('\n') + argsRaw;
        if (calledKeys_.contains(key)) {  // 同名同参数重复：拒绝执行并中止循环
            QJsonObject toolMessage;
            toolMessage[QStringLiteral("role")] = QStringLiteral("tool");
            toolMessage[QStringLiteral("tool_call_id")] = callId;
            toolMessage[QStringLiteral("content")] =
                QString::fromUtf8(QJsonDocument(
                                      ToolError(QStringLiteral("重复调用（同名同参数），已拒绝执行并停止")))
                                      .toJson(QJsonDocument::Compact));
            history_.append(toolMessage);
            emit actionDone(QStringLiteral("检测到重复工具调用，已停止"));
            return false;
        }
        calledKeys_.insert(key);

        const QJsonObject result =
            name.isEmpty() ? ToolError(QStringLiteral("工具调用缺少函数名"))
                           : executor_->Execute(name, args);

        QJsonObject toolMessage;
        toolMessage[QStringLiteral("role")] = QStringLiteral("tool");
        toolMessage[QStringLiteral("tool_call_id")] = callId;
        toolMessage[QStringLiteral("content")] =
            QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
        history_.append(toolMessage);

        emit actionDone(ToolSummary(name, result));
    }
    return true;
}

QString AiAgent::ToolSummary(const QString& name, const QJsonObject& result) const {
    if (!result.value(QStringLiteral("ok")).toBool(false)) {
        const QString error = result.value(QStringLiteral("error")).toString();
        return QStringLiteral("工具 %1 失败：%2")
            .arg(name, error.isEmpty() ? QStringLiteral("未知错误") : error);
    }
    if (name == QStringLiteral("get_board_state"))
        return QStringLiteral("已读取白板状态（%1 个元素）")
            .arg(result.value(QStringLiteral("element_count")).toInt());
    if (name == QStringLiteral("draw_sketch"))
        return QStringLiteral("已绘制 %1 条笔迹")
            .arg(result.value(QStringLiteral("stroke_count")).toInt());
    if (name == QStringLiteral("erase_region"))
        return QStringLiteral("已擦除指定区域");
    if (name == QStringLiteral("delete_elements"))
        return QStringLiteral("已删除 %1 个元素").arg(result.value(QStringLiteral("count")).toInt());
    if (name == QStringLiteral("add_widget"))
        return QStringLiteral("已添加小工具");
    if (name == QStringLiteral("add_text"))
        return QStringLiteral("已添加文字");
    if (name == QStringLiteral("replace_with_shape"))
        return QStringLiteral("已把 %1 条笔迹替换为标准图形")
            .arg(result.value(QStringLiteral("replaced_count")).toInt());
    return QStringLiteral("已执行 %1").arg(name);
}

void AiAgent::FinishRound(const QString& text) {
    busy_ = false;
    iterations_ = 0;
    cancelled_ = false;
    emit busyChanged(false);
    if (!text.isEmpty())
        emit replyReady(text);
}

void AiAgent::FinishWithError(const QString& error) {
    busy_ = false;
    iterations_ = 0;
    cancelled_ = false;
    emit busyChanged(false);
    emit errorOccurred(error.isEmpty() ? QStringLiteral("请求失败") : error);
}

// 历史裁剪：保留最近 kMaxHistoryTurns 轮（每轮从 user 消息开始，整轮保留/丢弃，
// 保证 assistant(tool_calls)/tool 消息配对完整）
void AiAgent::TrimHistory() {
    int userCount = 0;
    for (const QJsonValue& message : history_) {
        if (RoleOf(message) == QLatin1String("user"))
            ++userCount;
    }
    if (userCount <= kMaxHistoryTurns)
        return;

    const int keepFromUser = userCount - kMaxHistoryTurns + 1;  // 保留起始 user 序号（1-based）
    int seen = 0;
    int cutIndex = 0;
    for (int i = 0; i < history_.size(); ++i) {
        if (RoleOf(history_.at(i)) == QLatin1String("user")) {
            ++seen;
            if (seen == keepFromUser) {
                cutIndex = i;
                break;
            }
        }
    }
    for (int i = 0; i < cutIndex; ++i)
        history_.removeFirst();
}

}  // namespace ai
