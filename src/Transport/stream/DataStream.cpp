#include "DataStream.h"

using namespace hrtc;

hrtc::DataStream::DataStream(const std::string& serverIp, int serverPort, const HeartbeatConfig& heartbeat)
    : client_(heartbeat)
    , api_thread_(dispatch_task_queue::create("dataStream"))
    , server_ip_(serverIp)
    , server_port_(serverPort)
{
    client_.add_observer(this);
}

hrtc::DataStream::~DataStream()
{
    // 先解除观察者（Collections 的 Foreach/Remove 互斥，之后不会再有新回调），
    // 再清空队列并同步屏障，确保 api 线程不再访问本对象。
    client_.remove_observer(this);
    api_thread_->clear();
    api_thread_->sync([]() {});
}

std::string hrtc::DataStream::currentUser() const
{
    std::string user;
    api_thread_->sync([&]() {
        user = current_user_;
    });
    return user;
}

std::string hrtc::DataStream::currentRoom() const
{
    std::string room;
    api_thread_->sync([&]() {
        room = current_room_;
    });
    return room;
}

RtcResult hrtc::DataStream::Connect(const std::string& serverIp, int serverPort)
{
    RtcResult ret = HRTC_CODE_OK;
    api_thread_->sync([&]() {
        if (serverIp.empty() || serverPort <= 0) {
            ret = HRTC_CODE_ERROR_INVALID_ARG;
            return;
        }
        server_ip_ = serverIp;
        server_port_ = serverPort;
        client_.connect(serverIp, serverPort);
    });
    return ret;
}

RtcResult hrtc::DataStream::Disconnect()
{
    RtcResult ret = HRTC_CODE_OK;
    api_thread_->sync([&]() {
        joined_ = false;
        pending_join_ = false;
        client_.disconnect();
    });
    return ret;
}

RtcResult hrtc::DataStream::JoinRoom(const std::string& roomId, const std::string& userId)
{
    RtcResult ret = HRTC_CODE_OK;
    api_thread_->sync([&]() {
        if (roomId.empty() || userId.empty()) {
            ret = HRTC_CODE_ERROR_INVALID_ARG;
            return;
        }
        if (joined_) {
            ret = HRTC_CODE_ERROR_WRONG_STATE;
            return;
        }
        if (server_ip_.empty() || server_port_ <= 0) {
            ret = HRTC_CODE_ERROR_INVALID_CONFIG;
            return;
        }
        current_room_ = roomId;
        current_user_ = userId;
        pending_join_ = true;

        if (!client_.isConnected()) {
            // 连接成功后 on_connected 中自动发送 JOIN
            client_.connect(server_ip_, server_port_);
            return;
        }

        DataStreamType join;
        join.type = static_cast<int>(DataStreamType::Type::JOIN);
        join.param = { roomId, userId };
        if (client_.sendMessage(SerializeStream(join)) == 0) {
            joined_ = true;
            pending_join_ = false;
        }
        else {
            ret = HRTC_CODE_API_EXECUTION_FAILURE;
        }
    });
    return ret;
}

RtcResult hrtc::DataStream::Unicast(const std::string& toId, const std::string& data)
{
    RtcResult ret = HRTC_CODE_OK;
    api_thread_->sync([&]() {
        if (!joined_) {
            ret = HRTC_CODE_ERROR_WRONG_STATE;
            return;
        }
        DataStreamType stream;
        stream.type = static_cast<int>(DataStreamType::Type::UNICAST);
        stream.param = { current_user_, toId, data };
        ret = Send(stream);
    });
    return ret;
}

RtcResult hrtc::DataStream::Broadcast(const std::string& data)
{
    RtcResult ret = HRTC_CODE_OK;
    api_thread_->sync([&]() {
        if (!joined_) {
            ret = HRTC_CODE_ERROR_WRONG_STATE;
            return;
        }
        DataStreamType stream;
        stream.type = static_cast<int>(DataStreamType::Type::BROADCAST);
        stream.param = { current_user_, data };
        ret = Send(stream);
    });
    return ret;
}

RtcResult hrtc::DataStream::LeaveRoom()
{
    RtcResult ret = HRTC_CODE_OK;
    api_thread_->sync([&]() {
        if (!joined_) {
            ret = HRTC_CODE_ERROR_WRONG_STATE;
            return;
        }
        DataStreamType stream;
        stream.type = static_cast<int>(DataStreamType::Type::LEAVE);
        stream.param = { current_user_ };
        ret = Send(stream);

        joined_ = false;
        pending_join_ = false;
    });
    return ret;
}

RtcResult hrtc::DataStream::Send(const DataStreamType& stream)
{
    // 仅 api_thread_ 线程调用
    if (!client_.isConnected())
        return HRTC_CODE_ERROR_NOT_INITIALIZED;
    return client_.sendMessage(SerializeStream(stream)) == 0
        ? HRTC_CODE_OK
        : HRTC_CODE_API_EXECUTION_FAILURE;
}

void hrtc::DataStream::on_connected()
{
    // TcpClient loop 线程回调，投递 api_thread_ 串行处理
    api_thread_->async([this]() {
        if (pending_join_) {
            DataStreamType join;
            join.type = static_cast<int>(DataStreamType::Type::JOIN);
            join.param = { current_room_, current_user_ };
            if (client_.sendMessage(SerializeStream(join)) == 0) {
                joined_ = true;
                pending_join_ = false;
            }
        }

        observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
            ptr->onConnected();
        });
    });
}

void hrtc::DataStream::on_disconnected()
{
    // TcpClient loop 线程回调，投递 api_thread_ 串行处理
    api_thread_->async([this]() {
        joined_ = false;
        pending_join_ = false;
        observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
            ptr->onDisconnected();
        });
    });
}

void hrtc::DataStream::on_message(const std::string& data)
{
    // TcpClient loop 线程回调，投递 api_thread_ 串行处理
    api_thread_->async([this, data]() {
        DecodeData(data);
    });
}

void hrtc::DataStream::DecodeData(const std::string& data)
{
    // 仅 api_thread_ 线程调用
    DataStreamType stream;
    if (!DeserializeStream(data, stream)) {
        NotifyError(HRTC_CODE_ERROR_FAILURE, "deserialize stream failed");
        return;
    }

    switch (static_cast<DataStreamType::Type>(stream.type)) {
    case DataStreamType::Type::JOIN:
        if (stream.param.size() >= 2) {
            const std::string userId = stream.param[1];
            observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                ptr->onUserJoined(userId);
            });
        }
        break;
    case DataStreamType::Type::UNICAST:
        if (stream.param.size() >= 3) {
            const std::string fromId = stream.param[0];
            const std::string toId = stream.param[1];
            const std::string data = stream.param[2];
            observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                ptr->onUnicast(fromId, toId, data);
            });
        }
        break;
    case DataStreamType::Type::BROADCAST:
        if (stream.param.size() >= 2) {
            const std::string fromId = stream.param[0];
            const std::string data = stream.param[1];
            observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                ptr->onBroadcast(fromId, data);
            });
        }
        break;
    case DataStreamType::Type::LEAVE:
        if (stream.param.size() >= 1) {
            const std::string userId = stream.param[0];
            observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                ptr->onUserLeave(userId);
            });
        }
        break;
    case DataStreamType::Type::REQUEST_DATA:
        if (stream.param.size() >= 1) {
            const std::string requesterId = stream.param[0];
            observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                ptr->onRequestData(requesterId);
            });
        }
        break;
    default:
        NotifyError(-1, "unknown stream type");
        break;
    }
}

void hrtc::DataStream::NotifyError(int errorCode, const std::string& error)
{
    observers_.Foreach([&](Collections<DatastreamObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
        ptr->onError(errorCode, error);
    });
}
