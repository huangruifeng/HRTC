#include "TcpRoomServer.h"
#include <Base/ErrorCode.h>
#include <Base/base.h>

using namespace hrtc;

hrtc::TcpRoomServer::TcpRoomServer(const HeartbeatConfig& heartbeat)
    : heartbeat_(heartbeat)
{
    server_.add_observer(this);
}

hrtc::TcpRoomServer::~TcpRoomServer()
{
    Stop();
}

int hrtc::TcpRoomServer::Start(int port)
{
    server_.async_run(port);
    // 心跳扫描定时器在 server loop 线程创建/启动
    server_.Post([this]() {
        if (hb_active_.load())
            return;
        uv_timer_init(server_.get_loop(), &hb_timer_);
        hb_timer_.data = this;
        uv_timer_start(&hb_timer_, onHeartbeatTimer, 1000, 1000);   // 每秒扫描
        hb_active_.store(true);
    });
    return HRTC_CODE_OK;
}

void hrtc::TcpRoomServer::Stop()
{
    server_.Post([this]() {
        if (hb_active_.load()) {
            hb_active_.store(false);
            uv_timer_stop(&hb_timer_);
            uv_close((uv_handle_t*)&hb_timer_, NULL);
        }
    });
    server_.stop();

    rooms_.Clear();
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        ctxs_.clear();
    }
    monitor_.Clear();
}

void hrtc::TcpRoomServer::KickUser(const std::string& roomId, const std::string& userId)
{
    auto room = rooms_.Get(roomId);
    if (!room)
        return;
    auto conn = room->GetConnection(userId);
    if (conn) {
        conn->close();   // 后续 on_connection_closed 统一清理并广播 LEAVE
    }
}

int hrtc::TcpRoomServer::Unicast(const std::string& roomId, const std::string& fromId,
                                 const std::string& toId, const std::string& data)
{
    auto room = rooms_.Get(roomId);
    if (!room)
        return HRTC_CODE_ERROR_NOT_FOUND;

    DataStreamType stream;
    stream.type = static_cast<int>(DataStreamType::Type::UNICAST);
    stream.param = { fromId, toId, data };
    return room->Unicast(toId, SerializeStream(stream));
}

int hrtc::TcpRoomServer::Broadcast(const std::string& roomId, const std::string& fromId,
                                   const std::string& data)
{
    auto room = rooms_.Get(roomId);
    if (!room)
        return HRTC_CODE_ERROR_NOT_FOUND;

    DataStreamType stream;
    stream.type = static_cast<int>(DataStreamType::Type::BROADCAST);
    stream.param = { fromId, data };
    return room->Broadcast(SerializeStream(stream));
}

std::vector<std::string> hrtc::TcpRoomServer::GetUserIds(const std::string& roomId) const
{
    auto room = rooms_.Get(roomId);
    if (!room)
        return {};
    return room->GetUserIds();
}

std::vector<std::string> hrtc::TcpRoomServer::GetRoomIds() const
{
    return rooms_.GetRoomIds();
}

void hrtc::TcpRoomServer::on_new_connection(const std::shared_ptr<TcpConnection>& connection)
{
    // loop 线程
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        ConnCtx ctx;
        ctx.conn = connection;
        ctxs_.emplace(connection.get(), std::move(ctx));
    }
    connection->addObserver(this);
}

void hrtc::TcpRoomServer::on_data_received(TcpConnection* conn, const std::string& data)
{
    // loop 线程
    monitor_.Touch(conn);   // 收到任何数据都视为活跃

    DataStreamType stream;
    if (!DeserializeStream(data, stream)) {
        observers_.Foreach([&](auto ptr) {
            ptr->on_error(HRTC_CODE_ERROR_FAILURE, "deserialize stream failed");
        });
        return;
    }

    switch (static_cast<DataStreamType::Type>(stream.type)) {
    case DataStreamType::Type::JOIN:
        HandleJoin(conn, stream);
        break;
    case DataStreamType::Type::LEAVE:
        HandleLeave(conn, stream);
        break;
    case DataStreamType::Type::UNICAST:
        HandleUnicast(conn, stream);
        break;
    case DataStreamType::Type::BROADCAST:
        HandleBroadcast(conn, stream);
        break;
    case DataStreamType::Type::PING: {
        DataStreamType pong;
        pong.type = static_cast<int>(DataStreamType::Type::PONG);
        conn->write(SerializeStream(pong));
        break;
    }
    case DataStreamType::Type::PONG:
        break;   // 宽容处理：客户端一般只收 PONG
    default:
        observers_.Foreach([&](auto ptr) {
            ptr->on_error(-1, "unknown stream type");
        });
        break;
    }
}

void hrtc::TcpRoomServer::on_connection_closed(TcpConnection* conn)
{
    // loop 线程（连接 close 回调）
    RemoveUserFromRoom(conn, true);
    monitor_.Erase(conn);

    // 延迟到下一轮 loop 释放连接引用（保证 TcpConnection 的 async 句柄
    // close 回调先执行完毕）
    server_.Post([this, conn]() {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        ctxs_.erase(conn);
    });
}

void hrtc::TcpRoomServer::HandleJoin(TcpConnection* conn, const DataStreamType& stream)
{
    // param: [roomId, userId]
    if (stream.param.size() < 2) {
        observers_.Foreach([&](auto ptr) {
            ptr->on_error(HRTC_CODE_ERROR_INVALID_ARG, "JOIN requires [roomId, userId]");
        });
        return;
    }

    const std::string& roomId = stream.param[0];
    const std::string& userId = stream.param[1];

    ConnCtx* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        auto it = ctxs_.find(conn);
        if (it == ctxs_.end())
            return;
        ctx = &it->second;
        if (ctx->joined)
            return;   // 已加入，忽略重复 JOIN
    }

    auto room = rooms_.GetOrCreate(roomId);
    // 判断是否第一个加入：AddUser 之前房间已有用户时，让存在最久的用户
    // 把全量数据单播给新用户（onRequestData）
    const std::string oldest = room->UserCount() > 0 ? room->GetOldestUserId() : std::string();
    if (room->AddUser(userId, ctx->conn) != HRTC_CODE_OK) {
        observers_.Foreach([&](auto ptr) {
            ptr->on_error(HRTC_CODE_ERROR_DUPLICATED, "user already in room: " + userId);
        });
        return;
    }

    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        ctx->userId = userId;
        ctx->roomId = roomId;
        ctx->joined = true;
    }

    // 通知房间内其他用户有新成员加入
    DataStreamType join;
    join.type = static_cast<int>(DataStreamType::Type::JOIN);
    join.param = { roomId, userId };
    room->Broadcast(SerializeStream(join), userId);

    // 非第一人：向存在最久的用户发送 onRequestData，由其单播全量数据给新用户
    if (!oldest.empty()) {
        DataStreamType request;
        request.type = static_cast<int>(DataStreamType::Type::REQUEST_DATA);
        request.param = { userId, roomId };
        room->Unicast(oldest, SerializeStream(request));
    }

    observers_.Foreach([&](auto ptr) {
        ptr->on_user_joined(roomId, userId);
    });
}

void hrtc::TcpRoomServer::HandleLeave(TcpConnection* conn, const DataStreamType&)
{
    RemoveUserFromRoom(conn, true);
}

void hrtc::TcpRoomServer::HandleUnicast(TcpConnection* conn, const DataStreamType& stream)
{
    // param: [fromId, toId, data]
    if (stream.param.size() < 3) {
        observers_.Foreach([&](auto ptr) {
            ptr->on_error(HRTC_CODE_ERROR_INVALID_ARG, "UNICAST requires [fromId, toId, data]");
        });
        return;
    }

    const std::string& fromId = stream.param[0];
    const std::string& toId = stream.param[1];

    ConnCtx* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        auto it = ctxs_.find(conn);
        if (it == ctxs_.end())
            return;
        ctx = &it->second;
    }
    if (!ctx->joined)
        return;

    auto room = rooms_.Get(ctx->roomId);
    if (!room)
        return;
    room->Unicast(toId, SerializeStream(stream));

    observers_.Foreach([&](auto ptr) {
        ptr->on_unicast(ctx->roomId, fromId, toId, stream.param[2]);
    });
}

void hrtc::TcpRoomServer::HandleBroadcast(TcpConnection* conn, const DataStreamType& stream)
{
    // param: [fromId, data]
    if (stream.param.size() < 2) {
        observers_.Foreach([&](auto ptr) {
            ptr->on_error(HRTC_CODE_ERROR_INVALID_ARG, "BROADCAST requires [fromId, data]");
        });
        return;
    }

    const std::string& fromId = stream.param[0];

    ConnCtx* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        auto it = ctxs_.find(conn);
        if (it == ctxs_.end())
            return;
        ctx = &it->second;
    }
    if (!ctx->joined)
        return;

    auto room = rooms_.Get(ctx->roomId);
    if (!room)
        return;
    room->Broadcast(SerializeStream(stream), fromId);

    observers_.Foreach([&](auto ptr) {
        ptr->on_broadcast(ctx->roomId, fromId, stream.param[1]);
    });
}

void hrtc::TcpRoomServer::RemoveUserFromRoom(TcpConnection* conn, bool notifyObserver)
{
    // loop 线程
    ConnCtx* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        auto it = ctxs_.find(conn);
        if (it == ctxs_.end())
            return;
        ctx = &it->second;
        if (!ctx->joined)
            return;
    }

    auto room = rooms_.Get(ctx->roomId);
    if (room) {
        room->RemoveUser(ctx->userId);

        // 广播 LEAVE 给房间内其他用户
        DataStreamType leave;
        leave.type = static_cast<int>(DataStreamType::Type::LEAVE);
        leave.param = { ctx->userId };
        room->Broadcast(SerializeStream(leave), ctx->userId);

        rooms_.RemoveIfEmpty(ctx->roomId);
    }

    const std::string roomId = ctx->roomId;
    const std::string userId = ctx->userId;
    {
        std::lock_guard<std::mutex> lock(ctx_mutex_);
        ctx->joined = false;
    }

    if (notifyObserver) {
        observers_.Foreach([&](auto ptr) {
            ptr->on_user_leave(roomId, userId);
        });
    }
}

void hrtc::TcpRoomServer::onHeartbeatTimer(uv_timer_t* handle)
{
    // loop 线程
    TcpRoomServer* server = static_cast<TcpRoomServer*>(handle->data);
    const auto keys = server->monitor_.GetTimeoutKeys(NowMs(), server->heartbeat_.timeoutMs);
    for (auto* conn : keys) {
        LOG_WARNING("TcpRoomServer", "heartbeat timeout, kick connection");
        server->monitor_.Erase(conn);
        conn->close();   // on_connection_closed 统一清理
    }
}
