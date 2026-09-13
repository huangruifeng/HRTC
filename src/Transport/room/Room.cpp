#include "Room.h"
#include <Base/ErrorCode.h>
#include <algorithm>
using namespace hrtc;

hrtc::Room::Room(const std::string& roomId)
    : room_id_(roomId)
{
}

int hrtc::Room::AddUser(const std::string& userId, const std::shared_ptr<TcpConnection>& conn)
{
    if (!conn)
        return HRTC_CODE_ERROR_NULLPTR;

    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.find(userId) != users_.end())
        return HRTC_CODE_ERROR_DUPLICATED;
    users_.emplace(userId, conn);
    join_order_.push_back(userId);   // 记录加入顺序（front 为最久）
    return HRTC_CODE_OK;
}

int hrtc::Room::RemoveUser(const std::string& userId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.erase(userId) == 0)
        return HRTC_CODE_ERROR_NOT_FOUND;
    join_order_.erase(std::remove(join_order_.begin(), join_order_.end(), userId),
                      join_order_.end());
    return HRTC_CODE_OK;
}

bool hrtc::Room::HasUser(const std::string& userId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.find(userId) != users_.end();
}

std::shared_ptr<TcpConnection> hrtc::Room::GetConnection(const std::string& userId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = users_.find(userId);
    if (it == users_.end())
        return nullptr;
    return it->second;
}

std::vector<std::string> hrtc::Room::GetUserIds() const
{
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lock(mutex_);
    ids.reserve(users_.size());
    for (const auto& kv : users_)
        ids.push_back(kv.first);
    return ids;
}

size_t hrtc::Room::UserCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.size();
}

std::string hrtc::Room::GetOldestUserId() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return join_order_.empty() ? std::string() : join_order_.front();
}

int hrtc::Room::Unicast(const std::string& toId, const std::string& data)
{
    std::shared_ptr<TcpConnection> target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = users_.find(toId);
        if (it == users_.end())
            return HRTC_CODE_ERROR_NOT_FOUND;
        target = it->second;
    }
    target->write(data);
    return HRTC_CODE_OK;
}

int hrtc::Room::Broadcast(const std::string& data, const std::string& exceptId)
{
    // 快照，避免长时间持锁
    std::vector<std::shared_ptr<TcpConnection>> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        targets.reserve(users_.size());
        for (const auto& kv : users_) {
            if (!exceptId.empty() && kv.first == exceptId)
                continue;
            targets.push_back(kv.second);
        }
    }
    for (const auto& conn : targets) {
        conn->write(data);
    }
    return HRTC_CODE_OK;
}
