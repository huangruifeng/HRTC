// 服务端示例：启动 TcpRoomServer(8083)，打印用户加入/离开/消息日志。
#include <chrono>
#include <cstdio>
#include <thread>
#include "Transport/transport.h"

using namespace hrtc;

// 日志 observer：演示 RoomServerObserver 的使用
class DemoServerObserver : public RoomServerObserver {
public:
    void on_user_joined(const std::string& roomId, const std::string& userId) override {
        printf("[server] user joined: room=%s user=%s\n", roomId.c_str(), userId.c_str());
    }
    void on_user_leave(const std::string& roomId, const std::string& userId) override {
        printf("[server] user leave : room=%s user=%s\n", roomId.c_str(), userId.c_str());
    }
    void on_unicast(const std::string& roomId, const std::string& fromId,
                    const std::string& toId, const std::string& data) override {
        printf("[server] unicast    : room=%s %s -> %s : %s\n",
               roomId.c_str(), fromId.c_str(), toId.c_str(), data.c_str());
    }
    void on_broadcast(const std::string& roomId, const std::string& fromId,
                      const std::string& data) override {
        printf("[server] broadcast  : room=%s %s -> all : %s\n",
               roomId.c_str(), fromId.c_str(), data.c_str());
    }
    void on_error(int errorCode, const std::string& error) override {
        printf("[server] error      : code=%d msg=%s\n", errorCode, error.c_str());
    }
};

int main() {
    TcpRoomServer server;
    DemoServerObserver observer;
    server.add_observer(&observer);

    int ret = server.Start(8083);
    if (ret != HRTC_CODE_OK) {
        printf("[server] start failed: %d\n", ret);
        return 1;
    }
    printf("[server] listening on port 8083, press Ctrl+C to quit...\n");

    // 运行 3600 秒后退出（演示用，便于长时间联调）
    std::this_thread::sleep_for(std::chrono::seconds(3600));
    server.Stop();
    server.remove_observer(&observer);
    printf("[server] stopped.\n");
    return 0;
}
