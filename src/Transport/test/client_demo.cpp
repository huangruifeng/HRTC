// 客户端示例：两个 DataStream（不同 userId）加入同一房间，
// 验证 unicast / broadcast / 心跳存活，需先启动 server_demo。
#include <chrono>
#include <cstdio>
#include <thread>
#include "Transport/transport.h"

using namespace hrtc;

// 客户端日志 observer：演示 DatastreamObserver 的使用
class DemoStreamObserver : public DatastreamObserver {
public:
    explicit DemoStreamObserver(const std::string& tag) : tag_(tag) {}

    void onUserJoined(const std::string& userId) override {
        printf("[%s] user joined : %s\n", tag_.c_str(), userId.c_str());
    }
    void onUnicast(const std::string& fromId, const std::string& toId,
                   const std::string& data) override {
        printf("[%s] unicast     : %s -> %s : %s\n",
               tag_.c_str(), fromId.c_str(), toId.c_str(), data.c_str());
    }
    void onBroadcast(const std::string& fromId, const std::string& data) override {
        printf("[%s] broadcast   : %s -> all : %s\n",
               tag_.c_str(), fromId.c_str(), data.c_str());
    }
    void onUserLeave(const std::string& userId) override {
        printf("[%s] user leave  : %s\n", tag_.c_str(), userId.c_str());
    }
    void onRequestData(const std::string& requesterId) override {
        printf("[%s] request data: from %s (send full data via unicast)\n",
               tag_.c_str(), requesterId.c_str());
    }
    void onError(int errorCode, const std::string& error) override {
        printf("[%s] error       : code=%d msg=%s\n",
               tag_.c_str(), errorCode, error.c_str());
    }
    void onConnected() override {
        printf("[%s] connected   : user=%s room=%s\n",
               tag_.c_str(), user_.c_str(), room_.c_str());
    }
    void onDisconnected() override {
        printf("[%s] disconnected: user=%s room=%s\n",
               tag_.c_str(), user_.c_str(), room_.c_str());
    }

    void setIdentity(const std::string& user, const std::string& room) {
        user_ = user;
        room_ = room;
    }

private:
    std::string tag_;
    std::string user_;
    std::string room_;
};

int main() {
    // 心跳用短周期便于演示：2s 一次 PING，6s 无响应判定超时
    HeartbeatConfig hb;
    hb.pingIntervalMs = 2000;
    hb.timeoutMs = 6000;

    DemoStreamObserver obsA("clientA");
    DemoStreamObserver obsB("clientB");
    obsA.setIdentity("userA", "room1");
    obsB.setIdentity("userB", "room1");

    DataStream a("127.0.0.1", 8083, hb);
    DataStream b("127.0.0.1", 8083, hb);
    a.RegisterObserver(&obsA);
    b.RegisterObserver(&obsB);

    // 1. 两个客户端加入同一房间（未连接时自动连接，连上后补发 JOIN）
    RtcResult r1 = a.JoinRoom("room1", "userA");
    RtcResult r2 = b.JoinRoom("room1", "userB");
    printf("[main] JoinRoom ret: a=%d b=%d\n", r1, r2);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. 单播：A -> B
    a.Unicast("userB", "hello B, this is A (unicast)");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 3. 广播：B -> 所有人
    b.Broadcast("hello everyone, this is B (broadcast)");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 4. 广播：A -> 所有人
    a.Broadcast("broadcast from A");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 5. 心跳验证：等待超过 2 个心跳周期，连接应保持（PING/PONG 正常）
    std::this_thread::sleep_for(std::chrono::seconds(5));
    printf("[main] after heartbeat: a connected=%d b connected=%d\n",
           a.IsConnected() ? 1 : 0, b.IsConnected() ? 1 : 0);

    // 6. B 离开房间，A 应收到 LEAVE 通知
    b.LeaveRoom();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 7. 断开连接
    a.Disconnect();
    b.Disconnect();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 8. 断线重连验证：B 断开后立即重新加入房间并广播
    RtcResult r3 = b.JoinRoom("room1", "userB");
    printf("[main] Re-JoinRoom ret: b=%d\n", r3);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    b.Broadcast("B reconnected");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    b.Disconnect();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    a.UnregisterObserver(&obsA);
    b.UnregisterObserver(&obsB);
    printf("[main] client demo done.\n");
    return 0;
}
