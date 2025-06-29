#pragma once
#include <uv.h>
#include <thread>
#include <vector>
#include <memory>
#include "TcpConnection.h"
namespace hrtc {
	
	class TcpServer :public hrtc::TcpConnectionObserver {
	public:
		friend class RemoveConnectionClosed;
		TcpServer();
		~TcpServer();
		void async_run(int port);
		void stop();

		uv_loop_t* get_loop(){ return loop_.get(); }
		int sync_run(int port);
		std::vector<std::shared_ptr<TcpConnection>> connections_;

		virtual void onDataReceived(hrtc::TcpConnection* connection, const std::string& data) override;
		virtual void onConnectionClosed(hrtc::TcpConnection* connection) override;
		virtual void onConnectionEstablished(hrtc::TcpConnection* connection) override;
	private:
		std::unique_ptr<uv_async_t> task_;
		std::unique_ptr<uv_loop_t> loop_;
		int port_;
		std::thread thread_;
		std::unique_ptr<uv_tcp_t> server_;
	};
}