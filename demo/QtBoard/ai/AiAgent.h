#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <memory>

#include "AiClient.h"

class BoardView;
class QtBoardData;

namespace ai {

class AiExecutor;

// 会话智能体：持有会话历史（内存态）与动态系统提示词，驱动
// 「请求模型 → 执行工具 → 结果回填 → 再请求」循环，直到模型给出纯文本回复。
// 单轮上限 6 次模型请求；检测到重复工具调用（同名同参数）即中止。
class AiAgent : public QObject {
    Q_OBJECT
public:
    AiAgent(BoardView& view, QtBoardData& data, QObject* parent = nullptr);
    ~AiAgent() override;

    bool IsBusy() const { return busy_; }
    void Send(const QString& userText);  // 用户消息入口（忙碌时拒绝并发）
    void BeautifySelection();  // 美化选中笔迹：直接携带选中数据请求 replace_with_shape
    void Cancel();             // 中断在途请求（含工具循环）
    void ClearHistory();       // 清空会话历史（忙碌时忽略）

signals:
    void busyChanged(bool busy);
    void statusChanged(const QString& status);  // 中间态提示（"正在思考…"）
    void actionDone(const QString& summary);    // 每个工具执行摘要
    void replyReady(const QString& text);       // 最终文本回复
    void errorOccurred(const QString& error);   // 失败提示

private:
    void StartRound(const QString& userContent);  // 追加 user 消息并进入循环
    QJsonArray BuildMessages() const;             // system（动态视口）+ 历史
    QString SystemPrompt() const;
    void RequestNext();
    void OnReply(bool ok, const AiReply& reply, const QString& error);
    bool ExecuteToolCalls(const QJsonArray& toolCalls);  // false = 需中止循环
    QString ToolSummary(const QString& name, const QJsonObject& result) const;
    void FinishRound(const QString& text);  // 正常收尾（text 可空则不回显）
    void FinishWithError(const QString& error);
    void TrimHistory();  // 保留最近 10 轮（从 user 消息边界裁剪）

    BoardView& view_;
    QtBoardData& data_;
    std::unique_ptr<AiExecutor> executor_;
    std::unique_ptr<AiClient> client_;

    QJsonArray history_;         // OpenAI 格式消息数组（含 assistant/tool 角色）
    QSet<QString> calledKeys_;   // 本轮已调用的工具签名（name+args），防死循环
    QString pendingReplyText_;   // 最近一次模型的随伴文本（循环中止时回显）
    bool busy_ = false;
    bool cancelled_ = false;
    int iterations_ = 0;  // 本轮模型请求次数

    static constexpr int kMaxIterations = 6;  // 单轮模型请求上限
    static constexpr int kMaxHistoryTurns = 10;  // 历史保留轮数
};

}  // namespace ai
