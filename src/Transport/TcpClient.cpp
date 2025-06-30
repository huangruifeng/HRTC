#include "TcpClient.h"
#include <iostream>
using namespace hrtc;

hrtc::TcpClient::TcpClient() :loop_(new uv_loop_t), api_thread_(dispatch_task_queue::create("tcpClient"))
{
    uv_loop_init(loop_);
}

hrtc::TcpClient::~TcpClient()
{
    disconnect();
    uv_loop_close(loop_);
    delete loop_;
}

void hrtc::TcpClient::connect(const std::string& ip, int port, int timeout_ms)
{
    api_thread_->sync([&]() {
        if (state_ < ConnectionState::Connecting) {
            state_ = ConnectionState::Connecting;
            connection_.reset(new TcpConnection(loop_));
            connection_->addObserver(this);
            connection_->connect(ip, port, timeout_ms);
            run();
        }
    });
}

void hrtc::TcpClient::disconnect()
{
    api_thread_->async([&]() {
        if (state_ >= ConnectionState::Connecting) {
            state_ = ConnectionState::Disconnected;
            if (connection_) {
                connection_->close();
                stop();
                connection_.reset();
            }
        }
    });
}

int hrtc::TcpClient::sendMessage(const std::string& message)
{
    int ret = -1;
    api_thread_->sync([&]() {
        if (isConnected()) {
            connection_->write(message);
            ret = 0;
        }
    });
    return ret;
}

void hrtc::TcpClient::run()
{
    loop_thread_ = std::thread([this]() {
        uv_run(loop_, UV_RUN_DEFAULT);
    });
}

void hrtc::TcpClient::stop()
{
    uv_stop(loop_);
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}

void hrtc::TcpClient::onDataReceived(TcpConnection*,const std::string& data)
{
    std::cout << "Server: " << data << std::endl;
}

void hrtc::TcpClient::onConnectionClosed(TcpConnection* c)
{
    disconnect();
}

void hrtc::TcpClient::onConnectionEstablished(TcpConnection*)
{
    state_ = ConnectionState::Connected;
}
