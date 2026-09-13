#pragma once
#include <QObject>
#include <QString>

#include <memory>
#include <string>

#include "QtBoardData.h"
#include "Transport/stream/DataStream.h"
#include "Whiteboard/protocol/command_codec.h"

// 互动白板会话：聚合 DataStream（网络）+ QtBoardData（数据）。
// - Start：连接服务器并加入房间，注册 QtBoardData 命令产出回调（本地操作 → 广播）；
// - observer 回调在 DataStream api 线程触发：远程命令反序列化后投递 QtBoardData 远程接口
//   （QtBoardData 内部线程安全）；状态通知经 Qt 信号（跨线程自动 queued）送回 UI 线程；
// - onRequestData：全量导出页面 → FullSync 单播给请求者。
class WhiteboardSession : public QObject, public hrtc::DatastreamObserver {
    Q_OBJECT
public:
    explicit WhiteboardSession(QtBoardData& data, QObject* parent = nullptr);
    ~WhiteboardSession() override;

    // 连接服务器并加入房间（失败返回 false，成功仅表示请求已发出；
    // 实际入房状态以 joined 信号为准）
    bool Start(const std::string& ip, int port,
               const std::string& roomId, const std::string& userId);
    // 离开房间并断开（同时注销命令产出回调）
    void Stop();
    bool IsActive() const { return stream_ != nullptr; }
    std::string UserId() const { return userId_; }

signals:
    void connected();                       // TCP 已连接
    void joined();                          // 已加入房间（可开始互动）
    void disconnected();                    // 连接断开/已停止
    void errorOccurred(const QString& message);  // 网络/协议错误

private:
    // ---- DatastreamObserver（DataStream api 线程触发） ----
    void onUserJoined(const std::string& userId) override;
    void onUnicast(const std::string& fromId, const std::string& toId,
                   const std::string& data) override;
    void onBroadcast(const std::string& fromId, const std::string& data) override;
    void onUserLeave(const std::string& userId) override;
    void onRequestData(const std::string& requesterId) override;
    void onError(int errorCode, const std::string& error) override;
    void onConnected() override;
    void onDisconnected() override;

    // 反序列化并按类型分派到 QtBoardData 远程接口（api 线程调用）
    void DispatchCommand(const std::shared_ptr<whiteboard::Command>& cmd);

    QtBoardData& data_;
    std::unique_ptr<hrtc::DataStream> stream_;
    std::string userId_;
    std::string roomId_;
};
