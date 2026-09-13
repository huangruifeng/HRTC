#pragma once
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <uv.h>
#include <Base/Collections.h>
#include <Base/base.h>

namespace hrtc{

    class TcpConnection;

    class TcpConnectionObserver {
    public:
        virtual ~TcpConnectionObserver() = default;
        // 收到一帧完整数据（TcpConnection 内部已完成 TCP 分帧）。
        virtual void on_data_received(TcpConnection*,const std::string& data) = 0;
        // 连接已彻底关闭（tcp 句柄 close 回调中触发）。
        // 注意：回调中不得同步释放对该连接对象的最后一个引用，
        // 应异步投递释放（参见 TcpServer/TcpClient 的用法）。
        virtual void on_connection_closed(TcpConnection*) = 0;
        // 客户端连接成功 / 服务端 accept 成功。
        virtual void on_connection_connected(TcpConnection*) = 0;
    };

    // 基于 libuv 的 TCP 连接封装。
    // 线程模型：所有 uv 句柄操作仅在 loop 线程执行；write/close/connect 可跨线程
    // 调用，内部通过 uv_async 投递到 loop 线程（线程安全）。
    // 帧协议：每帧 = [4 字节大端长度][负载]，最大帧 8MB。
	class TcpConnection {
    public:
        explicit TcpConnection(uv_loop_t* loop);
        ~TcpConnection();

        int addObserver(TcpConnectionObserver* observer);
        int removeObserver(TcpConnectionObserver* observer);

        // 主动连接（可跨线程调用）。超时时间为 0 表示不设置超时。
        void connect(const std::string& server_ip, int server_port, int timeout_ms = 0);

        // 发送一帧数据（可跨线程调用），自动拼接帧头。
        void write(const std::string& data);

        // 关闭连接（可跨线程调用，幂等）。
        void close();

        bool isClosing() const { return is_closing_.load(); }

        // 服务端专用：必须在 loop 线程调用（由 TcpServer 的 accept 回调触发）。
        void accept(uv_stream_t* server);

    private:
        // 以下均在 loop 线程执行
        void doConnect();
        void doClose();
        void drainWriteQueue();

        static void onTimeout(uv_timer_t* handle);
        static void onIoAsync(uv_async_t* handle);
        static void onAsyncClosed(uv_handle_t* handle);
        static void allocBuffer(uv_handle_t* handle, size_t suggested_size, uv_buf_t* buf);
        static void onRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
        static void onWrite(uv_write_t* req, int status);
        static void onConnect(uv_connect_t* req, int status);
        static void onClose(uv_handle_t* handle);

    private:
        static const uint32_t kMaxFrameSize = 8 * 1024 * 1024;

        uv_loop_t* loop_;
        std::unique_ptr<uv_tcp_t> handle_;
        std::unique_ptr<uv_async_t> io_async_;
        std::unique_ptr<uv_write_t> write_req_;
        uv_connect_t connect_req_;
        uv_timer_t timer_req_;

        std::atomic<bool> has_timer_{false};
        std::atomic<bool> connect_requested_{false}; 
        std::atomic<bool> write_pending_{false};
        std::atomic<bool> close_requested_{false};
        std::atomic<bool> is_closing_{false};
        std::atomic<bool> async_closed_{false};
        bool write_in_progress_ = false;   // 仅 loop 线程访问

        // 连接参数（lock_ 保护，doConnect 在 loop 线程读取）
        std::string connect_ip_;
        int connect_port_ = 0;
        int connect_timeout_ms_ = 0;

        std::vector<char> read_buffer_;     // libuv 读缓冲（loop 线程）
        std::string recv_buffer_;           // 拆帧缓冲（loop 线程）
        std::string framed_buffer_;         // 帧头+负载（loop 线程，uv_write 期间有效）

        hrtc::Collections<TcpConnectionObserver,hrtc::RawPointer,hrtc::MultiThreaded> observers_;
        std::mutex lock_;                   // 保护 write_queue_ 与连接参数
        std::deque<std::string> write_queue_;
	};
}
