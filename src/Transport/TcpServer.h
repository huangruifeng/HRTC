#pragma once
#include <uv.h>
#include <thread>
#include <vector>
#include <memory>
#include "TcpConnection.h"
namespace hrtc {
	
    class TcpServerObserver {
    public:
        virtual ~TcpServerObserver() = default;
        virtual void on_new_connection(const std::shared_ptr<TcpConnection>& connection) = 0;
    };

	class TcpServer {
	public:
		TcpServer();
		~TcpServer();
		void async_run(int port);
		void stop();
		int sync_run(int port);

        uv_loop_t* get_loop() { return loop_.get(); }

        void add_observer(TcpServerObserver* observer) { observers_.AddElement(observer); }
        void remove_observer(TcpServerObserver* observer) { observers_.RemoveElement(observer); }

        void on_new_connection(const std::shared_ptr<TcpConnection>& connection);
        void close_connection(const std::shared_ptr<TcpConnection>& conn);

	private:
		std::unique_ptr<uv_async_t> task_;
		std::unique_ptr<uv_loop_t> loop_;
		int port_;
		std::thread thread_;
		std::unique_ptr<uv_tcp_t> server_;
        hrtc::Collections<TcpConnection> connections_;
        hrtc::Collections<TcpServerObserver, hrtc::RawPointer, hrtc::MultiThreaded> observers_;
	};
}