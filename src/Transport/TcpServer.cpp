#include "TcpServer.h"
#include <iostream>
#include <condition_variable>

struct LoopTask {
	virtual ~LoopTask() = default;
	virtual void Run() = 0;
};

struct FreeConnectionTask:public LoopTask {
	FreeConnectionTask(const std::shared_ptr<hrtc::TcpConnection>& c) :connection(c) {}
	virtual void Run() override {
		connection = nullptr;
	}
private:
	std::shared_ptr<hrtc::TcpConnection> connection;
};

void on_async(uv_async_t* handle) {
	LoopTask* data = (LoopTask*)handle->data;
	data->Run();
	delete data;
}

void hrtc::TcpServer:: onDataReceived(hrtc::TcpConnection*connection,const std::string& data) {
	std::cout << "client: " << data << std::endl;
	connection->write("HTTP/1.0 200 OK\r\n\r\n\r\n<html><body>Hello world</body></html>");
    connection->close();
}
void hrtc::TcpServer::onConnectionClosed(hrtc::TcpConnection* connection) {
	std::cout << "connection closed" << std::endl;
	for (auto it = connections_.begin(); it != connections_.end(); ++it) {
		if ((*it).get() == connection) {
			FreeConnectionTask* task = new FreeConnectionTask(*it);
			task_->data = task;
			uv_async_send(task_.get());
			connections_.erase(it);
			break;
		}
	}
}
void hrtc::TcpServer:: onConnectionEstablished(hrtc::TcpConnection* connection) {
	std::cout << "connection established" << std::endl;
}

void onNewConnection(uv_stream_t* server, int status) {
	if (status < 0) {
		return;
	}
	hrtc::TcpServer* tcp = (hrtc::TcpServer*)server->data;
	std::shared_ptr<hrtc::TcpConnection> connection = std::make_shared<hrtc::TcpConnection>(tcp->get_loop());
	connection->addObserver(tcp);
	connection->accept(server);
	tcp->connections_.push_back(connection);
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
	if(uv_listen((uv_stream_t*)server_.get(), 128, onNewConnection)< 0)
	{
		std::cout << "Listen error "  << std::endl;
		return -1;
	}
	int ret = uv_run(loop_.get(), UV_RUN_DEFAULT);
	return ret;
}
