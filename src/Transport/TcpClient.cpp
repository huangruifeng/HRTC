#include "TcpClient.h"
using namespace hrtc;

hrtc::TcpClient::TcpClient():loop_(new uv_loop_t)
{
    uv_loop_init(loop_);
}

hrtc::TcpClient::~TcpClient()
{
    uv_loop_close(loop_);
    delete loop_;
}

void hrtc::TcpClient::connect(const std::string& ip, int port, int timeout_ms)
{
    connection_.reset(new TcpConnection(loop_));
    connection_->connect(ip, port, timeout_ms);
    run();
}

void hrtc::TcpClient::disconnect()
{
    if (connection_){
        connection_->close();
        stop();
        connection_.reset();
    }
}

void hrtc::TcpClient::sendMessage(const std::string& message)
{
    if (isConnected()) {
        connection_->write(message);
    }
}

void hrtc::TcpClient::run()
{
    thread_ = std::thread([this](){
        uv_run(loop_, UV_RUN_DEFAULT);
    });
}

void hrtc::TcpClient::stop()
{
    uv_stop(loop_);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void hrtc::TcpClient::onDataReceived(TcpConnection*,const std::string& data)
{

}

void hrtc::TcpClient::onConnectionClosed(TcpConnection*)
{
    connected_ = false;
}

void hrtc::TcpClient::onConnectionEstablished(TcpConnection*)
{
    connected_ = true;
}
