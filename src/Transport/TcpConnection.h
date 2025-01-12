#pragma once
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <uv.h>
#include <Base/Collections.h>
#include <Base/base.h>

namespace hrtc{
    class TcpConnectionObserver {
    public:
        virtual ~TcpConnectionObserver() = default;
        virtual void onDataReceived(const std::string& data) = 0;
        virtual void onConnectionClosed() = 0;
        virtual void onConnectionEstablished() = 0;
    };

	class TcpConnection {
    public:
        TcpConnection(uv_loop_t* loop)
            : loop_(loop), handle_(std::make_unique<uv_tcp_t>()), write_req_(std::make_unique<uv_write_t>()),
            read_buffer_(nullptr), read_buffer_size_(0), is_closing_(false) {
            uv_tcp_init(loop_, handle_.get());
            uv_read_start((uv_stream_t*)handle_.get(), allocBuffer, onRead);
            handle_->data = this;
        }

        int addObserver(TcpConnectionObserver* observer) {
            return observers_.AddElement(observer);
        }

        int removeObserver(TcpConnectionObserver* observer) {
            return observers_.RemoveElement(observer);
        }

        ~TcpConnection() {
            close();
        }

        void connect(const std::string& server_ip, int server_port, int timeout_ms = 0) {
            uv_ip4_addr(server_ip.c_str(), server_port, &peer_addr_);
            uv_tcp_connect(&connect_req_, handle_.get(), (const struct sockaddr*)&peer_addr_, onConnect);
            connect_req_.data = this;
            has_timer_ = false;
            if (timeout_ms > 0) {
                uv_timer_init(loop_, &timer_req_);
                timer_req_.data = this;
                uv_timer_start(&timer_req_, onTimeout, timeout_ms, 0);
                has_timer_ = true;
            }
        }

        void accept(uv_stream_t* server) {
            int r = uv_accept(server, (uv_stream_t*)handle_.get());
            if (r == 0) {
                observers_.Foreach([&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                    ptr->onConnectionEstablished();
                });
            }
            else {
                LOG_ERROR("TcpConnection", "Accept error : " << uv_strerror(r));
                close();
            }
        }


        void write(const std::string& data) {
            if (is_closing_) {
                LOG_ERROR("TcpConnection","Connection is closing, cannot write data.")
            }
            std::lock_guard<std::mutex> lock(lock_);
            write_queue_.push_back(data);
            processWriteQueue();
        }

        void close() {
            if (has_timer_) {
                has_timer_ = false;
                uv_timer_stop(&timer_req_);
                uv_close((uv_handle_t*)&timer_req_, NULL);
            }

            if (!is_closing_) {
                is_closing_ = true;
                uv_close((uv_handle_t*)handle_.get(), onClose);
            }
        }
    private:

        static void onTimeout(uv_timer_t* handle) {
            // 连接超时
            TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
            connection->close();
        }

        static void allocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf) {
            TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
            if (connection->read_buffer_) {
                delete[] connection->read_buffer_;
            }
            connection->read_buffer_ = new char[suggested_size];
            connection->read_buffer_size_ = suggested_size;
            buf->base = connection->read_buffer_;
            buf->len = connection->read_buffer_size_;
        }

        static void onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
            TcpConnection* connection = static_cast<TcpConnection*>(stream->data);
            if (nread > 0) {
                connection->observers_.Foreach(
                    [&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                        ptr->onDataReceived(std::string(buf->base, nread));
                    });
            }
            else if (nread == UV_EOF) {
                // 对端关闭连接
                connection->close();
            }
            else {
                // 读取错误
                LOG_ERROR("TcpConnection","Read error : " << uv_strerror(nread))
                connection->close();
            }
            delete[] buf->base;
        }

        static void onWrite(uv_write_t* req, int status) {
            TcpConnection* connection = static_cast<TcpConnection*>(req->data);
            if (status >= 0) {
                std::lock_guard<std::mutex> lock(connection->lock_);
                connection->write_queue_.pop_front();
                connection->write_in_progress_ = false;
                connection->processWriteQueue();
            }
            else {
                LOG_ERROR("TcpConnection","Write error: " << uv_strerror(status));
                connection->close();
            }
        }

        static void onConnect(uv_connect_t* req, int status) {
            TcpConnection* connection = static_cast<TcpConnection*>(req->data);
            if (status == 0) {
                connection->observers_.Foreach(
                    [&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                        ptr->onConnectionEstablished();
                    });
            }
            else {
                LOG_ERROR("TcpConnection","Connect error: " << uv_strerror(status));
                connection->close();
            }
            if (connection->has_timer_) {
                uv_timer_stop(&connection->timer_req_);
                uv_close((uv_handle_t*)&connection->timer_req_, NULL);
                connection->has_timer_ = false;
            }
        }

        static void onClose(uv_handle_t* handle) {
            TcpConnection* connection = static_cast<TcpConnection*>(handle->data);
            connection->observers_.Foreach(
                [&](Collections<TcpConnectionObserver, RawPointer, hrtc::MultiThreaded>::PtrType ptr) {
                    ptr->onConnectionClosed();
                });
            delete[] connection->read_buffer_;
            // 注意：不要在这里删除connection，因为它可能是由shared_ptr管理的
        }

        void processWriteQueue() {
            if (!write_queue_.empty() && !write_in_progress_) {
                std::string data = std::move(write_queue_.front());
                uv_buf_t buf = uv_buf_init(const_cast<char*>(data.c_str()), data.size());
                uv_write(write_req_.get(), (uv_stream_t*)handle_.get(), &buf, 1, onWrite);
                write_req_->data = this;
                write_in_progress_ = true;
            }
        }

    private:
        uv_loop_t* loop_;
        std::unique_ptr<uv_tcp_t> handle_;
        std::unique_ptr<uv_write_t> write_req_;
        std::unique_ptr<uv_read_t> read_req_;
        uv_connect_t connect_req_;
        uv_timer_t timer_req_;
        std::atomic<bool> has_timer_;;
        struct sockaddr_in peer_addr_;
        char* read_buffer_;
        size_t read_buffer_size_;
        hrtc::Collections<TcpConnectionObserver,hrtc::RawPointer,hrtc::MultiThreaded> observers_;
        std::mutex lock_;
        std::deque<std::string> write_queue_;
        std::atomic<bool> is_closing_;
        std::atomic<bool> write_in_progress_;
	};
}