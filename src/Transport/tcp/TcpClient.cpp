#include "TcpClient.h"
#include "Transport/common/CommonType.h"
#include <Base/base.h>

using namespace hrtc;

hrtc::TcpClient::TcpClient(const HeartbeatConfig& heartbeat)
    : loop_(new uv_loop_t)
    , heartbeat_(heartbeat)
{
    uv_loop_init(loop_);
    quit_async_ = std::make_unique<uv_async_t>();
    quit_async_->data = this;
    uv_async_init(loop_, quit_async_.get(), onQuitAsync);

    mgmt_async_ = std::make_unique<uv_async_t>();
    mgmt_async_->data = this;
    uv_async_init(loop_, mgmt_async_.get(), onMgmtAsync);

    loop_thread_ = std::thread([this]() {
        uv_run(loop_, UV_RUN_DEFAULT);
    });
}

hrtc::TcpClient::~TcpClient()
{
    // 通知 loop 线程退出：关闭心跳与连接，最后关闭 async 句柄并 uv_stop。
    quit_requested_.store(true);
    uv_async_send(quit_async_.get());

    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }

    // loop 已停止，以下清理均为单线程
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        connection_.reset();
    }
    pending_connections_.clear();
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        mgmt_tasks_.clear();
    }

    uv_loop_close(loop_);
    delete loop_;
}

void hrtc::TcpClient::connect(const std::string& ip, int port, int timeout_ms)
{
    if (quit_requested_.load())
        return;

    // CAS 防并发：仅当处于未连接状态才发起连接
    ConnectionState expected = ConnectionState::Disconnected;
    if (!state_.compare_exchange_strong(expected, ConnectionState::Connecting))
        return;

    // 连接对象在 loop 线程创建（uv_tcp_init 需在 loop 线程）
    Post([this, ip, port, timeout_ms]() {
        if (quit_requested_.load() || state_ != ConnectionState::Connecting)
            return;
        auto conn = std::make_shared<TcpConnection>(loop_);
        conn->addObserver(this);
        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            if (connection_) {
                // 旧连接可能仍在关闭流程中，移入 pending 保活，
                // 待其 close 回调完成后延迟释放。
                pending_connections_.push_back(std::move(connection_));
            }
            connection_ = conn;
        }
        conn->connect(ip, port, timeout_ms);
    });
}

void hrtc::TcpClient::disconnect()
{
    if (quit_requested_.load())
        return;

    std::shared_ptr<TcpConnection> conn;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        state_ = ConnectionState::Disconnected;
        conn = connection_;
    }
    if (conn) {
        // close 线程安全，无锁调用
        conn->close();
    }
}

int hrtc::TcpClient::sendMessage(const std::string& message)
{
    if (state_ != ConnectionState::Connected)
        return -1;

    std::shared_ptr<TcpConnection> conn;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        conn = connection_;
    }
    if (!conn)
        return -1;

    // write 线程安全；快照保证对象在发送期间存活
    conn->write(message);
    return 0;
}

void hrtc::TcpClient::Post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        mgmt_tasks_.push_back(std::move(task));
    }
    // 退出流程中 loop 即将停止，任务将不会执行（析构时清理）
    if (!quit_requested_.load()) {
        uv_async_send(mgmt_async_.get());
    }
}

void hrtc::TcpClient::onMgmtAsync(uv_async_t* handle)
{
    // loop 线程
    TcpClient* client = static_cast<TcpClient*>(handle->data);
    std::deque<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(client->mgmt_mutex_);
        tasks.swap(client->mgmt_tasks_);
    }
    for (auto& task : tasks) {
        task();
    }
}

void hrtc::TcpClient::startHeartbeat()
{
    // loop 线程
    if (timer_active_.load())
        return;
    if (!timer_closed_.load()) {
        // 句柄未被关闭，可直接重新启动
        uv_timer_start(&timer_req_, onHeartbeatTimer, heartbeat_.pingIntervalMs, heartbeat_.pingIntervalMs);
        timer_active_.store(true);
        return;
    }
    uv_timer_init(loop_, &timer_req_);
    timer_req_.data = this;
    uv_timer_start(&timer_req_, onHeartbeatTimer, heartbeat_.pingIntervalMs, heartbeat_.pingIntervalMs);
    timer_closed_.store(false);
    timer_active_.store(true);
}

void hrtc::TcpClient::stopHeartbeat()
{
    // loop 线程
    if (!timer_active_.load())
        return;
    timer_active_.store(false);
    uv_timer_stop(&timer_req_);
    uv_close((uv_handle_t*)&timer_req_, onTimerClosed);
}

void hrtc::TcpClient::onHeartbeatTimer(uv_timer_t* handle)
{
    TcpClient* client = static_cast<TcpClient*>(handle->data);
    if (client->state_ != ConnectionState::Connected)
        return;

    const int64_t now = NowMs();
    if (now - client->last_pong_ms_.load() > client->heartbeat_.timeoutMs) {
        LOG_WARNING("TcpClient", "Heartbeat timeout, close connection.");
        std::shared_ptr<TcpConnection> conn;
        {
            std::lock_guard<std::mutex> lock(client->conn_mutex_);
            conn = client->connection_;
        }
        if (conn) {
            conn->close();
        }
        return;
    }

    DataStreamType ping;
    ping.type = static_cast<int>(DataStreamType::Type::PING);
    std::shared_ptr<TcpConnection> conn;
    {
        std::lock_guard<std::mutex> lock(client->conn_mutex_);
        conn = client->connection_;
    }
    if (conn) {
        conn->write(SerializeStream(ping));
    }
}

void hrtc::TcpClient::on_data_received(TcpConnection*, const std::string& data)
{
    DataStreamType stream;
    if (DeserializeStream(data, stream)) {
        if (stream.type == static_cast<int>(DataStreamType::Type::PONG)) {
            last_pong_ms_.store(NowMs());
            return;
        }
    }
    observers_.Foreach([&](Collections<TcpClientObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
        ptr->on_message(data);
    });
}

void hrtc::TcpClient::on_connection_connected(TcpConnection* conn)
{
    // loop 线程
    if (state_ != ConnectionState::Connecting) {
        // 连接建立前用户已断开
        conn->close();
        return;
    }
    state_ = ConnectionState::Connected;
    last_pong_ms_.store(NowMs());
    startHeartbeat();
    observers_.Foreach([&](Collections<TcpClientObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
        ptr->on_connected();
    });
}

void hrtc::TcpClient::on_connection_closed(TcpConnection* conn)
{
    // loop 线程
    bool is_current = false;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        is_current = (connection_.get() == conn);
        if (is_current) {
            // 延迟到 loop 下一轮释放（此时 TcpConnection 的 io_async_ 关闭回调已完成），
            // 不能在回调中同步释放最后一个引用。
            pending_connections_.push_back(std::move(connection_));
            connection_.reset();
        }
    }

    // 仅当前连接的关闭才通知上层并更新状态（旧连接的迟来回调不影响重连）
    if (is_current) {
        stopHeartbeat();
        state_ = ConnectionState::Disconnected;

        observers_.Foreach([&](Collections<TcpClientObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
            ptr->on_disconnected();
        });
    }

    // 无条件投递清理（含旧连接残留），下一轮 loop 执行时本连接的关闭回调已完成
    Post([this]() {
        pending_connections_.clear();
    });

    if (quit_requested_.load()) {
        closeQuit();
    }
}

void hrtc::TcpClient::onQuitAsync(uv_async_t* handle)
{
    // loop 线程
    TcpClient* client = static_cast<TcpClient*>(handle->data);
    client->stopHeartbeat();
    std::shared_ptr<TcpConnection> conn;
    {
        std::lock_guard<std::mutex> lock(client->conn_mutex_);
        conn = client->connection_;
    }
    if (conn) {
        conn->close();
    }
    else {
        client->closeQuit();
    }
}

void hrtc::TcpClient::closeQuit()
{
    // loop 线程：先提交 mgmt 的关闭，再提交 quit 的关闭，
    // 保证 onQuitClosed（uv_stop）时所有句柄已关闭。
    if (!uv_is_closing((uv_handle_t*)mgmt_async_.get())) {
        uv_close((uv_handle_t*)mgmt_async_.get(), onMgmtClosed);
    }
    if (!uv_is_closing((uv_handle_t*)quit_async_.get())) {
        uv_close((uv_handle_t*)quit_async_.get(), onQuitClosed);
    }
}

void hrtc::TcpClient::onQuitClosed(uv_handle_t* handle)
{
    TcpClient* client = static_cast<TcpClient*>(handle->data);
    uv_stop(client->loop_);
}

void hrtc::TcpClient::onMgmtClosed(uv_handle_t*)
{
}

void hrtc::TcpClient::onTimerClosed(uv_handle_t* handle)
{
    TcpClient* client = static_cast<TcpClient*>(handle->data);
    client->timer_closed_.store(true);
}
