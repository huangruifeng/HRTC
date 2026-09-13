#pragma once
// Transport 网络传输层聚合头。
// 目录结构：
//   common/    公共类型与协议消息（后续 UDP/RTP 复用）
//   tcp/       TCP 连接/服务端/客户端
//   heartbeat/ 心跳配置与活跃表监控
//   room/      服务端房间管理
//   server/    房间服务端（TCP 实现）
//   stream/    客户端数据流业务入口

#include "Transport/common/CommonType.h"
#include "Transport/common/IpUtils.h"
#include "Transport/heartbeat/Heartbeat.h"
#include "Transport/tcp/TcpConnection.h"
#include "Transport/tcp/TcpServer.h"
#include "Transport/tcp/TcpClient.h"
#include "Transport/room/Room.h"
#include "Transport/room/RoomManager.h"
#include "Transport/server/TcpRoomServer.h"
#include "Transport/stream/DataStream.h"
