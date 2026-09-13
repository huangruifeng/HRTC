#pragma once
#include <memory>
#include <string>
#include <Base/Collections.h>
#include <Base/ErrorCode.h>
#include "Base/Thread/DispatchQueue.h"
#include "Transport/tcp/TcpClient.h"
#include "Transport/common/CommonType.h"
#include "Transport/heartbeat/Heartbeat.h"

namespace hrtc {

	class DatastreamObserver {
	public:
		virtual ~DatastreamObserver() = default;
		virtual void onUserJoined(const std::string& userId) = 0;
		virtual void onUnicast(const std::string& fromId, const std::string& toId, const std::string& data) = 0;
		virtual void onBroadcast(const std::string& fromId, const std::string& data) = 0;
		virtual void onUserLeave(const std::string& userId) = 0;
		// 服务器通知：房间新用户 requesterId 请求全量数据（本端为存在最久的用户时触发），
		// 收到后应把全部数据通过 Unicast 单播给 requesterId
		virtual void onRequestData(const std::string& requesterId) = 0;
		virtual void onError(int errorCode,const std::string& error) = 0;
		virtual void onConnected() = 0;
		virtual void onDisconnected() = 0;
	};

	// 数据流客户端：封装 TcpClient，提供加入房间/单播/广播/离开房间等业务接口。
	// 线程模型：所有 API 与内部状态访问经 api_thread_（dispatch_task_queue）串行化，
	// 内部无锁；TcpClient 的 loop 回调（on_connected/on_message/on_disconnected）
	// 经 async 投递到 api_thread_ 统一处理，observer 通知在 api_thread_ 触发
	// （dispatch_task_queue::sync 同线程直接执行，observer 回调中再调 API 不会死锁）。
	// 心跳/断线检测由底层 TcpClient 维护（PING/PONG）。
	class DataStream : public TcpClientObserver {
	public:
		// serverIp/serverPort 可留空，之后通过 Connect 显式指定
		explicit DataStream(const std::string& serverIp = "", int serverPort = 0,
		                    const HeartbeatConfig& heartbeat = HeartbeatConfig());
		~DataStream();

		void RegisterObserver(DatastreamObserver* observer) { observers_.AddElement(observer); }
		void UnregisterObserver(DatastreamObserver* observer) { observers_.RemoveElement(observer); }

		RtcResult Connect(const std::string& serverIp, int serverPort);
		RtcResult Disconnect();
		RtcResult JoinRoom(const std::string& roomId, const std::string& userId);
		RtcResult Unicast(const std::string& toId, const std::string& data);
		RtcResult Broadcast(const std::string& data);
		RtcResult LeaveRoom();

		bool IsConnected() const { return client_.isConnected(); }
		std::string currentUser() const;
		std::string currentRoom() const;

		// TcpClientObserver（TcpClient loop 线程回调，内部投递 api_thread_）
		void on_connected() override;
		void on_disconnected() override;
		void on_message(const std::string& data) override;

	private:
		RtcResult Send(const DataStreamType& stream);       // 仅 api_thread_ 线程调用
		void DecodeData(const std::string& data);           // 仅 api_thread_ 线程调用
		void NotifyError(int errorCode, const std::string& error);

	private:
		TcpClient client_;
		// api_thread_ 串行化以下所有状态访问（无需加锁）。
		// 成员析构顺序（逆声明序）：observers_ → 状态 → api_thread_ → client_，
		// 即 api_thread_ 先于 client_ 析构，析构函数中已做 clear + 同步屏障。
		std::shared_ptr<dispatch_task_queue::task_queue> api_thread_;
		std::string server_ip_;
		int server_port_ = 0;
		std::string current_user_;
		std::string current_room_;
		bool joined_ = false;
		bool pending_join_ = false;

		hrtc::Collections<DatastreamObserver, hrtc::RawPointer, hrtc::MultiThreaded> observers_;
	};
}
