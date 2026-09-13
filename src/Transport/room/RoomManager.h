#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include "Room.h"

namespace hrtc {

// 服务端房间集合：按 roomId 管理多个 Room。线程安全。
class RoomManager {
public:
    // 获取房间；不存在且 createIfMissing 为 true 时创建。
    std::shared_ptr<Room> GetOrCreate(const std::string& roomId);
    std::shared_ptr<Room> Get(const std::string& roomId) const;

    // 移除房间（返回被移除的 Room，不存在返回 nullptr）。
    std::shared_ptr<Room> Remove(const std::string& roomId);

    // 房间为空（无用户）时移除，返回是否发生移除。
    bool RemoveIfEmpty(const std::string& roomId);

    std::vector<std::string> GetRoomIds() const;
    size_t RoomCount() const;
    void Clear();

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Room>> rooms_;
};

}
