#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace ai {

// AI 接入配置（OpenAI 兼容协议）。
// 读取优先级：QSettings("HRTC","Board") 键 ai/apiKey|baseUrl|model >
// 环境变量 DEEPSEEK_API_KEY / HRTC_AI_BASE_URL / HRTC_AI_MODEL > 内置默认值。
struct AiConfig {
    QString apiKey;  // 留空 = 未配置（面板设置行写入）
    QString baseUrl = QStringLiteral("https://api.deepseek.com");
    QString model = QStringLiteral("deepseek-chat");

    // 当前模型是否支持 Function Calling（deepseek-reasoner 系列不支持）
    bool SupportsTools() const;
    static AiConfig Load();  // QSettings > 环境变量 > 默认值
    void Save() const;       // 写入 QSettings（API Key 明文存储，面板已注明）
};

// 模型回复：助手文本 + tool_calls（原样透传，含 id / function.name / arguments）。
struct AiReply {
    QString content;       // 助手文本（纯工具轮次可能为空串）
    QJsonArray toolCalls;  // choices[0].message.tool_calls
};

// LLM HTTP 客户端（Qt5::Network）：POST {baseUrl}/chat/completions，Bearer 鉴权。
// 单请求串行（上层已保证互斥）；60s 超时自动中断；Cancel() 主动断开。
class AiClient : public QObject {
    Q_OBJECT
public:
    // 回调在 UI 线程触发（QNetworkReply::finished 信号线程）；ok=false 时 error 可读展示
    using ChatCallback = std::function<void(bool ok, const AiReply& reply, const QString& error)>;

    explicit AiClient(QObject* parent = nullptr);

    bool IsBusy() const { return reply_ != nullptr; }
    // 发送一轮对话（stream:false, temperature:0.3）；withTools=false 时不带 tools 字段
    // （reasoner 等不支持工具调用的模型仅纯对话）
    void SendChat(const QJsonArray& messages, const QJsonArray& tools, bool withTools,
                  const AiConfig& config, ChatCallback callback);
    void Cancel();  // 中断在途请求（回调以 ok=false 返回"已取消"）

signals:
    void busyChanged(bool busy);

private:
    enum class AbortKind { None, Timeout, Cancelled };

    void OnReplyFinished();
    void Finish(bool ok, const AiReply& reply, const QString& error);

    QNetworkAccessManager* nam_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QTimer* timeoutTimer_ = nullptr;
    ChatCallback callback_;
    AbortKind abortKind_ = AbortKind::None;  // Cancel/超时中断：错误文案区分
};

}  // namespace ai
