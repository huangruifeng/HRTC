#include "TcpServer.h"
#include <iostream>
#include <condition_variable>

struct LoopTask {
	virtual ~LoopTask() = default;
	virtual void run() = 0;
};

struct FreeConnectionTask:public LoopTask {
	FreeConnectionTask(hrtc::TcpServer* s, const std::shared_ptr<hrtc::TcpConnection>& c) :server(s),connection(c) {}
	virtual void run() override {
        if (server) {
            server->close_connection(connection);
        }
	}
private:
    hrtc::TcpServer* server;
    std::shared_ptr<hrtc::TcpConnection> connection;
};

void on_async(uv_async_t* handle) {
	LoopTask* data = (LoopTask*)handle->data;
	data->run();
	delete data;
}


void hrtc::TcpServer::on_new_connection(const std::shared_ptr<TcpConnection>& connection)
{
    connections_.AddElement(connection);
    observers_.Foreach([&](auto ptr) {
        ptr->on_new_connection(connection);
    });
}

void hrtc::TcpServer::close_connection(const std::shared_ptr<TcpConnection>& conn)
{
    FreeConnectionTask* task = new FreeConnectionTask(this,conn);
    task_->data = task;
    uv_async_send(task_.get());
}

void on_new_connection(uv_stream_t* server, int status) {
	if (status < 0) {
		return;
	}
	hrtc::TcpServer* tcp = (hrtc::TcpServer*)server->data;
	std::shared_ptr<hrtc::TcpConnection> connection = std::make_shared<hrtc::TcpConnection>(tcp->get_loop());
    tcp->on_new_connection(connection);
	connection->accept(server);
}

hrtc::TcpServer::TcpServer():task_(new uv_async_t),loop_(new uv_loop_t),port_(0), server_(new uv_tcp_t)
{
}

hrtc::TcpServer::~TcpServer()
{
	uv_loop_close(loop_.get());
}

void hrtc::TcpServer::async_run(int port)
{
	thread_ = std::thread([this,port](){
		sync_run(port);
	});
}

void hrtc::TcpServer::stop()
{
    uv_stop(loop_.get());
	uv_close((uv_handle_t*)server_.get(), NULL);
    uv_close((uv_handle_t*)task_.get(),NULL);
	if (thread_.joinable()) {
		thread_.join();
	}
}

int hrtc::TcpServer::sync_run(int port)
{
	uv_loop_init(loop_.get());
	loop_->data = this;
	uv_tcp_init(loop_.get(), server_.get());
	struct sockaddr_in addr_;
	server_->data = this;

	uv_async_init(loop_.get(), task_.get(), on_async);

	port_ = port;
	uv_ip4_addr("0.0.0.0", port_, &addr_);
	if (uv_tcp_bind(server_.get(), (const struct sockaddr*)&addr_, 0) < 0) {
		std::cout << "Bind error" << std::endl;
		return -1;
	}
	if(uv_listen((uv_stream_t*)server_.get(), 128, ::on_new_connection)< 0)
	{
		std::cout << "Listen error "  << std::endl;
		return -1;
	}
	int ret = uv_run(loop_.get(), UV_RUN_DEFAULT);
	return ret;
}
