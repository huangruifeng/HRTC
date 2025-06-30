#pragma once
#include<string>
#include<thread>
#include "Base/Thread/DispatchQueue.h"
#include "TcpConnection.h"
namespace hrtc{
	class TcpClient : public TcpConnectionObserver {
	public:
        enum class ConnectionState {
            Disconnected =0 ,     // 未连接
            Connecting = 3,       // 正在连接
            Connected = 4,        // 已建立连接
        };
		TcpClient();
		~TcpClient();
		void connect(const std::string& ip, int port,int timeout_ms = 0);
		void disconnect();
		bool isConnected() const{ return state_ == ConnectionState::Connected; }
		int sendMessage(const std::string& message);
	private:
		uv_loop_t* loop_;
        std::thread loop_thread_;
        std::shared_ptr<dispatch_task_queue::task_queue> api_thread_;
		uv_timer_t timer_req_;
		std::unique_ptr<TcpConnection> connection_;
        std::atomic<ConnectionState> state_{ ConnectionState::Disconnected };


        void run();
		void stop();
		virtual void onDataReceived(TcpConnection*,const std::string& data) override;
		virtual void onConnectionClosed(TcpConnection*) override;
		virtual void onConnectionEstablished(TcpConnection*) override;
	};
}