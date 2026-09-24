#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class BoardView;
class QtBoardData;

namespace ai {

// 白板工具集：7 个工具的 JSON Schema 定义与执行器。
// 全部在 UI 线程调用；写操作统一走 QtBoardData（PushSnapshot 可撤销 +
// 命令广播房间同步），多元素操作用 BeginBatch/EndBatch 合并为一步撤销。
// 返回值约定：{"ok":true, ...} 或 {"ok":false, "error":"…"}（回填给模型）。
class AiExecutor {
public:
    AiExecutor(BoardView& view, QtBoardData& data);

    // 工具 schema 列表（OpenAI 兼容 tools 字段：type=function + function.{name,
    // description, parameters}）
    static QJsonArray ToolSchemas();

    // 执行一次工具调用；未知工具名/非法参数一律转错误 JSON，不抛异常
    QJsonObject Execute(const QString& name, const QJsonObject& args);

private:
    QJsonObject GetBoardState(bool includePoints) const;
    QJsonObject DrawSketch(const QJsonObject& args);
    QJsonObject EraseRegion(const QJsonObject& args);
    QJsonObject DeleteElements(const QJsonObject& args);
    QJsonObject AddWidget(const QJsonObject& args);
    QJsonObject AddText(const QJsonObject& args);
    QJsonObject ReplaceWithShape(const QJsonObject& args);

    BoardView& view_;
    QtBoardData& data_;
};

}  // namespace ai
