#pragma once
#include<string>
#include<thread>
#include "TcpConnection.h"
namespace hrtc{
	class TcpClient : public TcpConnectionObserver {
	public:
		TcpClient();
		~TcpClient();
		void connect(const std::string& ip, int port,int timeout_ms = 0);
		void disconnect();
		bool isConnected() const{ return connected_; }
		void sendMessage(const std::string& message);
	private:
		uv_loop_t* loop_;
		std::thread thread_;
		uv_timer_t timer_req_;
		std::unique_ptr<TcpConnection> connection_;
		bool connected_ = false;
		void run();
		void stop();
		virtual void onDataReceived(const std::string& data) override;
		virtual void onConnectionClosed() override;
		virtual void onConnectionEstablished() override;
	};
}