#include "TcpConnection.h"

using namespace hrtc;

hrtc::TcpConnection::TcpConnection(uv_loop_t* loop)
    : loop_(loop)
    , handle_(std::make_unique<uv_tcp_t>())
    , io_async_(std::make_unique<uv_async_t>())
    , write_req_(std::make_unique<uv_write_t>())
{
    handle_->data = this;
    uv_tcp_init(loop_, handle_.get());

    io_async_->data = this;
    uv_async_init(loop_, io_async_.get(), onIoAsync);
}

hrtc::TcpConnection::~TcpConnection()
{
    // 约定：调用方需先 close() 并等待 on_connection_closed 回调完成后再析构，
    // 否则 loop 仍在运行时回调会访问已析构对象。
    close();
}

int hrtc::TcpConnection::addObserver(TcpConnectionObserver* observer)
{
    return observers_.AddElement(observer);
}

int hrtc::TcpConnection::removeObserver(TcpConnectionObserver* observer)
{
    return observers_.RemoveElement(observer);
}

void hrtc::TcpConnection::connect(const std::string& server_ip, int server_port, int timeout_ms)
{
    {
        std::lock_guard<std::mutex> lock(lock_);
        connect_ip_ = server_ip;
        connect_port_ = server_port;
        connect_timeout_ms_ = timeout_ms;
    }
    bool expected = false;
    if (connect_requested_.compare_exchange_strong(expected, true)) {
        uv_async_send(io_async_.get());
    }
}

void hrtc::TcpConnection::write(const std::string& data)
{
    if (is_closing_.load() || close_requested_.load()) {
        LOG_ERROR("TcpConnection","Connection is closing, drop write.")
        return;
    }
    {
        std::lock_guard<std::mutex> lock(lock_);
        write_queue_.push_back(data);
    }
    bool expected = false;
    if (write_pending_.compare_exchange_strong(expected, true)) {
        uv_async_send(io_async_.get());
    }
}

void hrtc::TcpConnection::close()
{
    // 仅第一次调用会真正发送关闭请求，后续调用直接返回，
    // 保证 uv_async_send 一定发生在 io_async_ 关闭之前。
    bool expected = false;
    if (close_requested_.compare_exchange_strong(expected, true)) {
        uv_async_send(io_async_.get());
    }
}

void hrtc::TcpConnection::accept(uv_stream_t* server)
{
    int r = uv_accept(server, (uv_stream_t*)handle_.get());
    if (r == 0) {
        observers_.Foreach([&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
            ptr->on_connection_connected(this);
        });
        uv_read_start((uv_stream_t*)handle_.get(), allocBuffer, onRead);
    }
    else {
        LOG_ERROR("TcpConnection", "Accept error : " << uv_strerror(r));
        close();
    }
}

void hrtc::TcpConnection::doConnect()
{
    connect_requested_.store(false);
    if (is_closing_.load())
        return;

    std::string ip;
    int port = 0;
    int timeout = 0;
    {
        std::lock_guard<std::mutex> lock(lock_);
        ip = connect_ip_;
        port = connect_port_;
        timeout = connect_timeout_ms_;
    }

    struct sockaddr_in addr;
    if (uv_ip4_addr(ip.c_str(), port, &addr) != 0) {
        LOG_ERROR("TcpConnection","Invalid server address : " << ip << ":" << port);
        close();
        return;
    }

    int r = uv_tcp_connect(&connect_req_, handle_.get(), (const struct sockaddr*)&addr, onConnect);
    if (r < 0) {
        LOG_ERROR("TcpConnection","Connect error: " << uv_strerror(r));
        close();
        return;
    }
    connect_req_.data = this;

    if (timeout > 0) {
        uv_timer_init(loop_, &timer_req_);
        timer_req_.data = this;
        uv_timer_start(&timer_req_, onTimeout, timeout, 0);
        has_timer_.store(true);
    }
}

void hrtc::TcpConnection::doClose()
{
    if (is_closing_.load())
        return;
    is_closing_.store(true);

    if (has_timer_.load()) {
        has_timer_.store(false);
        uv_timer_stop(&timer_req_);
        uv_close((uv_handle_t*)&timer_req_, NULL);
    }

    uv_read_stop((uv_stream_t*)handle_.get());
    if (!uv_is_closing((uv_handle_t*)handle_.get())) {
        uv_close((uv_handle_t*)handle_.get(), onClose);
    }
}

void hrtc::TcpConnection::drainWriteQueue()
{
    if (write_in_progress_ || is_closing_.load())
        return;

    std::string payload;
    {
        std::lock_guard<std::mutex> lock(lock_);
        if (write_queue_.empty()) {
            write_pending_.store(false);
            return;
        }
        payload = std::move(write_queue_.front());
        write_queue_.pop_front();
    }

    // 拼帧头（4 字节大端长度）+ 负载
    const uint32_t len = static_cast<uint32_t>(payload.size());
    framed_buffer_.clear();
    framed_buffer_.reserve(4 + len);
    framed_buffer_.push_back(static_cast<char>((len >> 24) & 0xFF));
    framed_buffer_.push_back(static_cast<char>((len >> 16) & 0xFF));
    framed_buffer_.push_back(static_cast<char>((len >> 8) & 0xFF));
    framed_buffer_.push_back(static_cast<char>(len & 0xFF));
    framed_buffer_.append(payload);

    uv_buf_t buf = uv_buf_init(&framed_buffer_[0], static_cast<unsigned int>(framed_buffer_.size()));
    write_in_progress_ = true;
    write_req_->data = this;
    int r = uv_write(write_req_.get(), (uv_stream_t*)handle_.get(), &buf, 1, onWrite);
    if (r < 0) {
        write_in_progress_ = false;
        LOG_ERROR("TcpConnection","Write error: " << uv_strerror(r));
        close();
    }
}

void hrtc::TcpConnection::onTimeout(uv_timer_t* handle)
{
    // 连接超时
    TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
    connection->close();
}

void hrtc::TcpConnection::onIoAsync(uv_async_t* handle)
{
    TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
    if (connection->close_requested_.load()) {
        connection->doClose();
        return;
    }
    if (connection->connect_requested_.load()) {
        connection->doConnect();
    }
    if (connection->write_pending_.load()) {
        connection->drainWriteQueue();
    }
}

void hrtc::TcpConnection::onAsyncClosed(uv_handle_t* handle)
{
    TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
    connection->async_closed_.store(true);
}

void hrtc::TcpConnection::allocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf)
{
    TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
    connection->read_buffer_.resize(suggested_size > 0 ? suggested_size : 64 * 1024);
    buf->base = connection->read_buffer_.data();
    buf->len = static_cast<ULONG>(connection->read_buffer_.size());
}

void hrtc::TcpConnection::onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    TcpConnection* connection = static_cast<TcpConnection*>(stream->data);
    if (nread > 0) {
        connection->recv_buffer_.append(buf->base, nread);

        // 拆帧：每帧 [4 字节大端长度][负载]
        while (connection->recv_buffer_.size() >= 4) {
            const unsigned char* h =
                reinterpret_cast<const unsigned char*>(connection->recv_buffer_.data());
            uint32_t len = (static_cast<uint32_t>(h[0]) << 24) |
                           (static_cast<uint32_t>(h[1]) << 16) |
                           (static_cast<uint32_t>(h[2]) << 8) |
                           static_cast<uint32_t>(h[3]);
            if (len > kMaxFrameSize) {
                LOG_ERROR("TcpConnection","Frame too large: " << len);
                connection->close();
                return;
            }
            if (connection->recv_buffer_.size() < 4 + static_cast<size_t>(len))
                break; // 半包，等待更多数据

            std::string frame = connection->recv_buffer_.substr(4, len);
            connection->recv_buffer_.erase(0, 4 + len);

            connection->observers_.Foreach(
                [&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                    ptr->on_data_received(connection, frame);
                });

            // 回调中可能已请求关闭，不再继续分发
            if (connection->close_requested_.load())
                return;
        }
    }
    else if (nread == UV_EOF) {
        // 对端关闭
        uv_read_stop(stream);
        connection->close();
    }
    else {
        // 读取错误
        LOG_ERROR("TcpConnection","Read error : " << uv_strerror(static_cast<int>(nread)));
        connection->close();
    }
}

void hrtc::TcpConnection::onWrite(uv_write_t* req, int status)
{
    TcpConnection* connection = static_cast<TcpConnection*>(req->data);
    connection->write_in_progress_ = false;
    if (status < 0) {
        LOG_ERROR("TcpConnection","Write error: " << uv_strerror(status));
        connection->close();
        return;
    }
    connection->drainWriteQueue();
}

void hrtc::TcpConnection::onConnect(uv_connect_t* req, int status)
{
    TcpConnection* connection = static_cast<TcpConnection*>(req->data);

    if (connection->has_timer_.load()) {
        uv_timer_stop(&connection->timer_req_);
        uv_close((uv_handle_t*)&connection->timer_req_, NULL);
        connection->has_timer_.store(false);
    }

    if (status == 0) {
        connection->observers_.Foreach(
            [&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                ptr->on_connection_connected(connection);
            });
        uv_read_start((uv_stream_t*)req->handle, allocBuffer, onRead);
    }
    else {
        LOG_ERROR("TcpConnection","Connect error: " << uv_strerror(status));
        connection->close();
    }
}

void hrtc::TcpConnection::onClose(uv_handle_t* handle)
{
    TcpConnection* connection = static_cast<TcpConnection*>(handle->data);

    // 先关闭 io_async_（其 close 回调在同一轮 run_closing_handles 中执行），
    // 再通知 observer。observer 若异步释放最后一个引用，将在 io_async_ 的
    // close 回调（onAsyncClosed）之后执行，保证连接对象仍然存活。
    if (!uv_is_closing((uv_handle_t*)connection->io_async_.get())) {
        uv_close((uv_handle_t*)connection->io_async_.get(), onAsyncClosed);
    }

    // 清空待写队列，避免残留数据
    {
        std::lock_guard<std::mutex> lock(connection->lock_);
        connection->write_queue_.clear();
    }

    connection->observers_.Foreach(
        [&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
            ptr->on_connection_closed(connection);
        });
}
