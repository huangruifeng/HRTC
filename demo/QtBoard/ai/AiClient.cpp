#include "AiClient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace ai {

namespace {

constexpr int kTimeoutMs = 60000;  // 单请求超时（含模型思考时间）

// baseUrl 规范化：去尾斜杠；未以 /chat/completions 结尾时补全
// （兼容 https://api.deepseek.com、.../v1、Ollama/其他兼容服务自定义地址）
QString EndpointUrl(const QString& baseUrl) {
    QString url = baseUrl.trimmed();
    while (url.endsWith(QLatin1Char('/')))
        url.chop(1);
    if (!url.endsWith(QStringLiteral("/chat/completions")))
        url += QStringLiteral("/chat/completions");
    return url;
}

// 截断过长错误文本（防把整页 HTML 灌进面板；绝不回显请求头/Key）
QString ShortenError(const QString& raw) {
    QString text = raw.trimmed();
    if (text.size() > 300)
        text = text.left(300) + QStringLiteral("…");
    return text;
}

}  // namespace

bool AiConfig::SupportsTools() const {
    return !model.contains(QStringLiteral("reasoner"), Qt::CaseInsensitive);
}

AiConfig AiConfig::Load() {
    const QSettings settings(QStringLiteral("HRTC"), QStringLiteral("Board"));
    AiConfig c;
    c.apiKey = settings.value(QStringLiteral("ai/apiKey")).toString();
    if (c.apiKey.isEmpty())
        c.apiKey = qEnvironmentVariable("DEEPSEEK_API_KEY");
    const QString baseUrl = settings.value(QStringLiteral("ai/baseUrl")).toString();
    if (!baseUrl.isEmpty())
        c.baseUrl = baseUrl;
    else if (!qEnvironmentVariable("HRTC_AI_BASE_URL").isEmpty())
        c.baseUrl = qEnvironmentVariable("HRTC_AI_BASE_URL");
    const QString model = settings.value(QStringLiteral("ai/model")).toString();
    if (!model.isEmpty())
        c.model = model;
    else if (!qEnvironmentVariable("HRTC_AI_MODEL").isEmpty())
        c.model = qEnvironmentVariable("HRTC_AI_MODEL");
    return c;
}

void AiConfig::Save() const {
    QSettings settings(QStringLiteral("HRTC"), QStringLiteral("Board"));
    settings.setValue(QStringLiteral("ai/apiKey"), apiKey);
    settings.setValue(QStringLiteral("ai/baseUrl"), baseUrl);
    settings.setValue(QStringLiteral("ai/model"), model);
}

AiClient::AiClient(QObject* parent) : QObject(parent) {
    nam_ = new QNetworkAccessManager(this);
    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, [this]() {
        if (!reply_)
            return;
        abortKind_ = AbortKind::Timeout;
        reply_->abort();  // abort 触发 finished → 统一走 OnReplyFinished
    });
}

void AiClient::SendChat(const QJsonArray& messages, const QJsonArray& tools, bool withTools,
                        const AiConfig& config, ChatCallback callback) {
    if (reply_) {  // 并发防御（上层已互斥）：立即失败回调，不打断在途请求
        if (callback)
            callback(false, AiReply(), QStringLiteral("上一次请求尚未完成"));
        return;
    }
    if (config.apiKey.trimmed().isEmpty()) {
        if (callback)
            callback(false, AiReply(), QStringLiteral("尚未配置 API Key（点击面板右上角齿轮设置）"));
        return;
    }

    QJsonObject body;
    body[QStringLiteral("model")] = config.model;
    body[QStringLiteral("messages")] = messages;
    body[QStringLiteral("stream")] = false;
    body[QStringLiteral("temperature")] = 0.3;
    if (withTools && config.SupportsTools()) {
        body[QStringLiteral("tools")] = tools;
        body[QStringLiteral("tool_choice")] = QStringLiteral("auto");
    }

    QNetworkRequest request{ QUrl(EndpointUrl(config.baseUrl)) };
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         QByteArrayLiteral("Bearer ") + config.apiKey.trimmed().toUtf8());
    request.setRawHeader("Accept", "application/json");

    callback_ = std::move(callback);
    abortKind_ = AbortKind::None;
    reply_ = nam_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::finished, this, &AiClient::OnReplyFinished);
    timeoutTimer_->start(kTimeoutMs);
    emit busyChanged(true);
}

void AiClient::Cancel() {
    if (!reply_)
        return;
    abortKind_ = AbortKind::Cancelled;
    reply_->abort();
}

void AiClient::OnReplyFinished() {
    QNetworkReply* reply = reply_;
    if (!reply)
        return;
    reply_ = nullptr;
    timeoutTimer_->stop();

    const QByteArray payload = reply->readAll();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netError = reply->error();
    const QString netErrorText = reply->errorString();
    reply->deleteLater();

    // 错误体通常为 {"error":{"message":"…"}}：优先取业务错误文案
    QString serverMessage;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        const QJsonObject errObj = obj.value(QStringLiteral("error")).toObject();
        if (!errObj.isEmpty())
            serverMessage = errObj.value(QStringLiteral("message")).toString();
        else if (obj.contains(QStringLiteral("message")))
            serverMessage = obj.value(QStringLiteral("message")).toString();
    }

    if (netError != QNetworkReply::NoError) {
        QString error;
        if (abortKind_ == AbortKind::Timeout)
            error = QStringLiteral("请求超时（60 秒无响应），请稍后重试");
        else if (abortKind_ == AbortKind::Cancelled)
            error = QStringLiteral("已取消");
        else if (netErrorText.contains(QStringLiteral("TLS initialization failed")))
            error = QStringLiteral("HTTPS 初始化失败：缺少 OpenSSL 运行库（libssl-1_1-x64.dll / "
                                   "libcrypto-1_1-x64.dll）。请运行 3rd/openssl/build_openssl.ps1 "
                                   "构建并重新编译，或手动将这两个 DLL 放到程序目录");
        else if (httpStatus > 0)
            error = QStringLiteral("请求失败（HTTP %1）：%2")
                        .arg(httpStatus)
                        .arg(serverMessage.isEmpty() ? netErrorText : serverMessage);
        else
            error = QStringLiteral("网络错误：%1")
                        .arg(serverMessage.isEmpty() ? netErrorText : serverMessage);
        abortKind_ = AbortKind::None;
        Finish(false, AiReply(), ShortenError(error));
        return;
    }
    abortKind_ = AbortKind::None;

    if (!doc.isObject()) {
        Finish(false, AiReply(), QStringLiteral("响应不是有效 JSON"));
        return;
    }
    const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        Finish(false, AiReply(), QStringLiteral("响应缺少 choices 字段"));
        return;
    }
    const QJsonObject message =
        choices.first().toObject().value(QStringLiteral("message")).toObject();
    AiReply replyResult;
    replyResult.content = message.value(QStringLiteral("content")).toString();
    replyResult.toolCalls = message.value(QStringLiteral("tool_calls")).toArray();
    if (replyResult.content.isEmpty() && replyResult.toolCalls.isEmpty()) {
        Finish(false, AiReply(), QStringLiteral("模型返回为空"));
        return;
    }
    Finish(true, replyResult, QString());
}

// 统一收尾：回调仅触发一次；busyChanged(false) 先于回调（面板可先复位按钮态）
void AiClient::Finish(bool ok, const AiReply& reply, const QString& error) {
    ChatCallback callback = std::move(callback_);
    callback_ = nullptr;
    emit busyChanged(false);
    if (callback)
        callback(ok, reply, error);
}

}  // namespace ai
