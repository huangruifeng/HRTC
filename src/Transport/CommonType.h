#pragma once

#include<vector>
#include<string>
namespace hrtc {
	class DataStreamType {
	public:
        enum class Type : uint8_t {
            JOIN = 0,
            UNICAST = 1,
            BROADCAST = 2,
            LEAVE = 3,
		};
		int type;
		std::vector<std::string> param;
	};
}