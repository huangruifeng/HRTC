// 互动房间探测客户端（控制台）：连接服务器并加入房间，打印全部回调。
// 用途：端到端联调时观察 JOIN / REQUEST_DATA / FullSync 全链路。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "Transport/transport.h"
#include "Whiteboard/command/full_sync_command.h"
#include "Whiteboard/protocol/protocol.h"

using namespace hrtc;

static void Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

class ProbeObserver : public DatastreamObserver {
public:
    void onUserJoined(const std::string& userId) override {
        Log("[probe] user joined : %s\n", userId.c_str());
    }
    void onUserLeave(const std::string& userId) override {
        Log("[probe] user leave  : %s\n", userId.c_str());
    }
    void onUnicast(const std::string& fromId, const std::string& toId,
                   const std::string& data) override {
        Log("[probe] unicast     : %s -> %s (%zu bytes)\n",
            fromId.c_str(), toId.c_str(), data.size());
        DumpCommand(data);
    }
    void onBroadcast(const std::string& fromId, const std::string& data) override {
        Log("[probe] broadcast   : %s (%zu bytes)\n", fromId.c_str(), data.size());
        DumpCommand(data);
    }
    void onRequestData(const std::string& requesterId) override {
        Log("[probe] REQUEST_DATA: requester=%s -> respond FullSync\n",
            requesterId.c_str());
        whiteboard::FullSync sync;
        whiteboard::Page p;
        p.pageId = "probe-page-1";
        sync.pages.push_back(p);
        const std::string data = whiteboard::protocol::SerializeCommand(sync);
        stream->Unicast(requesterId, data);
    }
    void onError(int errorCode, const std::string& error) override {
        Log("[probe] error       : code=%d msg=%s\n", errorCode, error.c_str());
    }
    void onConnected() override { Log("[probe] connected\n"); }
    void onDisconnected() override { Log("[probe] disconnected\n"); }

    DataStream* stream = nullptr;

private:
    static void DumpCommand(const std::string& data) {
        try {
            auto cmd = whiteboard::protocol::DeserializeCommand(data);
            if (!cmd) {
                Log("[probe]   command : <null>\n");
                return;
            }
            Log("[probe]   command : %s pageId=%s\n", cmd->GetType().c_str(),
                cmd->pageId.c_str());
            if (auto* sync = dynamic_cast<whiteboard::FullSync*>(cmd.get())) {
                Log("[probe]   FullSync : %zu pages\n", sync->pages.size());
                for (size_t i = 0; i < sync->pages.size(); ++i) {
                    Log("[probe]     page[%zu] id=%s elements=%zu\n", i,
                        sync->pages[i].pageId.c_str(),
                        sync->pages[i].elements.size());
                }
            }
        } catch (...) {
            Log("[probe]   command : <deserialize error>\n");
        }
    }
};

int main(int argc, char* argv[]) {
    std::string ip = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? std::atoi(argv[2]) : 8083;
    std::string room = argc > 3 ? argv[3] : "room-1";
    std::string user = argc > 4 ? argv[4] : "user-probe";
    int seconds = argc > 5 ? std::atoi(argv[5]) : 60;

    DataStream ds;
    ProbeObserver obs;
    obs.stream = &ds;
    ds.RegisterObserver(&obs);

    Log("[probe] connect %s:%d room=%s user=%s\n", ip.c_str(), port,
        room.c_str(), user.c_str());
    const RtcResult rc1 = ds.Connect(ip, port);
    Log("[probe] Connect rc=%d\n", rc1);
    const RtcResult rc2 = ds.JoinRoom(room, user);
    Log("[probe] JoinRoom rc=%d (pending_join -> 连接后自动 JOIN)\n", rc2);

    std::this_thread::sleep_for(std::chrono::seconds(seconds));

    ds.LeaveRoom();
    ds.Disconnect();
    ds.UnregisterObserver(&obs);
    Log("[probe] done.\n");
    return 0;
}
