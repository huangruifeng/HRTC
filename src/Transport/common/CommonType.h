#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hrtc {
	// 数据流协议消息。字段布局（msgpack）：
	//   [type, [param0, param1, ...]]
	// JOIN    : [roomId, userId]
	// UNICAST : [fromId, toId, data]
	// BROADCAST : [fromId, data]
	// LEAVE   : [userId]
	// PING / PONG : []
	class DataStreamType {
	public:
        enum class Type : uint8_t {
            JOIN = 0,
            UNICAST = 1,
            BROADCAST = 2,
            LEAVE = 3,
            PING = 4,
            PONG = 5,
            REQUEST_DATA = 6,
		};
		int type = (int)Type::JOIN;
		std::vector<std::string> param;
	};

	// msgpack 编解码：Serialize 返回二进制字符串，Deserialize 失败返回 false。
	std::string SerializeStream(const DataStreamType& stream);
	bool DeserializeStream(const std::string& data, DataStreamType& stream);
}
