#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <Base/Collections.h>
#include "Transport/tcp/TcpServer.h"
#include "Transport/room/RoomManager.h"
#include "Transport/heartbeat/Heartbeat.h"
#include "Transport/common/CommonType.h"

namespace hrtc {

class RoomServerObserver {
public:
    virtual ~RoomServerObserver() = default;
    virtual void on_user_joined(const std::string& roomId, const std::string& userId) = 0;
    virtual void on_user_leave(const std::string& roomId, const std::string& userId) = 0;
    virtual void on_unicast(const std::string& roomId, const std::string& fromId,
                            const std::string& toId, const std::string& data) = 0;
    virtual void on_broadcast(const std::string& roomId, const std::string& fromId,
                              const std::string& data) = 0;
    virtual void on_error(int errorCode, const std::string& error) = 0;
};

// TCP 房间服务端：在 TcpServer 之上维护 Room 集合，处理 JOIN/LEAVE/
// UNICAST/BROADCAST 协议消息，并做双向心跳（收到任何数据即刷新活跃时间，
// 定时器扫描超时连接并踢除）。
// 线程模型：消息处理在 TcpServer 的 loop 线程；对外 API（KickUser/Unicast/
// Broadcast）可跨线程调用（内部数据结构均线程安全）。
class TcpRoomServer : public TcpServerObserver, public TcpConnectionObserver {
public:
    explicit TcpRoomServer(const HeartbeatConfig& heartbeat = HeartbeatConfig());
    ~TcpRoomServer();

    int Start(int port);
    void Stop();

    // 对外 API（线程安全）
    void KickUser(const std::string& roomId, const std::string& userId);
    int Unicast(const std::string& roomId, const std::string& fromId,
                const std::string& toId, const std::string& data);
    int Broadcast(const std::string& roomId, const std::string& fromId,
                  const std::string& data);

    std::vector<std::string> GetUserIds(const std::string& roomId) const;
    std::vector<std::string> GetRoomIds() const;

    void add_observer(RoomServerObserver* observer) { observers_.AddElement(observer); }
    void remove_observer(RoomServerObserver* observer) { observers_.RemoveElement(observer); }

    // TcpServerObserver（loop 线程）
    void on_new_connection(const std::shared_ptr<TcpConnection>& connection) override;

    // TcpConnectionObserver（loop 线程）
    void on_data_received(TcpConnection* conn, const std::string& data) override;
    void on_connection_closed(TcpConnection* conn) override;
    void on_connection_connected(TcpConnection*) override {}

private:
    struct ConnCtx {
        std::shared_ptr<TcpConnection> conn;
        std::string userId;
        std::string roomId;
        bool joined = false;
    };

    // 以下均在 loop 线程执行
    void HandleJoin(TcpConnection* conn, const DataStreamType& stream);
    void HandleLeave(TcpConnection* conn, const DataStreamType& stream);
    void HandleUnicast(TcpConnection* conn, const DataStreamType& stream);
    void HandleBroadcast(TcpConnection* conn, const DataStreamType& stream);
    void RemoveUserFromRoom(TcpConnection* conn, bool notifyObserver);
    static void onHeartbeatTimer(uv_timer_t* handle);

private:
    TcpServer server_;
    RoomManager rooms_;
    HeartbeatConfig heartbeat_;
    ServerHeartbeatMonitor<TcpConnection*> monitor_;

    uv_timer_t hb_timer_;
    std::atomic<bool> hb_active_{false};

    std::mutex ctx_mutex_;
    std::map<TcpConnection*, ConnCtx> ctxs_;    // ctx_mutex_ 保护

    hrtc::Collections<RoomServerObserver, hrtc::RawPointer, hrtc::MultiThreaded> observers_;
};

}
