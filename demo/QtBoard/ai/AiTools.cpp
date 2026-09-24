#include "AiTools.h"

#include <QRectF>
#include <QtMath>

#include <memory>
#include <vector>

#include "BoardView.h"
#include "QtBoardData.h"
#include "SketchBuilder.h"

namespace ai {

namespace {

constexpr double kMaxCoord = 100000.0;      // 场景坐标上限
constexpr double kMaxExtent = 100000.0;     // 宽/高上限
constexpr int kMaxStateElements = 60;       // get_board_state 元素封顶
constexpr int kStateStrokePoints = 24;      // 笔迹抽稀点数上限
constexpr double kWidgetCardW = 280.0;      // 小工具卡片基础宽（1 倍）
constexpr double kWidgetCardH = 156.0;      // 小工具卡片基础高（1 倍）

QJsonObject Error(const QString& message) {
    QJsonObject o;
    o[QStringLiteral("ok")] = false;
    o[QStringLiteral("error")] = message;
    return o;
}

QJsonObject Success() {
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    return o;
}

bool NumberArg(const QJsonObject& args, const QString& key, double* out) {
    const QJsonValue value = args.value(key);
    if (!value.isDouble())
        return false;
    *out = value.toDouble();
    return true;
}

double ClampCoord(double v) {
    return qBound(-kMaxCoord, v, kMaxCoord);
}

// 工具名（英文）→ 小工具类型 0~6；未知返回 -1
int WidgetKindFromName(const QString& name) {
    if (name == QStringLiteral("stopwatch")) return 0;
    if (name == QStringLiteral("timer")) return 1;
    if (name == QStringLiteral("calculator")) return 2;
    if (name == QStringLiteral("abacus")) return 3;
    if (name == QStringLiteral("dice")) return 4;
    if (name == QStringLiteral("wheel")) return 5;
    if (name == QStringLiteral("picker")) return 6;
    return -1;
}

// 参数化图形名 → GraphicKind 整数值（rect=0 … star=5）；未知返回 -1
int ParametricKindFromName(const QString& name) {
    if (name == QStringLiteral("rect")) return 0;
    if (name == QStringLiteral("circle")) return 1;
    if (name == QStringLiteral("ellipse")) return 2;
    if (name == QStringLiteral("triangle")) return 3;
    if (name == QStringLiteral("pentagon")) return 4;
    if (name == QStringLiteral("star")) return 5;
    return -1;
}

// polygon/line 的 subpaths 解析（points 为 board 绝对坐标 [[x,y],...]）
bool ParseSubpaths(const QJsonArray& arr, bool isLine, std::vector<whiteboard::Subpath>* out,
                   QString* error) {
    if (arr.isEmpty()) {
        *error = QStringLiteral("shape.subpaths 为空");
        return false;
    }
    const int minPoints = isLine ? 2 : 3;
    for (int i = 0; i < arr.size(); ++i) {
        const QJsonObject sp = arr.at(i).toObject();
        const QJsonArray pts = sp.value(QStringLiteral("points")).toArray();
        if (pts.size() < minPoints) {
            *error = QStringLiteral("shape.subpaths[%1] 至少需要 %2 个点").arg(i).arg(minPoints);
            return false;
        }
        if (pts.size() > 200) {
            *error = QStringLiteral("shape.subpaths[%1] 点数超过上限 200").arg(i);
            return false;
        }
        whiteboard::Subpath path;
        // line 缺省开放；polygon 缺省闭合
        path.closed = sp.value(QStringLiteral("closed")).toBool(!isLine);
        for (const QJsonValue& pv : pts) {
            const QJsonArray pair = pv.toArray();
            if (pair.size() < 2 || !pair.at(0).isDouble() || !pair.at(1).isDouble()) {
                *error = QStringLiteral("shape.subpaths[%1].points 含非法点（需 [x,y] 数值对）").arg(i);
                return false;
            }
            path.points.push_back(whiteboard::Point(qRound(ClampCoord(pair.at(0).toDouble())),
                                                    qRound(ClampCoord(pair.at(1).toDouble()))));
        }
        out->push_back(std::move(path));
    }
    return true;
}

// 元素摘要：{id, type, bbox:[x,y,w,h], 及类型特有字段}
QJsonObject ElementSummary(const whiteboard::Element& e, bool includePoints) {
    const std::string type = e.GetType();
    QJsonObject item;
    item[QStringLiteral("id")] = QString::fromStdString(e.id);
    item[QStringLiteral("type")] = QString::fromStdString(type);

    whiteboard::Rect bbox;
    if (type == "Stroke") {
        const auto& s = static_cast<const whiteboard::Stroke&>(e);
        bbox = s.bounding.ToRect();
        item[QStringLiteral("color")] = HexFromColor(s.color);
        item[QStringLiteral("width")] = s.width;
        if (includePoints) {
            const std::vector<whiteboard::Point>& src = s.rawPoints.empty() ? s.points : s.rawPoints;
            const std::vector<whiteboard::Point> pts = SimplifyPoints(src, kStateStrokePoints);
            QJsonArray arr;
            for (const whiteboard::Point& p : pts)
                arr.append(QJsonArray{ p.x, p.y });
            item[QStringLiteral("points")] = arr;
        }
    } else if (type == "Graphic") {
        const auto& g = static_cast<const whiteboard::GraphicElement&>(e);
        bbox = g.bounding.ToRect();
        item[QStringLiteral("color")] = HexFromColor(g.color);
        item[QStringLiteral("width")] = g.width;
        item[QStringLiteral("shape_kind")] = g.kind;  // GraphicKind 整数值
    } else if (type == "Text") {
        const auto& t = static_cast<const whiteboard::TextElement&>(e);
        bbox = t.bounds;
        item[QStringLiteral("color")] = HexFromColor(t.color);
        item[QStringLiteral("font_size")] = t.fontSize;
        QString text = QString::fromUtf8(t.text.c_str());
        if (text.size() > 80)
            text = text.left(80) + QStringLiteral("…");
        item[QStringLiteral("text")] = text;
    } else if (type == "Widget") {
        const auto& w = static_cast<const whiteboard::WidgetElement&>(e);
        bbox = whiteboard::Rect(w.x, w.y, qRound(kWidgetCardW * w.scale),
                                qRound(kWidgetCardH * w.scale));
        item[QStringLiteral("widget_kind")] = w.kind;  // 0 秒表 / 1 计时器 / …
    } else if (type == "Table") {
        const auto& t = static_cast<const whiteboard::TableElement&>(e);
        const whiteboard::TableElement::Layout layout = t.ComputeLayout();
        bbox = whiteboard::Rect(t.origin.x, t.origin.y, qMax(1, qRound(layout.totalW)),
                                qMax(1, qRound(layout.totalH)));
    } else if (type == "MindMap") {
        const auto& m = static_cast<const whiteboard::MindMapElement&>(e);
        const whiteboard::MindLayout layout = whiteboard::ComputeMindMapLayout(m);
        if (layout.boxes.empty()) {
            bbox = whiteboard::Rect(m.root.x, m.root.y, 1, 1);
        } else {
            int minX = 0, minY = 0, maxX = 0, maxY = 0;
            bool first = true;
            for (const auto& box : layout.boxes) {
                if (first) {
                    minX = box.frame.x;
                    minY = box.frame.y;
                    maxX = box.frame.x + box.frame.width;
                    maxY = box.frame.y + box.frame.height;
                    first = false;
                } else {
                    minX = qMin(minX, box.frame.x);
                    minY = qMin(minY, box.frame.y);
                    maxX = qMax(maxX, box.frame.x + box.frame.width);
                    maxY = qMax(maxY, box.frame.y + box.frame.height);
                }
            }
            bbox = whiteboard::Rect(minX, minY, qMax(1, maxX - minX), qMax(1, maxY - minY));
        }
    } else {
        bbox = whiteboard::Rect(0, 0, 1, 1);  // 未知类型：占位
    }

    QJsonArray rectArr;
    rectArr.append(bbox.x);
    rectArr.append(bbox.y);
    rectArr.append(bbox.width);
    rectArr.append(bbox.height);
    item[QStringLiteral("bbox")] = rectArr;
    return item;
}

// ---------- Schema 构建小工具 ----------

QJsonObject Prop(const QString& type, const QString& description) {
    QJsonObject p;
    p[QStringLiteral("type")] = type;
    p[QStringLiteral("description")] = description;
    return p;
}

// [数字, 数字] 定长数组（坐标对）
QJsonObject PairProp(const QString& description) {
    QJsonObject point;
    point[QStringLiteral("type")] = QStringLiteral("array");
    point[QStringLiteral("description")] = description;
    QJsonObject num;
    num[QStringLiteral("type")] = QStringLiteral("number");
    point[QStringLiteral("items")] = num;
    point[QStringLiteral("minItems")] = 2;
    point[QStringLiteral("maxItems")] = 2;
    return point;
}

QJsonObject EnumProp(const QString& description, const QJsonArray& values) {
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("string");
    p[QStringLiteral("description")] = description;
    p[QStringLiteral("enum")] = values;
    return p;
}

QJsonObject ToolDef(const QString& name, const QString& description,
                    const QJsonObject& properties, const QJsonArray& required) {
    QJsonObject parameters;
    parameters[QStringLiteral("type")] = QStringLiteral("object");
    parameters[QStringLiteral("properties")] = properties;
    parameters[QStringLiteral("required")] = required;

    QJsonObject function;
    function[QStringLiteral("name")] = name;
    function[QStringLiteral("description")] = description;
    function[QStringLiteral("parameters")] = parameters;

    QJsonObject toolObj;
    toolObj[QStringLiteral("type")] = QStringLiteral("function");
    toolObj[QStringLiteral("function")] = function;
    return toolObj;
}

}  // namespace

AiExecutor::AiExecutor(BoardView& view, QtBoardData& data) : view_(view), data_(data) {}

QJsonArray AiExecutor::ToolSchemas() {
    QJsonArray tools;

    {
        QJsonObject props;
        props[QStringLiteral("include_points")] =
            Prop(QStringLiteral("boolean"),
                 QStringLiteral("是否附上每条笔迹的抽稀点集（默认 true；仅需概览时可设 false）"));
        tools.append(ToolDef(
            QStringLiteral("get_board_state"),
            QStringLiteral("读取当前白板状态：当前页 id、可视区域（board 坐标）与全部元素清单"
                           "（id/类型/包围盒/颜色/线宽；笔迹可含抽稀点集）。回答位置相关"
                           "问题、识别已有内容或做任何修改前，先调用本工具。"),
            props, QJsonArray()));
    }

    {
        QJsonObject frameProps;
        frameProps[QStringLiteral("center")] =
            PairProp(QStringLiteral("绘制区域中心 [x, y]（board 像素坐标，可为负）"));
        frameProps[QStringLiteral("size")] =
            PairProp(QStringLiteral("绘制区域尺寸 [宽, 高]（像素，建议 160~800）"));
        QJsonObject frame;
        frame[QStringLiteral("type")] = QStringLiteral("object");
        frame[QStringLiteral("description")] = QStringLiteral("绘制区域（board 坐标）");
        frame[QStringLiteral("properties")] = frameProps;
        frame[QStringLiteral("required")] = QJsonArray{ QStringLiteral("center"), QStringLiteral("size") };

        QJsonObject shapeProps;
        shapeProps[QStringLiteral("prim")] =
            EnumProp(QStringLiteral("图元：polyline 折线（points ≥2）；quad 二次贝塞尔"
                                    "（points 恰 3 个控制点，采样 16 段）；cubic 三次贝塞尔"
                                    "（points 恰 4 个控制点，采样 20 段）；ellipse 椭圆"
                                    "（用 center/rx/ry，48 段闭合）"),
                     QJsonArray{ QStringLiteral("polyline"), QStringLiteral("quad"),
                                 QStringLiteral("cubic"), QStringLiteral("ellipse") });
        {
            QJsonObject points;
            points[QStringLiteral("type")] = QStringLiteral("array");
            points[QStringLiteral("description")] =
                QStringLiteral("控制点列表（归一化 0~1，相对 frame，(0,0)=左上、(1,1)=右下）");
            points[QStringLiteral("items")] = PairProp(QStringLiteral("归一化点 [u, v]"));
            shapeProps[QStringLiteral("points")] = points;
        }
        shapeProps[QStringLiteral("center")] =
            PairProp(QStringLiteral("ellipse 中心（归一化 0~1）"));
        shapeProps[QStringLiteral("rx")] =
            Prop(QStringLiteral("number"),
                 QStringLiteral("ellipse 水平半径（归一化，相对 frame 宽；如 0.12）"));
        shapeProps[QStringLiteral("ry")] =
            Prop(QStringLiteral("number"),
                 QStringLiteral("ellipse 垂直半径（归一化，相对 frame 高）"));
        shapeProps[QStringLiteral("closed")] =
            Prop(QStringLiteral("boolean"), QStringLiteral("polyline 是否首尾闭合（默认 false）"));
        shapeProps[QStringLiteral("color")] =
            Prop(QStringLiteral("string"), QStringLiteral("颜色 #RRGGBB，缺省用当前笔色"));
        shapeProps[QStringLiteral("width")] =
            Prop(QStringLiteral("integer"), QStringLiteral("线宽像素，缺省用当前笔宽"));

        QJsonObject shapeItem;
        shapeItem[QStringLiteral("type")] = QStringLiteral("object");
        shapeItem[QStringLiteral("properties")] = shapeProps;
        shapeItem[QStringLiteral("required")] = QJsonArray{ QStringLiteral("prim") };

        QJsonObject shapes;
        shapes[QStringLiteral("type")] = QStringLiteral("array");
        shapes[QStringLiteral("description")] = QStringLiteral("形状列表（上限 60 条），每条生成一笔");
        shapes[QStringLiteral("items")] = shapeItem;
        shapes[QStringLiteral("maxItems")] = 60;

        QJsonObject props;
        props[QStringLiteral("frame")] = frame;
        props[QStringLiteral("shapes")] = shapes;
        tools.append(ToolDef(
            QStringLiteral("draw_sketch"),
            QStringLiteral("用笔迹绘制一组线条（简笔画/图案）。frame 给出绘制区域（board 像素"
                           "坐标），shapes 为归一化坐标的形状列表：(0,0)=区域左上、(1,1)=右下。"
                           "一次调用最多 60 条笔迹；整组绘制为一步撤销。"),
            props, QJsonArray{ QStringLiteral("frame"), QStringLiteral("shapes") }));
    }

    {
        QJsonObject props;
        props[QStringLiteral("x")] = Prop(QStringLiteral("number"), QStringLiteral("矩形左上角 x（board 像素）"));
        props[QStringLiteral("y")] = Prop(QStringLiteral("number"), QStringLiteral("矩形左上角 y（board 像素）"));
        props[QStringLiteral("width")] = Prop(QStringLiteral("number"), QStringLiteral("矩形宽（≥1）"));
        props[QStringLiteral("height")] = Prop(QStringLiteral("number"), QStringLiteral("矩形高（≥1）"));
        tools.append(ToolDef(
            QStringLiteral("erase_region"),
            QStringLiteral("擦除指定矩形区域内的笔迹（与橡皮擦同语义：只对笔迹生效，部分覆盖"
                           "会裁剪笔画、完全覆盖整条删除）。区域外的内容不受影响。"),
            props,
            QJsonArray{ QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                        QStringLiteral("height") }));
    }

    {
        QJsonObject props;
        {
            QJsonObject ids;
            ids[QStringLiteral("type")] = QStringLiteral("array");
            ids[QStringLiteral("description")] = QStringLiteral("要删除的元素 id 列表（get_board_state 获取）");
            QJsonObject str;
            str[QStringLiteral("type")] = QStringLiteral("string");
            ids[QStringLiteral("items")] = str;
            props[QStringLiteral("element_ids")] = ids;
        }
        tools.append(ToolDef(
            QStringLiteral("delete_elements"),
            QStringLiteral("按 id 删除元素（笔画/图形/文字/小工具等任意类型）。整批删除为一步撤销。"),
            props, QJsonArray{ QStringLiteral("element_ids") }));
    }

    {
        QJsonObject props;
        props[QStringLiteral("kind")] =
            EnumProp(QStringLiteral("小工具类型：秒表/计时器/计算器/算盘/骰子/大转盘/点名器"),
                     QJsonArray{ QStringLiteral("stopwatch"), QStringLiteral("timer"),
                                 QStringLiteral("calculator"), QStringLiteral("abacus"),
                                 QStringLiteral("dice"), QStringLiteral("wheel"),
                                 QStringLiteral("picker") });
        props[QStringLiteral("x")] = Prop(QStringLiteral("number"), QStringLiteral("卡片中心 x（board 像素）"));
        props[QStringLiteral("y")] = Prop(QStringLiteral("number"), QStringLiteral("卡片中心 y（board 像素）"));
        props[QStringLiteral("duration_sec")] =
            Prop(QStringLiteral("integer"), QStringLiteral("计时器时长（秒，仅 timer，默认 300）"));
        {
            QJsonObject sides = Prop(QStringLiteral("integer"), QStringLiteral("骰子面数（仅 dice）"));
            sides[QStringLiteral("enum")] = QJsonArray{ 4, 6, 8, 12, 20 };
            props[QStringLiteral("dice_sides")] = sides;
        }
        props[QStringLiteral("dice_count")] =
            Prop(QStringLiteral("integer"), QStringLiteral("骰子颗数 1~10（仅 dice，默认 2）"));
        props[QStringLiteral("options")] =
            Prop(QStringLiteral("string"),
                 QStringLiteral("候选项（仅 wheel/picker）；UTF-8 文本、每行一项，如 \"张三\\n李四\""));
        props[QStringLiteral("dedup")] =
            Prop(QStringLiteral("boolean"), QStringLiteral("抽中后自动移出（仅 wheel/picker）"));
        props[QStringLiteral("pick_count")] =
            Prop(QStringLiteral("integer"), QStringLiteral("点名器一次抽取人数 1~5（仅 picker，默认 1）"));
        tools.append(ToolDef(
            QStringLiteral("add_widget"),
            QStringLiteral("在白板上添加一个小工具卡片（计时器/秒表/骰子等）。x,y 为卡片中心"
                           "坐标（board 像素）。"),
            props, QJsonArray{ QStringLiteral("kind"), QStringLiteral("x"), QStringLiteral("y") }));
    }

    {
        QJsonObject props;
        props[QStringLiteral("text")] = Prop(QStringLiteral("string"), QStringLiteral("文字内容（支持 \\n 换行）"));
        props[QStringLiteral("x")] = Prop(QStringLiteral("number"), QStringLiteral("文字中心 x（board 像素）"));
        props[QStringLiteral("y")] = Prop(QStringLiteral("number"), QStringLiteral("文字中心 y（board 像素）"));
        props[QStringLiteral("font_size")] =
            Prop(QStringLiteral("integer"), QStringLiteral("像素字号（默认 32）"));
        props[QStringLiteral("color")] =
            Prop(QStringLiteral("string"), QStringLiteral("颜色 #RRGGBB（默认 #FFFFFF）"));
        tools.append(ToolDef(
            QStringLiteral("add_text"),
            QStringLiteral("在白板上添加一段文字。x,y 为文字中心坐标（board 像素）。"),
            props, QJsonArray{ QStringLiteral("text"), QStringLiteral("x"), QStringLiteral("y") }));
    }

    {
        QJsonObject shapeProps;
        shapeProps[QStringLiteral("kind")] =
            EnumProp(QStringLiteral("目标图形；rect/circle/ellipse/triangle/pentagon/star 用 "
                                    "box 参数；polygon/line 用 subpaths 参数"),
                     QJsonArray{ QStringLiteral("rect"), QStringLiteral("circle"),
                                 QStringLiteral("ellipse"), QStringLiteral("triangle"),
                                 QStringLiteral("pentagon"), QStringLiteral("star"),
                                 QStringLiteral("polygon"), QStringLiteral("line") });
        {
            QJsonObject box;
            box[QStringLiteral("type")] = QStringLiteral("array");
            box[QStringLiteral("description")] =
                QStringLiteral("[x,y,宽,高]（board 像素；仅参数化图形，圆按短边取正方形）");
            QJsonObject num;
            num[QStringLiteral("type")] = QStringLiteral("number");
            box[QStringLiteral("items")] = num;
            box[QStringLiteral("minItems")] = 4;
            box[QStringLiteral("maxItems")] = 4;
            shapeProps[QStringLiteral("box")] = box;
        }
        {
            QJsonObject spPoints;
            spPoints[QStringLiteral("type")] = QStringLiteral("array");
            spPoints[QStringLiteral("description")] = QStringLiteral("顶点列表（board 绝对坐标 [x,y]）");
            spPoints[QStringLiteral("items")] = PairProp(QStringLiteral("顶点 [x,y]"));
            QJsonObject spItem;
            spItem[QStringLiteral("type")] = QStringLiteral("object");
            QJsonObject spProps;
            spProps[QStringLiteral("closed")] =
                Prop(QStringLiteral("boolean"), QStringLiteral("是否闭合（polygon 默认 true，line 默认 false）"));
            spProps[QStringLiteral("points")] = spPoints;
            spItem[QStringLiteral("properties")] = spProps;
            spItem[QStringLiteral("required")] = QJsonArray{ QStringLiteral("points") };

            QJsonObject subpaths;
            subpaths[QStringLiteral("type")] = QStringLiteral("array");
            subpaths[QStringLiteral("description")] =
                QStringLiteral("路径列表（仅 polygon/line；polygon 每条 ≥3 点，line 每条 ≥2 点）");
            subpaths[QStringLiteral("items")] = spItem;
            shapeProps[QStringLiteral("subpaths")] = subpaths;
        }
        QJsonObject shape;
        shape[QStringLiteral("type")] = QStringLiteral("object");
        shape[QStringLiteral("description")] = QStringLiteral("目标图形定义");
        shape[QStringLiteral("properties")] = shapeProps;
        shape[QStringLiteral("required")] = QJsonArray{ QStringLiteral("kind") };

        QJsonObject props;
        {
            QJsonObject ids;
            ids[QStringLiteral("type")] = QStringLiteral("array");
            ids[QStringLiteral("description")] =
                QStringLiteral("源笔迹 id 列表（替换完成后被删除；get_board_state 或选中项获取）");
            QJsonObject str;
            str[QStringLiteral("type")] = QStringLiteral("string");
            ids[QStringLiteral("items")] = str;
            props[QStringLiteral("stroke_ids")] = ids;
        }
        props[QStringLiteral("shape")] = shape;
        props[QStringLiteral("color")] =
            Prop(QStringLiteral("string"), QStringLiteral("颜色 #RRGGBB（缺省沿用源笔迹颜色）"));
        props[QStringLiteral("width")] =
            Prop(QStringLiteral("integer"), QStringLiteral("线宽（缺省沿用源笔迹线宽）"));
        tools.append(ToolDef(
            QStringLiteral("replace_with_shape"),
            QStringLiteral("把一条或多条现有笔迹替换为一个标准图形（用于“美化”：用户画了近似"
                           "三角形/圆等潦草笔迹时，先识别出对应 stroke_ids，再调用本工具生成"
                           "规则图形）。用 box 传参数化图形范围；自定义多边形/直线用 subpaths"
                           "（board 绝对坐标）。替换为一步撤销。"),
            props, QJsonArray{ QStringLiteral("stroke_ids"), QStringLiteral("shape") }));
    }

    return tools;
}

QJsonObject AiExecutor::Execute(const QString& name, const QJsonObject& args) {
    if (name == QStringLiteral("get_board_state"))
        return GetBoardState(args.value(QStringLiteral("include_points")).toBool(true));
    if (name == QStringLiteral("draw_sketch"))
        return DrawSketch(args);
    if (name == QStringLiteral("erase_region"))
        return EraseRegion(args);
    if (name == QStringLiteral("delete_elements"))
        return DeleteElements(args);
    if (name == QStringLiteral("add_widget"))
        return AddWidget(args);
    if (name == QStringLiteral("add_text"))
        return AddText(args);
    if (name == QStringLiteral("replace_with_shape"))
        return ReplaceWithShape(args);
    return Error(QStringLiteral("未知工具：%1").arg(name));
}

QJsonObject AiExecutor::GetBoardState(bool includePoints) const {
    whiteboard::Page page;
    data_.GetPage(page);
    const QRectF viewport = view_.visibleBoardRect();

    QJsonArray elements;
    int total = 0;
    bool truncated = false;
    for (const auto& elementPtr : page.elements) {
        if (!elementPtr)
            continue;
        ++total;
        if (elements.size() >= kMaxStateElements) {
            truncated = true;
            continue;
        }
        elements.append(ElementSummary(*elementPtr, includePoints));
    }

    QJsonObject viewportObj;
    viewportObj[QStringLiteral("x")] = qRound(viewport.x());
    viewportObj[QStringLiteral("y")] = qRound(viewport.y());
    viewportObj[QStringLiteral("width")] = qRound(viewport.width());
    viewportObj[QStringLiteral("height")] = qRound(viewport.height());
    viewportObj[QStringLiteral("center")] =
        QJsonArray{ qRound(viewport.center().x()), qRound(viewport.center().y()) };

    QJsonObject out = Success();
    out[QStringLiteral("page_id")] = QString::fromStdString(page.pageId);
    out[QStringLiteral("viewport")] = viewportObj;
    out[QStringLiteral("element_count")] = total;
    out[QStringLiteral("truncated")] = truncated;
    out[QStringLiteral("elements")] = elements;
    return out;
}

QJsonObject AiExecutor::DrawSketch(const QJsonObject& args) {
    const QJsonObject frameObj = args.value(QStringLiteral("frame")).toObject();
    const QJsonArray center = frameObj.value(QStringLiteral("center")).toArray();
    const QJsonArray size = frameObj.value(QStringLiteral("size")).toArray();
    if (center.size() < 2 || size.size() < 2)
        return Error(QStringLiteral("frame 需要 center 与 size（[x,y] 与 [宽,高]）"));
    const double cx = ClampCoord(center.at(0).toDouble());
    const double cy = ClampCoord(center.at(1).toDouble());
    const double w = size.at(0).toDouble();
    const double h = size.at(1).toDouble();
    if (w < 1 || h < 1)
        return Error(QStringLiteral("frame.size 宽高必须 ≥1"));
    const QRectF frame(cx - qMin(w, kMaxExtent) / 2.0, cy - qMin(h, kMaxExtent) / 2.0,
                       qMin(w, kMaxExtent), qMin(h, kMaxExtent));

    const QJsonArray shapes = args.value(QStringLiteral("shapes")).toArray();
    if (shapes.isEmpty())
        return Error(QStringLiteral("shapes 为空"));
    if (shapes.size() > 60)
        return Error(QStringLiteral("shapes 超过上限 60 条（请拆分多次调用）"));

    QString buildError;
    const auto strokes = BuildStrokes(shapes, frame, view_.penColor(), view_.penWidth(),
                                      &buildError);
    if (strokes.empty())
        return Error(buildError.isEmpty() ? QStringLiteral("笔迹构建失败") : buildError);

    data_.BeginBatch();
    for (const auto& stroke : strokes)
        data_.AddElement(stroke);
    data_.EndBatch();

    QJsonObject out = Success();
    out[QStringLiteral("stroke_count")] = static_cast<int>(strokes.size());
    return out;
}

QJsonObject AiExecutor::EraseRegion(const QJsonObject& args) {
    double x = 0, y = 0, w = 0, h = 0;
    if (!NumberArg(args, QStringLiteral("x"), &x) || !NumberArg(args, QStringLiteral("y"), &y) ||
        !NumberArg(args, QStringLiteral("width"), &w) || !NumberArg(args, QStringLiteral("height"), &h))
        return Error(QStringLiteral("需要 x, y, width, height 四个数值参数"));
    if (w < 1 || h < 1)
        return Error(QStringLiteral("width/height 必须 ≥1"));
    const whiteboard::Rect rc(qRound(ClampCoord(x)), qRound(ClampCoord(y)),
                              qRound(qMin(w, kMaxExtent)), qRound(qMin(h, kMaxExtent)));
    data_.EraseRegion({ rc });
    QJsonObject out = Success();
    out[QStringLiteral("note")] = QStringLiteral("区域擦除已提交");
    return out;
}

QJsonObject AiExecutor::DeleteElements(const QJsonObject& args) {
    const QJsonArray idArr = args.value(QStringLiteral("element_ids")).toArray();
    if (idArr.isEmpty())
        return Error(QStringLiteral("element_ids 为空"));
    if (idArr.size() > 200)
        return Error(QStringLiteral("一次最多删除 200 个元素"));
    std::vector<std::string> ids;
    ids.reserve(static_cast<size_t>(idArr.size()));
    for (const QJsonValue& v : idArr) {
        const QString id = v.toString();
        if (!id.isEmpty())
            ids.push_back(id.toStdString());
    }
    if (ids.empty())
        return Error(QStringLiteral("element_ids 中没有有效 id"));
    data_.BeginBatch();
    data_.RemoveElements(ids);
    data_.EndBatch();
    QJsonObject out = Success();
    out[QStringLiteral("count")] = static_cast<int>(ids.size());
    return out;
}

QJsonObject AiExecutor::AddWidget(const QJsonObject& args) {
    const int kind = WidgetKindFromName(args.value(QStringLiteral("kind")).toString());
    if (kind < 0)
        return Error(QStringLiteral("kind 非法（stopwatch/timer/calculator/abacus/dice/wheel/picker）"));
    double cx = 0, cy = 0;
    if (!NumberArg(args, QStringLiteral("x"), &cx) || !NumberArg(args, QStringLiteral("y"), &cy))
        return Error(QStringLiteral("需要 x, y 数值参数（卡片中心坐标）"));

    auto widget = std::make_shared<whiteboard::WidgetElement>();
    widget->Reset();  // 默认值：5 分钟时长 / 1 倍缩放 / 6 面 2 颗骰子
    widget->kind = kind;
    widget->x = qRound(ClampCoord(cx) - kWidgetCardW * widget->scale / 2.0);
    widget->y = qRound(ClampCoord(cy) - kWidgetCardH * widget->scale / 2.0);
    if (kind == 1 && args.contains(QStringLiteral("duration_sec")))
        widget->durationSec = qBound(1, args.value(QStringLiteral("duration_sec")).toInt(300), 86400);
    if (kind == 4) {
        if (args.contains(QStringLiteral("dice_sides"))) {
            const int sides = args.value(QStringLiteral("dice_sides")).toInt(6);
            if (sides != 4 && sides != 6 && sides != 8 && sides != 12 && sides != 20)
                return Error(QStringLiteral("dice_sides 仅支持 4/6/8/12/20"));
            widget->diceSides = sides;
        }
        if (args.contains(QStringLiteral("dice_count")))
            widget->diceCount = qBound(1, args.value(QStringLiteral("dice_count")).toInt(2), 10);
    }
    if (kind == 5 || kind == 6) {
        if (args.contains(QStringLiteral("options")))
            widget->options = args.value(QStringLiteral("options")).toString().toUtf8().toStdString();
        if (args.contains(QStringLiteral("dedup")))
            widget->dedup = args.value(QStringLiteral("dedup")).toBool(false);
        if (kind == 6 && args.contains(QStringLiteral("pick_count")))
            widget->pickCount = qBound(1, args.value(QStringLiteral("pick_count")).toInt(1), 5);
    }

    data_.AddElement(widget);
    QJsonObject out = Success();
    out[QStringLiteral("id")] = QString::fromStdString(widget->id);
    return out;
}

QJsonObject AiExecutor::AddText(const QJsonObject& args) {
    const QString text = args.value(QStringLiteral("text")).toString();
    if (text.trimmed().isEmpty())
        return Error(QStringLiteral("text 为空"));
    if (text.size() > 2000)
        return Error(QStringLiteral("text 过长（≤2000 字符）"));
    double cx = 0, cy = 0;
    if (!NumberArg(args, QStringLiteral("x"), &cx) || !NumberArg(args, QStringLiteral("y"), &cy))
        return Error(QStringLiteral("需要 x, y 数值参数（文字中心坐标）"));
    const int fontSize = qBound(8, args.value(QStringLiteral("font_size")).toInt(32), 256);
    const uint32_t color =
        ColorFromHex(args.value(QStringLiteral("color")).toString(), 0x00FFFFFFu);

    auto element = std::make_shared<whiteboard::TextElement>();
    element->Reset();
    element->text = text.toUtf8().toStdString();
    element->fontSize = fontSize;
    element->color = color;
    // center → 锚点（左上角）：先用同源字体测字形偏移与尺寸，再精确换算
    element->x = 0;
    element->y = 0;
    const whiteboard::Rect measure = BoardView::textGlyphBounds(*element);
    element->x = qRound(ClampCoord(cx) - measure.x - measure.width / 2.0);
    element->y = qRound(ClampCoord(cy) - measure.y - measure.height / 2.0);
    element->bounds = BoardView::textGlyphBounds(*element);

    data_.AddElement(element);
    QJsonObject out = Success();
    out[QStringLiteral("id")] = QString::fromStdString(element->id);
    return out;
}

QJsonObject AiExecutor::ReplaceWithShape(const QJsonObject& args) {
    const QJsonArray idArr = args.value(QStringLiteral("stroke_ids")).toArray();
    if (idArr.isEmpty())
        return Error(QStringLiteral("stroke_ids 为空"));
    if (idArr.size() > 200)
        return Error(QStringLiteral("stroke_ids 超过上限 200"));

    // 过滤出真实存在的笔迹；缺省颜色/线宽取第一条源笔迹
    std::vector<std::string> validIds;
    uint32_t srcColor = 0x00FFFFFFu;
    int srcWidth = 3;
    bool haveSrc = false;
    for (const QJsonValue& v : idArr) {
        const std::string id = v.toString().toStdString();
        if (id.empty())
            continue;
        const auto snapshot = data_.GetElementSnapshot(id);
        if (!snapshot || snapshot->GetType() != "Stroke")
            continue;
        if (!haveSrc) {
            const auto& stroke = static_cast<const whiteboard::Stroke&>(*snapshot);
            srcColor = stroke.color;
            srcWidth = stroke.width;
            haveSrc = true;
        }
        validIds.push_back(id);
    }
    if (validIds.empty())
        return Error(QStringLiteral("stroke_ids 中没有找到有效笔迹"));

    const QJsonObject shape = args.value(QStringLiteral("shape")).toObject();
    if (shape.isEmpty())
        return Error(QStringLiteral("shape 缺失"));
    const QString kindName = shape.value(QStringLiteral("kind")).toString();

    int graphicKind = -1;
    bool isLine = false;
    std::vector<whiteboard::Subpath> subpaths;
    if (kindName == QStringLiteral("polygon") || kindName == QStringLiteral("line")) {
        isLine = kindName == QStringLiteral("line");
        graphicKind = isLine ? 7 : 6;  // GraphicKind::Line / Polygon
        QString parseError;
        if (!ParseSubpaths(shape.value(QStringLiteral("subpaths")).toArray(), isLine, &subpaths,
                           &parseError))
            return Error(parseError);
    } else {
        graphicKind = ParametricKindFromName(kindName);
        if (graphicKind < 0)
            return Error(QStringLiteral("shape.kind 非法（rect/circle/ellipse/triangle/pentagon/"
                                        "star/polygon/line）"));
        const QJsonArray box = shape.value(QStringLiteral("box")).toArray();
        if (box.size() < 4)
            return Error(QStringLiteral("shape.box 需要 [x, y, 宽, 高]"));
        const double bw = box.at(2).toDouble();
        const double bh = box.at(3).toDouble();
        if (bw < 1 || bh < 1)
            return Error(QStringLiteral("shape.box 宽高必须 ≥1"));
        const whiteboard::Rect rc(qRound(ClampCoord(box.at(0).toDouble())),
                                  qRound(ClampCoord(box.at(1).toDouble())),
                                  qRound(qMin(bw, kMaxExtent)), qRound(qMin(bh, kMaxExtent)));
        subpaths = whiteboard::BuildShape(graphicKind, rc);
        if (subpaths.empty())
            return Error(QStringLiteral("图形轮廓构建失败"));
    }

    const uint32_t color = ColorFromHex(args.value(QStringLiteral("color")).toString(), srcColor);
    const int width = qBound(1, args.value(QStringLiteral("width")).toInt(srcWidth), 64);

    auto graphic = std::make_shared<whiteboard::GraphicElement>();
    graphic->Reset();
    graphic->kind = graphicKind;
    graphic->width = width;
    graphic->color = color;
    graphic->subpaths = std::move(subpaths);
    graphic->Rebuild();

    data_.BeginBatch();
    data_.RemoveElements(validIds);
    data_.AddElement(graphic);
    data_.EndBatch();

    QJsonObject out = Success();
    out[QStringLiteral("id")] = QString::fromStdString(graphic->id);
    out[QStringLiteral("replaced_count")] = static_cast<int>(validIds.size());
    return out;
}

}  // namespace ai
