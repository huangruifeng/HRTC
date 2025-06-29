#include "DataStream.h"
using namespace hrtc;

hrtc::DataStream::DataStream()
{
}

void hrtc::DataStream::OnData(const std::string& data)
{
	DecodeData(data);
}

RtcResult hrtc::DataStream::JoinRoom(const std::string& roomId,const std::string& uuid)
{
	currentRoom_ = roomId;
	currentUser_ = uuid;
	hrtc::DataStreamType joinType;
	joinType.type =(int) hrtc::DataStreamType::Type::JOIN;
	joinType.param.push_back(roomId);
	joinType.param.push_back(uuid);
	return RtcResult();
}

RtcResult hrtc::DataStream::Unicast(const std::string& toId, const std::string& data)
{
	return RtcResult();
}

RtcResult hrtc::DataStream::Broadcast(const std::string& data)
{
	return RtcResult();
}

RtcResult hrtc::DataStream::LeaveRoom()
{
	return RtcResult();
}

void hrtc::DataStream::EncodeData(const hrtc::DataStreamType& stream)
{

}

void hrtc::DataStream::DecodeData(const std::string& data)
{
	auto stream = DataStreamType();
	switch ((hrtc::DataStreamType::Type)stream.type) {
		case hrtc::DataStreamType::Type::JOIN:
		{
			observers_.Foreach([&](const auto& observer) {
				observer->onUserJoined(stream.param[1]);
			});
			break;
		}
		case hrtc::DataStreamType::Type::UNICAST:
		{
			observers_.Foreach([&](const auto& observer) {
				observer->onUnicast(stream.param[0], stream.param[1],stream.param[2]);
			});
			break;
		}
		case hrtc::DataStreamType::Type::BROADCAST:
		{
			observers_.Foreach([&](const auto& observer) {
				observer->onBroadcast(stream.param[0],stream.param[1]);
			});
			break;
		}
		case hrtc::DataStreamType::Type::LEAVE:
		{
			observers_.Foreach([&](const auto& observer) {
				observer->onUserLeave(stream.param[0]);
			});
			break;
		}
		default:
		{
			observers_.Foreach([&](const auto& observer) {
				observer->onError(-1, "unknow type");
			});
		}
	}
}
