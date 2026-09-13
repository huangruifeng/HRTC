#pragma once
#include <uv.h>
#include <thread>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <map>
#include <functional>
#include "TcpConnection.h"

namespace hrtc {

    class TcpServerObserver {
    public:
        virtual ~TcpServerObserver() = default;
        virtual void on_new_connection(const std::shared_ptr<TcpConnection>& connection) = 0;
    };

    // 基于 libuv 的 TCP 服务端。
    // 线程模型：loop 线程处理所有 I/O；stop/close_connection 可跨线程调用，
    // 内部通过 uv_async 投递（线程安全）。
	class TcpServer : public TcpConnectionObserver {
	public:
		TcpServer();
		~TcpServer();

		void async_run(int port);   // 后台线程运行
		void stop();                // 停止服务（线程安全，幂等）
		int sync_run(int port);     // 阻塞当前线程运行

        uv_loop_t* get_loop() { return loop_.get(); }
        bool is_running() const { return running_.load(); }

        void add_observer(TcpServerObserver* observer) { observers_.AddElement(observer); }
        void remove_observer(TcpServerObserver* observer) { observers_.RemoveElement(observer); }

        void on_new_connection(const std::shared_ptr<TcpConnection>& connection);
        void close_connection(const std::shared_ptr<TcpConnection>& conn);

        // 向 loop 线程投递任务并排队执行（线程安全，FIFO）。
        void Post(std::function<void()> task);

        // TcpConnectionObserver（loop 线程回调）
        void on_data_received(TcpConnection*, const std::string&) override {}
        void on_connection_closed(TcpConnection* conn) override;
        void on_connection_connected(TcpConnection*) override {}

	private:
        void onMgmtAsync();
        static void mgmtAsyncCb(uv_async_t* handle);
        static void onNewConnection(uv_stream_t* server, int status);
        static void onServerClosed(uv_handle_t* handle);
        static void onMgmtClosed(uv_handle_t* handle);

	private:
		std::unique_ptr<uv_loop_t> loop_;
		std::unique_ptr<uv_tcp_t> server_;
		std::unique_ptr<uv_async_t> mgmt_async_;
		std::thread thread_;
        std::atomic<bool> running_{false};
        std::atomic<bool> loop_ready_{false};

        std::mutex mgmt_mutex_;
        // 以下成员由 mgmt_mutex_ 保护
        std::map<TcpConnection*, std::shared_ptr<TcpConnection>> connections_;
        std::vector<std::shared_ptr<TcpConnection>> pending_removes_;  // 延迟到 mgmt 回调释放
        std::vector<std::function<void()>> pending_tasks_;             // Post 投递的任务
        bool stop_requested_ = false;
        bool stop_closing_conns_ = false;
        bool server_closed_ = false;

        hrtc::Collections<TcpServerObserver, hrtc::RawPointer, hrtc::MultiThreaded> observers_;
	};
}
