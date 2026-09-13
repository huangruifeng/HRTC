#include "TcpServer.h"
#include <Base/base.h>

using namespace hrtc;

hrtc::TcpServer::TcpServer()
    : loop_(new uv_loop_t)
    , server_(new uv_tcp_t)
    , mgmt_async_(new uv_async_t)
{
}

hrtc::TcpServer::~TcpServer()
{
    stop();
    uv_loop_close(loop_.get());
}

void hrtc::TcpServer::async_run(int port)
{
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        if (running_.load() || stop_requested_)
            return;
    }
    thread_ = std::thread([this, port]() {
        sync_run(port);
    });
}

void hrtc::TcpServer::stop()
{
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        if (stop_requested_)
            return;
        stop_requested_ = true;
        if (loop_ready_.load()) {
            uv_async_send(mgmt_async_.get());
        }
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

int hrtc::TcpServer::sync_run(int port)
{
    uv_loop_init(loop_.get());
    loop_->data = this;

    uv_tcp_init(loop_.get(), server_.get());
    server_->data = this;

    uv_async_init(loop_.get(), mgmt_async_.get(), mgmtAsyncCb);
    mgmt_async_->data = this;

    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        loop_ready_.store(true);
        running_.store(true);
        // stop() 可能在 init 完成前调用，此时补发管理回调
        if (stop_requested_) {
            uv_async_send(mgmt_async_.get());
        }
    }

    struct sockaddr_in addr_;
    uv_ip4_addr("0.0.0.0", port, &addr_);
    if (uv_tcp_bind(server_.get(), (const struct sockaddr*)&addr_, 0) < 0) {
        LOG_ERROR("TcpServer", "Bind error, port: " << port);
        // 关闭句柄并跑一轮 loop 让 close 回调完成
        uv_close((uv_handle_t*)server_.get(), NULL);
        uv_close((uv_handle_t*)mgmt_async_.get(), NULL);
        uv_run(loop_.get(), UV_RUN_DEFAULT);
        running_.store(false);
        return -1;
    }
    if (uv_listen((uv_stream_t*)server_.get(), 128, onNewConnection) < 0) {
        LOG_ERROR("TcpServer", "Listen error, port: " << port);
        uv_close((uv_handle_t*)server_.get(), NULL);
        uv_close((uv_handle_t*)mgmt_async_.get(), NULL);
        uv_run(loop_.get(), UV_RUN_DEFAULT);
        running_.store(false);
        return -1;
    }

    int ret = uv_run(loop_.get(), UV_RUN_DEFAULT);
    running_.store(false);
    return ret;
}

void hrtc::TcpServer::on_new_connection(const std::shared_ptr<TcpConnection>& connection)
{
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        connections_.emplace(connection.get(), connection);
    }
    connection->addObserver(this);
    observers_.Foreach([&](auto ptr) {
        ptr->on_new_connection(connection);
    });
}

void hrtc::TcpServer::close_connection(const std::shared_ptr<TcpConnection>& conn)
{
    if (conn) {
        conn->close();
    }
}

void hrtc::TcpServer::Post(std::function<void()> task)
{
    if (!task)
        return;
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        pending_tasks_.push_back(std::move(task));
        if (loop_ready_.load()) {
            uv_async_send(mgmt_async_.get());
        }
    }
}

void hrtc::TcpServer::on_connection_closed(TcpConnection* conn)
{
    // loop 线程（close 回调）。连接对象引用延迟到 mgmt 回调释放，
    // 保证 TcpConnection 的 async 句柄 close 回调先执行完毕。
    std::lock_guard<std::mutex> lock(mgmt_mutex_);
    auto it = connections_.find(conn);
    if (it != connections_.end()) {
        pending_removes_.push_back(it->second);
        connections_.erase(it);
        uv_async_send(mgmt_async_.get());
    }
}

void hrtc::TcpServer::onMgmtAsync()
{
    // loop 线程
    // 1. 先执行 Post 投递的任务（锁外执行，避免任务内调用 Post/close 造成死锁）
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(mgmt_mutex_);
        tasks.swap(pending_tasks_);
    }
    for (auto& task : tasks) {
        if (task)
            task();
    }

    std::lock_guard<std::mutex> lock(mgmt_mutex_);

    pending_removes_.clear();   // 此时连接的 async close 回调已完成，可安全释放

    if (stop_requested_) {
        if (connections_.empty()) {
            if (!server_closed_) {
                server_closed_ = true;
                if (!uv_is_closing((uv_handle_t*)server_.get())) {
                    uv_close((uv_handle_t*)server_.get(), onServerClosed);
                }
            }
        }
        else if (!stop_closing_conns_) {
            stop_closing_conns_ = true;
            for (auto& kv : connections_) {
                kv.second->close();
            }
        }
    }
}

void hrtc::TcpServer::mgmtAsyncCb(uv_async_t* handle)
{
    TcpServer* server = static_cast<TcpServer*>(handle->data);
    server->onMgmtAsync();
}

void hrtc::TcpServer::onNewConnection(uv_stream_t* server, int status)
{
    if (status < 0) {
        return;
    }
    TcpServer* srv = static_cast<TcpServer*>(server->data);
    std::shared_ptr<TcpConnection> connection = std::make_shared<TcpConnection>(srv->get_loop());
    srv->on_new_connection(connection);
    connection->accept(server);
}

void hrtc::TcpServer::onServerClosed(uv_handle_t* handle)
{
    TcpServer* server = static_cast<TcpServer*>(handle->data);
    if (!uv_is_closing((uv_handle_t*)server->mgmt_async_.get())) {
        uv_close((uv_handle_t*)server->mgmt_async_.get(), onMgmtClosed);
    }
}

void hrtc::TcpServer::onMgmtClosed(uv_handle_t* handle)
{
    TcpServer* server = static_cast<TcpServer*>(handle->data);
    uv_stop(server->loop_.get());
}
