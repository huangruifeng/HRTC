#include "TcpClient.h"
#include "TcpServer.h"


int main() {

    hrtc::TcpServer server;
    server.async_run(8083);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::thread([]() {
        hrtc::TcpClient client;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        client.connect("127.0.0.1", 8083);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        client.sendMessage("hello");
        client.sendMessage("world");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        client.sendMessage("bye");
        client.disconnect();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }).join();
    
    std::this_thread::sleep_for(std::chrono::seconds(20));
    server.stop();
    return 0;
}