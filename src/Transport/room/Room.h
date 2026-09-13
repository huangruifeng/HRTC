#pragma once
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "Transport/tcp/TcpConnection.h"

namespace hrtc {

// 服务端房间：维护房间内用户与其 TCP 连接的映射，提供单播/广播。
// 线程安全：所有成员访问由内部互斥锁保护，广播前先拷贝快照。
class Room {
public:
    explicit Room(const std::string& roomId);
    ~Room() = default;

    const std::string& roomId() const { return room_id_; }

    // 用户加入。重复 userId 返回 HRTC_CODE_ERROR_DUPLICATED。
    int AddUser(const std::string& userId, const std::shared_ptr<TcpConnection>& conn);
    int RemoveUser(const std::string& userId);

    bool HasUser(const std::string& userId) const;
    std::shared_ptr<TcpConnection> GetConnection(const std::string& userId) const;
    std::vector<std::string> GetUserIds() const;
    size_t UserCount() const;
    // 加入时间最久的用户（无用户返回空字符串）
    std::string GetOldestUserId() const;

    // 单播：发给 toId 用户。目标不存在返回 HRTC_CODE_ERROR_NOT_FOUND。
    int Unicast(const std::string& toId, const std::string& data);
    // 广播：发给房间内所有用户（exceptId 非空时排除该用户）。
    int Broadcast(const std::string& data, const std::string& exceptId = "");

private:
    std::string room_id_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<TcpConnection>> users_;
    std::deque<std::string> join_order_;  // 加入顺序（front 为最久）
};

}
