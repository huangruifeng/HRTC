#pragma once
#include <string>
#include <thread>
#include <memory>
#include <atomic>
#include <cstdint>
#include <functional>
#include <deque>
#include <mutex>
#include <vector>
#include <Base/Collections.h>
#include "TcpConnection.h"
#include "Transport/heartbeat/Heartbeat.h"

namespace hrtc{

    class TcpClientObserver {
    public:
        virtual ~TcpClientObserver() = default;
        virtual void on_connected() = 0;
        virtual void on_disconnected() = 0;
        // 收到一帧业务数据（PING/PONG 已内部消化）
        virtual void on_message(const std::string& data) = 0;
    };

    // 基于 libuv 的 TCP 客户端。
    // 线程模型：仅一个 loop 线程（I/O、心跳、回调与 connection_ 生命周期）；
    // API（connect/disconnect/sendMessage）可跨线程调用：
    //   connect   用 state_ CAS 防并发，连接对象在 loop 线程创建；
    //   sendMessage/disconnect 经 conn_mutex_ 取 shared_ptr 快照后调用
    //   TcpConnection 的线程安全接口（write/close）。
    // 心跳：连接成功后每 pingIntervalMs 发 PING，超过 timeoutMs 未收到
    // PONG 判定断线并触发 on_disconnected。
	class TcpClient : public TcpConnectionObserver {
	public:
        enum class ConnectionState {
            Disconnected = 0,   // 未连接
            Connecting = 3,     // 连接中
            Connected = 4,      // 已连接
        };

        explicit TcpClient(const HeartbeatConfig& heartbeat = HeartbeatConfig());
		~TcpClient();

		void connect(const std::string& ip, int port, int timeout_ms = 0);
		void disconnect();
		bool isConnected() const { return state_ == ConnectionState::Connected; }
		int sendMessage(const std::string& message);

        void add_observer(TcpClientObserver* observer) { observers_.AddElement(observer); }
        void remove_observer(TcpClientObserver* observer) { observers_.RemoveElement(observer); }

        // TcpConnectionObserver（loop 线程回调）
        virtual void on_data_received(TcpConnection*, const std::string& data) override;
        virtual void on_connection_closed(TcpConnection*) override;
        virtual void on_connection_connected(TcpConnection*) override;

	private:
        // 将任务投递到 loop 线程执行（任意线程可调用）。
        void Post(std::function<void()> task);
        void startHeartbeat();      // loop 线程
        void stopHeartbeat();       // loop 线程
        void closeQuit();           // loop 线程
        static void onHeartbeatTimer(uv_timer_t* handle);
        static void onQuitAsync(uv_async_t* handle);
        static void onMgmtAsync(uv_async_t* handle);
        static void onQuitClosed(uv_handle_t* handle);
        static void onMgmtClosed(uv_handle_t* handle);
        static void onTimerClosed(uv_handle_t* handle);

	private:
		uv_loop_t* loop_;
        std::thread loop_thread_;
        std::unique_ptr<uv_async_t> quit_async_;
        std::unique_ptr<uv_async_t> mgmt_async_;

        // loop 任务队列（mgmt_mutex_ 保护，onMgmtAsync 在 loop 线程消费）
        std::mutex mgmt_mutex_;
        std::deque<std::function<void()>> mgmt_tasks_;

        // connection_ 由 loop 线程创建/销毁；其他线程经 conn_mutex_ 取快照。
        std::mutex conn_mutex_;
        std::shared_ptr<TcpConnection> connection_;
        // 关闭的连接延迟到 loop 下一轮释放（仅 loop 线程访问）
        std::vector<std::shared_ptr<TcpConnection>> pending_connections_;

        uv_timer_t timer_req_;
        std::atomic<ConnectionState> state_{ ConnectionState::Disconnected };
        std::atomic<int64_t> last_pong_ms_{ 0 };
        std::atomic<bool> timer_active_{ false };
        std::atomic<bool> timer_closed_{ true };
        std::atomic<bool> quit_requested_{ false };

        HeartbeatConfig heartbeat_;
        hrtc::Collections<TcpClientObserver, hrtc::RawPointer, hrtc::MultiThreaded> observers_;
	};
}
