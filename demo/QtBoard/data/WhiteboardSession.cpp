#include "WhiteboardSession.h"

#include <Base/ErrorCode.h>

#include "Whiteboard/command/element_commands.h"
#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/full_sync_command.h"
#include "Whiteboard/command/page_commands.h"
#include "Whiteboard/command/preview_commands.h"
#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/transform_commands.h"
#include "Whiteboard/command/undo_redo_commands.h"

WhiteboardSession::WhiteboardSession(QtBoardData& data, QObject* parent)
    : QObject(parent), data_(data) {}

WhiteboardSession::~WhiteboardSession() {
    if (stream_) {
        data_.SetOutgoingCmdCb(nullptr);
        stream_->UnregisterObserver(this);
        stream_.reset();
    }
}

bool WhiteboardSession::Start(const std::string& ip, int port,
                              const std::string& roomId, const std::string& userId) {
    if (stream_)
        return false;
    userId_ = userId;
    roomId_ = roomId;

    stream_ = std::make_unique<hrtc::DataStream>();
    stream_->RegisterObserver(this);

    // 本地操作产生的命令 → 序列化后广播（回调在 QtBoardData 数据线程触发）
    data_.SetOutgoingCmdCb([this](std::shared_ptr<whiteboard::Command> cmd) {
        if (!stream_ || !cmd)
            return;
        try {
            stream_->Broadcast(whiteboard::protocol::SerializeCommand(*cmd));
        } catch (...) {
            // 序列化失败忽略（不应发生）
        }
    });

    const int rc1 = stream_->Connect(ip, port);
    if (HRTC_FAILED(rc1)) {
        Stop();
        emit errorOccurred(QStringLiteral("连接服务器失败（错误码 %1）").arg(rc1));
        return false;
    }

    const int rc2 = stream_->JoinRoom(roomId, userId);
    if (HRTC_FAILED(rc2)) {
        Stop();
        emit errorOccurred(QStringLiteral("加入房间失败（错误码 %1）").arg(rc2));
        return false;
    }

    // JOIN 已发出（TCP 可靠），后续远程命令将实时应用
    emit joined();
    return true;
}

void WhiteboardSession::Stop() {
    if (!stream_)
        return;
    data_.SetOutgoingCmdCb(nullptr);
    stream_->UnregisterObserver(this);
    stream_->LeaveRoom();
    stream_->Disconnect();
    stream_.reset();
    emit disconnected();
}

// ---------- DatastreamObserver（DataStream api 线程触发） ----------

void WhiteboardSession::onUserJoined(const std::string& /*userId*/) {
    // 服务器不向新用户自身回发 JOIN，此处仅用于其他用户加入通知（当前无需 UI 展示）
}

void WhiteboardSession::onUnicast(const std::string& /*fromId*/,
                                  const std::string& /*toId*/,
                                  const std::string& data) {
    // 单播：全量同步（onRequestData 应答）或其他定向命令
    try {
        DispatchCommand(whiteboard::protocol::DeserializeCommand(data));
    } catch (...) {
        emit errorOccurred(QStringLiteral("单播数据解析失败"));
    }
}

void WhiteboardSession::onBroadcast(const std::string& /*fromId*/, const std::string& data) {
    try {
        DispatchCommand(whiteboard::protocol::DeserializeCommand(data));
    } catch (...) {
        emit errorOccurred(QStringLiteral("广播数据解析失败"));
    }
}

void WhiteboardSession::onUserLeave(const std::string& /*userId*/) {}

void WhiteboardSession::onRequestData(const std::string& requesterId) {
    // 本端为房间最久用户：全量导出页面 → FullSync 单播给请求者
    if (!stream_)
        return;
    auto cmd = std::make_shared<whiteboard::FullSync>();
    data_.GetAllPages(cmd->pages);  // 同步等待数据线程（深拷贝，安全）
    try {
        stream_->Unicast(requesterId, whiteboard::protocol::SerializeCommand(*cmd));
    } catch (...) {
        emit errorOccurred(QStringLiteral("全量数据发送失败"));
    }
}

void WhiteboardSession::onError(int errorCode, const std::string& error) {
    emit errorOccurred(QStringLiteral("网络错误 %1: %2")
                           .arg(errorCode)
                           .arg(QString::fromStdString(error)));
}

void WhiteboardSession::onConnected() {
    emit connected();
}

void WhiteboardSession::onDisconnected() {
    emit disconnected();
}

// ---------- 命令分派 ----------

void WhiteboardSession::DispatchCommand(const std::shared_ptr<whiteboard::Command>& cmd) {
    if (!cmd)
        return;
    if (auto* c = dynamic_cast<whiteboard::StrokeBegin*>(cmd.get())) {
        data_.RemoteStrokeBegin(c->strokeId, c->width, c->color, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::StrokeMove*>(cmd.get())) {
        data_.RemoteStrokeMove(c->strokeId, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::StrokeEnd*>(cmd.get())) {
        data_.RemoteStrokeEnd(c->strokeId, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::EraserBegin*>(cmd.get())) {
        data_.RemoteEraserBegin(c->sessionId, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::EraserMove*>(cmd.get())) {
        data_.RemoteEraserMove(c->sessionId, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::EraserEnd*>(cmd.get())) {
        data_.RemoteEraserEnd(c->sessionId, c->removedIds, c->addedElements, c->addedPlacements);
    } else if (auto* c = dynamic_cast<whiteboard::LassoPreview*>(cmd.get())) {
        data_.RemoteLassoPreview(c->sessionId, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::SelectionPreview*>(cmd.get())) {
        data_.RemoteSelectionPreview(c->sessionId, c->points, c->selectedIds);
    } else if (auto* c = dynamic_cast<whiteboard::StrokeUpdate*>(cmd.get())) {
        data_.RemoteUpdateStroke(c->strokeId, c->points);
    } else if (auto* c = dynamic_cast<whiteboard::ElementAdd*>(cmd.get())) {
        data_.RemoteAddElement(c->element);
    } else if (auto* c = dynamic_cast<whiteboard::ElementRemove*>(cmd.get())) {
        data_.RemoteRemoveElements({ c->elementId });
    } else if (auto* c = dynamic_cast<whiteboard::ElementUpdate*>(cmd.get())) {
        data_.RemoteUpdateElement(c->element);
    } else if (auto* c = dynamic_cast<whiteboard::PageCreate*>(cmd.get())) {
        data_.RemoteCreatePage(c->newPageId);
    } else if (dynamic_cast<whiteboard::PageSelect*>(cmd.get())) {
        data_.RemoteSelectPage(cmd->pageId);
    } else if (dynamic_cast<whiteboard::PageDelete*>(cmd.get())) {
        data_.RemoteDeletePage(cmd->pageId);
    } else if (dynamic_cast<whiteboard::PageClear*>(cmd.get())) {
        data_.RemoteClear();
    } else if (dynamic_cast<whiteboard::Undo*>(cmd.get())) {
        data_.RemoteUndo();
    } else if (dynamic_cast<whiteboard::Redo*>(cmd.get())) {
        data_.RemoteRedo();
    } else if (auto* c = dynamic_cast<whiteboard::FullSync*>(cmd.get())) {
        data_.ApplyFullSync(c->pages);
    }
}
