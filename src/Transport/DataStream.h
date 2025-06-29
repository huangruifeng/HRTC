#pragma once
#include "CommonType.h"
#include "Base/Collections.h"
#include <Base/ErrorCode.h>

namespace hrtc {

	class DatastreamObserver {
	public:
		virtual void onUserJoined(const std::string& userId) = 0;
		virtual void onUnicast(const std::string& fromId, const std::string& toId, const std::string& data) = 0;
		virtual void onBroadcast(const std::string& fromId, const std::string& data) = 0;
		virtual void onUserLeave(const std::string& userId) = 0;
		virtual void onError(int errorCode,const std::string& error) = 0;

		virtual void OnEncodeData(const std::string& message) = 0;
	};

	class DataStream {
	public:
		DataStream();
		void OnData(const std::string& data);
		RtcResult JoinRoom(const std::string& roomId,const std::string& userId);
		RtcResult Unicast(const std::string& toId, const std::string& data);
		RtcResult Broadcast(const std::string& data);
		RtcResult LeaveRoom();
		~DataStream() {}
	private:
		void EncodeData(const hrtc::DataStreamType& stream);
		void DecodeData(const std::string& data);
	private:
		Collections<DatastreamObserver> observers_;
		std::string currentUser_;
		std::string currentRoom_;
	};
}