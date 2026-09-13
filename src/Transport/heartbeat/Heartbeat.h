#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace hrtc {

// 心跳配置：客户端每 pingIntervalMs 发送一次 PING，
// 超过 timeoutMs 未收到 PONG 判定超时。
struct HeartbeatConfig {
    int pingIntervalMs = 15000;   // 客户端 PING 间隔
    int timeoutMs = 45000;        // 超时判定（默认 3 次 PING 无响应）
};

// 稳态时钟毫秒时间戳（单调递增，用于超时计算）。
int64_t NowMs();

// 服务端活跃表监控：记录每个 key（如 TcpConnection*）的最近活跃时间，
// 供服务端定时器扫描超时连接。线程安全。
template <typename Key>
class ServerHeartbeatMonitor {
public:
    void Touch(const Key& key) { Touch(key, NowMs()); }

    void Touch(const Key& key, int64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_[key] = now;
    }

    void Erase(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.erase(key);
    }

    // 返回所有 now - lastActive > timeoutMs 的 key
    std::vector<Key> GetTimeoutKeys(int64_t now, int64_t timeoutMs) const {
        std::vector<Key> result;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : active_) {
            if (now - kv.second > timeoutMs) {
                result.push_back(kv.first);
            }
        }
        return result;
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        active_.clear();
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_.size();
    }

private:
    mutable std::mutex mutex_;
    std::map<Key, int64_t> active_;
};

}
