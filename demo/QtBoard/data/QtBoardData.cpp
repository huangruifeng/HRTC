#include "QtBoardData.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "Whiteboard/command/eraser_commands.h"
#include "Whiteboard/command/page_commands.h"
#include "Whiteboard/command/preview_commands.h"
#include "Whiteboard/command/stroke_commands.h"
#include "Whiteboard/command/transform_commands.h"
#include "Whiteboard/command/undo_redo_commands.h"

namespace {
// 与 Stroke::Reset 保持一致的 chrono 时间戳 id
std::string GeneratePageId() {
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
}

// 橡皮矩形 -> 4 点（左上、右上、右下、左下，供网络传输）
std::vector<whiteboard::Point> RectToPoints(const whiteboard::Rect& rc) {
    return { rc.GetTopLeft(), rc.GetTopRight(), rc.GetBottomRight(), rc.GetBottomLeft() };
}

// 网络 4 点 -> 橡皮矩形（防御：取所有点的外接矩形）
whiteboard::Rect PointsToRect(const std::vector<whiteboard::Point>& pts) {
    if (pts.empty())
        return whiteboard::Rect(0, 0, 0, 0);
    int minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (const auto& p : pts) {
        minx = std::min(minx, p.x);
        maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y);
        maxy = std::max(maxy, p.y);
    }
    return whiteboard::Rect(minx, miny, maxx - minx + 1, maxy - miny + 1);
}
}  // namespace

QtBoardData::QtBoardData() : thread_(hrtc::CreateThread("qtBoardData")) {
    pages_.push_back(std::make_shared<whiteboard::Page>());
    pages_[0]->pageId = GeneratePageId();
    pages_[0]->EnableEraserInsert(true);
}

QtBoardData::~QtBoardData() {
    // 屏障：等待数据线程上所有在途任务完成，防止析构后仍访问成员
    if (thread_)
        thread_->Invoke([]() {});
}

void QtBoardData::SetCallbacks(ElementsChangedCb elementsChanged,
                               StrokeCommittedCb strokeCommitted,
                               ClearedCb cleared,
                               PageChangedCb pageChanged,
                               StrokePreviewCb strokePreview,
                               ToolPreviewCb toolPreview,
                               SyncedCb synced) {
    thread_->BeginInvoke([this, elementsChanged = std::move(elementsChanged),
                          strokeCommitted = std::move(strokeCommitted),
                          cleared = std::move(cleared),
                          pageChanged = std::move(pageChanged),
                          strokePreview = std::move(strokePreview),
                          toolPreview = std::move(toolPreview),
                          synced = std::move(synced)]() {
        onElementsChanged_ = std::move(elementsChanged);
        onStrokeCommitted_ = std::move(strokeCommitted);
        onCleared_ = std::move(cleared);
        onPageChanged_ = std::move(pageChanged);
        onStrokePreview_ = std::move(strokePreview);
        onToolPreview_ = std::move(toolPreview);
        onSynced_ = std::move(synced);
    });
}

void QtBoardData::SetOutgoingCmdCb(OutgoingCmdCb outgoingCmd) {
    thread_->BeginInvoke([this, outgoingCmd = std::move(outgoingCmd)]() {
        onOutgoingCmd_ = std::move(outgoingCmd);
    });
}

void QtBoardData::RemoveCallbacks() {
    thread_->BeginInvoke([this]() {
        onElementsChanged_ = nullptr;
        onStrokeCommitted_ = nullptr;
        onCleared_ = nullptr;
        onPageChanged_ = nullptr;
        onOutgoingCmd_ = nullptr;
        onStrokePreview_ = nullptr;
        onToolPreview_ = nullptr;
        onSynced_ = nullptr;
    });
    thread_->Invoke([]() {});  // 屏障：返回后数据线程不会再触碰任何回调
}

std::shared_ptr<whiteboard::Page>& QtBoardData::CurrentPage() {
    return pages_[currentPage_];
}

// 操作前快照：把当前页深拷贝压入 undo 栈（上限 20），并清空 redo 栈。
// 仅数据线程调用。
void QtBoardData::PushSnapshot() {
    auto& h = histories_[CurrentPage()->pageId];
    h.undo.push_back(CurrentPage()->Clone());
    while (h.undo.size() > kHistoryLimit)
        h.undo.pop_front();
    h.redo.clear();
}

// 丢弃未完成笔画与橡皮增量（切页/撤销/重做/同步后调用）。仅数据线程调用。
void QtBoardData::ResetTransientState() {
    strokes_.clear();
    eraserRemoved_.clear();
    eraserAdded_.clear();
}

void QtBoardData::ReplaceStrokePoints(whiteboard::Stroke& stroke,
                                      const std::vector<whiteboard::Point>& points) {
    stroke.points = points;
    stroke.rawPoints = points;
    stroke.bounding = whiteboard::BoundaryRect();
    for (const auto& p : points)
        stroke.bounding.Update(p.x, p.y);
}

// ---------- 本地画笔（产生 StrokeBegin/Move/End 命令） ----------

void QtBoardData::PenBegin(const whiteboard::Point& p, int sessionId, uint64_t token) {
    thread_->BeginInvoke([this, p, sessionId, token]() {
        PushSnapshot();
        whiteboard::Stroke& s = strokes_[LocalStrokeKey(sessionId)];
        s.Reset();
        s.color = color_.load();
        s.width = penWidth_.load();
        s.Append(p);
        pendingToken_ = token;

        auto cmd = std::make_shared<whiteboard::StrokeBegin>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->strokeId = s.id;
        cmd->width = s.width;
        cmd->color = s.color;
        cmd->points = s.points;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

void QtBoardData::PenMove(const whiteboard::Point& p, int sessionId) {
    thread_->BeginInvoke([this, p, sessionId]() {
        auto it = strokes_.find(LocalStrokeKey(sessionId));
        if (it == strokes_.end())
            return;
        it->second.Append(p);

        auto cmd = std::make_shared<whiteboard::StrokeMove>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->strokeId = it->second.id;
        cmd->points = it->second.points;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

void QtBoardData::PenEnd(const whiteboard::Point& p, int sessionId) {
    thread_->BeginInvoke([this, p, sessionId]() {
        auto it = strokes_.find(LocalStrokeKey(sessionId));
        if (it == strokes_.end())
            return;
        it->second.Append(p);
        const std::vector<whiteboard::Point> pts = it->second.points;
        CurrentPage()->Append(it->second);
        const std::string id = it->second.id;
        strokes_.erase(it);

        auto cmd = std::make_shared<whiteboard::StrokeEnd>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->strokeId = id;
        cmd->points = pts;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (onStrokeCommitted_)
            onStrokeCommitted_(pendingToken_, id);
    });
}

// ---------- 本地橡皮（产生 EraserBegin/Move/End 命令，实时增量回调） ----------

void QtBoardData::EraserBegin(const whiteboard::Rect& rc, int sessionId) {
    thread_->BeginInvoke([this, rc, sessionId]() {
        PushSnapshot();  // 一次擦除拖动只记录一个快照（Move/End 不再记录）
        const std::string sid = std::to_string(sessionId);
        whiteboard::EraserResult r = CurrentPage()->Eraser(rc, sessionId);
        eraserRemoved_.insert(r.removedIds.begin(), r.removedIds.end());
        eraserAdded_.insert(eraserAdded_.end(), r.addedElements.begin(), r.addedElements.end());

        auto cmd = std::make_shared<whiteboard::EraserBegin>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->sessionId = sid;
        cmd->points = RectToPoints(rc);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (!eraserRemoved_.empty() || !eraserAdded_.empty())
            FlushEraser(false);  // 实时回调 UI，但保留累积（End 时整体清空）
    });
}

void QtBoardData::EraserMove(const whiteboard::Rect& rc, int sessionId) {
    thread_->BeginInvoke([this, rc, sessionId]() {
        const std::string sid = std::to_string(sessionId);
        whiteboard::EraserResult r = CurrentPage()->Eraser(rc, sessionId);
        eraserRemoved_.insert(r.removedIds.begin(), r.removedIds.end());
        eraserAdded_.insert(eraserAdded_.end(), r.addedElements.begin(), r.addedElements.end());

        auto cmd = std::make_shared<whiteboard::EraserMove>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->sessionId = sid;
        cmd->points = RectToPoints(rc);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (!eraserRemoved_.empty() || !eraserAdded_.empty())
            FlushEraser(false);  // 实时回调 UI，但保留累积（End 时整体清空）
    });
}

void QtBoardData::EraserEnd(const whiteboard::Rect& rc, int sessionId) {
    thread_->BeginInvoke([this, rc, sessionId]() {
        const std::string sid = std::to_string(sessionId);
        whiteboard::EraserResult r = CurrentPage()->Eraser(rc, sessionId);
        eraserRemoved_.insert(r.removedIds.begin(), r.removedIds.end());
        eraserAdded_.insert(eraserAdded_.end(), r.addedElements.begin(), r.addedElements.end());

        // 收集本次拖动全部增量（含最终回调），作为 EraserEnd 命令内容
        auto result = FlushEraser(true);

        auto cmd = std::make_shared<whiteboard::EraserEnd>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->sessionId = sid;
        cmd->removedIds = std::move(result.first);
        cmd->addedElements = std::move(result.second);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 收集橡皮增量：过滤已被后续擦除删除的碎片后回调；clearAccum 为 true 时清空累积。
// 返回过滤结果（供 EraserEnd 命令负载）。
std::pair<std::vector<std::string>, std::vector<std::shared_ptr<whiteboard::Element>>>
QtBoardData::FlushEraser(bool clearAccum) {
    std::vector<std::string> removed(eraserRemoved_.begin(), eraserRemoved_.end());
    std::vector<std::shared_ptr<whiteboard::Element>> added;
    for (auto& e : eraserAdded_) {
        // 碎片可能在后续擦除中又被删掉，以 removed 为准过滤
        if (eraserRemoved_.count(e->id) == 0)
            added.push_back(e);
    }
    if (clearAccum) {
        eraserRemoved_.clear();
        eraserAdded_.clear();
    }
    if (onElementsChanged_)
        onElementsChanged_(removed, added);
    return { std::move(removed), std::move(added) };
}

// ---------- 本地其他操作（产生对应命令） ----------

void QtBoardData::Clear() {
    thread_->BeginInvoke([this]() {
        PushSnapshot();
        CurrentPage()->Clear();
        ResetTransientState();

        auto cmd = std::make_shared<whiteboard::PageClear>();
        cmd->pageId = CurrentPage()->pageId;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (onCleared_)
            onCleared_();
    });
}

void QtBoardData::GetPage(whiteboard::Page& page) {
    thread_->Invoke([this, &page]() {
        page = *CurrentPage();
    });
}

void QtBoardData::UpdateStroke(const std::string& id,
                               const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, id, points]() {
        PushSnapshot();  // 变换烘焙是一次可撤销操作
        CurrentPage()->UpdateStroke(id, points);

        auto cmd = std::make_shared<whiteboard::StrokeUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->strokeId = id;
        cmd->points = points;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// ---------- 撤销 / 重做 ----------

bool QtBoardData::Undo() {
    bool ok = false;
    thread_->Invoke([this, &ok]() {
        auto& h = histories_[CurrentPage()->pageId];
        if (h.undo.empty())
            return;
        // 撤销前先把当前状态压入 redo 栈（上限 20）
        h.redo.push_front(CurrentPage()->Clone());
        while (h.redo.size() > kHistoryLimit)
            h.redo.pop_back();
        CurrentPage() = h.undo.back();
        h.undo.pop_back();
        ResetTransientState();

        auto cmd = std::make_shared<whiteboard::Undo>();
        cmd->pageId = CurrentPage()->pageId;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (onPageChanged_)
            onPageChanged_();
        ok = true;
    });
    return ok;
}

bool QtBoardData::Redo() {
    bool ok = false;
    thread_->Invoke([this, &ok]() {
        auto& h = histories_[CurrentPage()->pageId];
        if (h.redo.empty())
            return;
        // 重做前先把当前状态压回 undo 栈（上限 20）
        h.undo.push_back(CurrentPage()->Clone());
        while (h.undo.size() > kHistoryLimit)
            h.undo.pop_front();
        CurrentPage() = h.redo.front();
        h.redo.pop_front();
        ResetTransientState();

        auto cmd = std::make_shared<whiteboard::Redo>();
        cmd->pageId = CurrentPage()->pageId;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (onPageChanged_)
            onPageChanged_();
        ok = true;
    });
    return ok;
}

// ---------- 页面管理 ----------

std::vector<std::string> QtBoardData::GetPageIds() {
    std::vector<std::string> ids;
    thread_->Invoke([this, &ids]() {
        ids.reserve(pages_.size());
        for (auto& p : pages_)
            ids.push_back(p->pageId);
    });
    return ids;
}

std::string QtBoardData::GetCurrentPageId() {
    std::string id;
    thread_->Invoke([this, &id]() {
        id = CurrentPage()->pageId;
    });
    return id;
}

std::string QtBoardData::CreatePage() {
    std::string id;
    thread_->Invoke([this, &id]() {
        auto page = std::make_shared<whiteboard::Page>();
        page->pageId = GeneratePageId();
        page->EnableEraserInsert(true);
        pages_.push_back(page);
        currentPage_ = pages_.size() - 1;
        ResetTransientState();
        id = page->pageId;

        auto cmd = std::make_shared<whiteboard::PageCreate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->newPageId = id;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
    return id;
}

void QtBoardData::SelectPage(const std::string& pageId) {
    thread_->Invoke([this, pageId]() {
        for (size_t i = 0; i < pages_.size(); ++i) {
            if (pages_[i]->pageId != pageId)
                continue;
            currentPage_ = i;
            ResetTransientState();

            auto cmd = std::make_shared<whiteboard::PageSelect>();
            cmd->pageId = pageId;
            if (onOutgoingCmd_)
                onOutgoingCmd_(std::move(cmd));
            return;
        }
    });
}

bool QtBoardData::DeletePage(const std::string& pageId) {
    bool ok = false;
    thread_->Invoke([this, pageId, &ok]() {
        if (pages_.size() <= 1)
            return;
        for (size_t i = 0; i < pages_.size(); ++i) {
            if (pages_[i]->pageId != pageId)
                continue;
            pages_.erase(pages_.begin() + i);
            histories_.erase(pageId);  // 页面删除后历史随之丢弃
            if (currentPage_ >= pages_.size())
                currentPage_ = pages_.size() - 1;
            ResetTransientState();

            auto cmd = std::make_shared<whiteboard::PageDelete>();
            cmd->pageId = pageId;
            if (onOutgoingCmd_)
                onOutgoingCmd_(std::move(cmd));
            ok = true;
            return;
        }
    });
    return ok;
}

// ---------- 远程笔画应用（预览 + 落库，不产生命令） ----------

void QtBoardData::RemoteStrokeBegin(const std::string& strokeId, int width, uint32_t color,
                                    const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, strokeId, width, color, points]() {
        PushSnapshot();
        whiteboard::Stroke& s = strokes_[RemoteStrokeKey(strokeId)];
        s.Reset();
        s.id = strokeId;  // 使用发送端分配的 id，保证双方一致
        s.width = width;
        s.color = color;
        ReplaceStrokePoints(s, points);
        if (onStrokePreview_)
            onStrokePreview_(strokeId, color, width, points);
    });
}

void QtBoardData::RemoteStrokeMove(const std::string& strokeId,
                                   const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, strokeId, points]() {
        auto it = strokes_.find(RemoteStrokeKey(strokeId));
        if (it == strokes_.end())
            return;
        ReplaceStrokePoints(it->second, points);
        if (onStrokePreview_)
            onStrokePreview_(strokeId, it->second.color, it->second.width, points);
    });
}

void QtBoardData::RemoteStrokeEnd(const std::string& strokeId,
                                  const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, strokeId, points]() {
        auto it = strokes_.find(RemoteStrokeKey(strokeId));
        if (it == strokes_.end())
            return;
        ReplaceStrokePoints(it->second, points);
        CurrentPage()->Append(it->second);
        auto stroke = std::make_shared<whiteboard::Stroke>(it->second);
        strokes_.erase(it);
        if (onElementsChanged_) {
            std::vector<std::string> removed;
            std::vector<std::shared_ptr<whiteboard::Element>> added = { stroke };
            onElementsChanged_(std::move(removed), std::move(added));
        }
    });
}

// ---------- 远程橡皮应用（操作端为主：Begin/Move 仅 UI 预览，End 同步数据层） ----------

void QtBoardData::RemoteEraserBegin(const std::string& sessionId,
                                    const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, sessionId, points]() {
        // 数据层不动：仅触发工具预览回调，由远端 UI 实时视觉擦除
        if (onToolPreview_)
            onToolPreview_(1, sessionId, points, {});
    });
}

void QtBoardData::RemoteEraserMove(const std::string& sessionId,
                                   const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, sessionId, points]() {
        if (onToolPreview_)
            onToolPreview_(1, sessionId, points, {});
    });
}

void QtBoardData::RemoteEraserEnd(
    const std::string& sessionId,
    const std::vector<std::string>& removedIds,
    const std::vector<std::shared_ptr<whiteboard::Element>>& addedElements) {
    thread_->BeginInvoke([this, sessionId, removedIds, addedElements]() {
        PushSnapshot();  // 一次远程擦除拖动记录一个快照
        auto& elems = CurrentPage()->elements;
        for (const auto& id : removedIds) {
            elems.erase(std::remove_if(elems.begin(), elems.end(),
                                       [&id](const std::shared_ptr<whiteboard::Element>& e) {
                                           return e->id == id;
                                       }),
                        elems.end());
        }
        for (const auto& e : addedElements)
            elems.push_back(e);

        // 清除橡皮预览（UI 先恢复被隐藏的图元，再按下方增量精确删/加）
        if (onToolPreview_)
            onToolPreview_(1, sessionId, {}, {});
        if (onElementsChanged_ && (!removedIds.empty() || !addedElements.empty()))
            onElementsChanged_(removedIds, addedElements);
    });
}

void QtBoardData::RemoteLassoPreview(const std::string& sessionId,
                                     const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, sessionId, points]() {
        if (onToolPreview_)
            onToolPreview_(2, sessionId, points, {});
    });
}

void QtBoardData::RemoteSelectionPreview(const std::string& sessionId,
                                         const std::vector<whiteboard::Point>& points,
                                         const std::vector<std::string>& selectedIds) {
    thread_->BeginInvoke([this, sessionId, points, selectedIds]() {
        if (onToolPreview_)
            onToolPreview_(3, sessionId, points, selectedIds);
    });
}

// ---------- 本地工具预览命令（套索/选择，供远端 UI 实时展示） ----------

void QtBoardData::SendToolPreview(uint32_t tool, const std::string& sessionId,
                                  const std::vector<whiteboard::Point>& points,
                                  const std::vector<std::string>& elementIds) {
    thread_->BeginInvoke([this, tool, sessionId, points, elementIds]() {
        std::shared_ptr<whiteboard::Command> cmd;
        if (tool == 2) {
            auto c = std::make_shared<whiteboard::LassoPreview>();
            c->pageId = CurrentPage()->pageId;
            c->sessionId = sessionId;
            c->points = points;
            cmd = std::move(c);
        } else if (tool == 3) {
            auto c = std::make_shared<whiteboard::SelectionPreview>();
            c->pageId = CurrentPage()->pageId;
            c->sessionId = sessionId;
            c->points = points;
            c->selectedIds = elementIds;
            cmd = std::move(c);
        }
        if (cmd && onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// ---------- 远程其他操作（触发既有回调，不产生命令） ----------

void QtBoardData::RemoteUpdateStroke(const std::string& id,
                                     const std::vector<whiteboard::Point>& points) {
    thread_->BeginInvoke([this, id, points]() {
        PushSnapshot();
        CurrentPage()->UpdateStroke(id, points);
        // 找到更新后的笔画并作为 added 增量回调（removed={id} 保证 UI 先删旧图元）
        std::shared_ptr<whiteboard::Element> updated;
        for (const auto& e : CurrentPage()->elements) {
            if (e->id == id && dynamic_cast<const whiteboard::Stroke*>(e.get())) {
                updated = std::make_shared<whiteboard::Stroke>(
                    *static_cast<const whiteboard::Stroke*>(e.get()));
                break;
            }
        }
        if (onElementsChanged_) {
            std::vector<std::string> removed = { id };
            std::vector<std::shared_ptr<whiteboard::Element>> added;
            if (updated)
                added.push_back(updated);
            onElementsChanged_(std::move(removed), std::move(added));
        }
    });
}

void QtBoardData::RemoteClear() {
    thread_->BeginInvoke([this]() {
        PushSnapshot();
        CurrentPage()->Clear();
        ResetTransientState();
        if (onCleared_)
            onCleared_();
    });
}

void QtBoardData::RemoteCreatePage(const std::string& pageId) {
    thread_->BeginInvoke([this, pageId]() {
        // 已存在同 id 页面（FullSync 已包含）则仅切换
        for (size_t i = 0; i < pages_.size(); ++i) {
            if (pages_[i]->pageId != pageId)
                continue;
            currentPage_ = i;
            ResetTransientState();
            if (onPageChanged_)
                onPageChanged_();
            return;
        }
        auto page = std::make_shared<whiteboard::Page>();
        page->pageId = pageId;
        page->EnableEraserInsert(true);
        pages_.push_back(page);
        currentPage_ = pages_.size() - 1;
        ResetTransientState();
        if (onPageChanged_)
            onPageChanged_();
    });
}

void QtBoardData::RemoteSelectPage(const std::string& pageId) {
    thread_->BeginInvoke([this, pageId]() {
        for (size_t i = 0; i < pages_.size(); ++i) {
            if (pages_[i]->pageId != pageId)
                continue;
            currentPage_ = i;
            ResetTransientState();
            if (onPageChanged_)
                onPageChanged_();
            return;
        }
    });
}

void QtBoardData::RemoteDeletePage(const std::string& pageId) {
    thread_->BeginInvoke([this, pageId]() {
        if (pages_.size() <= 1)
            return;
        for (size_t i = 0; i < pages_.size(); ++i) {
            if (pages_[i]->pageId != pageId)
                continue;
            pages_.erase(pages_.begin() + i);
            histories_.erase(pageId);
            if (currentPage_ >= pages_.size())
                currentPage_ = pages_.size() - 1;
            ResetTransientState();
            if (onPageChanged_)
                onPageChanged_();
            return;
        }
    });
}

void QtBoardData::RemoteUndo() {
    thread_->BeginInvoke([this]() {
        auto& h = histories_[CurrentPage()->pageId];
        if (h.undo.empty())
            return;
        h.redo.push_front(CurrentPage()->Clone());
        while (h.redo.size() > kHistoryLimit)
            h.redo.pop_back();
        CurrentPage() = h.undo.back();
        h.undo.pop_back();
        ResetTransientState();
        if (onPageChanged_)
            onPageChanged_();
    });
}

void QtBoardData::RemoteRedo() {
    thread_->BeginInvoke([this]() {
        auto& h = histories_[CurrentPage()->pageId];
        if (h.redo.empty())
            return;
        h.undo.push_back(CurrentPage()->Clone());
        while (h.undo.size() > kHistoryLimit)
            h.undo.pop_front();
        CurrentPage() = h.redo.front();
        h.redo.pop_front();
        ResetTransientState();
        if (onPageChanged_)
            onPageChanged_();
    });
}

// ---------- 全量同步 ----------

void QtBoardData::ApplyFullSync(const std::vector<whiteboard::Page>& pages) {
    thread_->BeginInvoke([this, pages]() {
        pages_.clear();
        for (const auto& p : pages) {
            auto page = p.Clone();  // 深拷贝，避免与发送方共享元素
            page->EnableEraserInsert(true);  // 兜底：保证远程页面橡皮可用
            pages_.push_back(page);
        }
        if (pages_.empty())
            pages_.push_back(std::make_shared<whiteboard::Page>());
        currentPage_ = 0;
        histories_.clear();
        ResetTransientState();
        if (onSynced_)
            onSynced_();
    });
}

void QtBoardData::GetAllPages(std::vector<whiteboard::Page>& pages) {
    thread_->Invoke([this, &pages]() {
        pages.clear();
        pages.reserve(pages_.size());
        for (auto& p : pages_)
            pages.push_back(*(p->Clone()));  // 深拷贝：返回后数据线程可继续并行修改
    });
}

void QtBoardData::SetColor(uint32_t color) {
    color_.store(color);
}

void QtBoardData::SetPenWidth(int width) {
    if (width > 0)
        penWidth_.store(width);
}
