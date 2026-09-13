#include "RoomManager.h"

using namespace hrtc;

std::shared_ptr<Room> hrtc::RoomManager::GetOrCreate(const std::string& roomId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(roomId);
    if (it != rooms_.end())
        return it->second;
    auto room = std::make_shared<Room>(roomId);
    rooms_.emplace(roomId, room);
    return room;
}

std::shared_ptr<Room> hrtc::RoomManager::Get(const std::string& roomId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return nullptr;
    return it->second;
}

std::shared_ptr<Room> hrtc::RoomManager::Remove(const std::string& roomId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return nullptr;
    auto room = it->second;
    rooms_.erase(it);
    return room;
}

bool hrtc::RoomManager::RemoveIfEmpty(const std::string& roomId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = rooms_.find(roomId);
    if (it == rooms_.end())
        return false;
    if (it->second->UserCount() > 0)
        return false;
    rooms_.erase(it);
    return true;
}

std::vector<std::string> hrtc::RoomManager::GetRoomIds() const
{
    std::vector<std::string> ids;
    std::lock_guard<std::mutex> lock(mutex_);
    ids.reserve(rooms_.size());
    for (const auto& kv : rooms_)
        ids.push_back(kv.first);
    return ids;
}

size_t hrtc::RoomManager::RoomCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.size();
}

void hrtc::RoomManager::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    rooms_.clear();
}
