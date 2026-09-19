#include "QtBoardData.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "Whiteboard/command/element_commands.h"
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

// ---------- 元素归属/递归定位助手（页面级 + 表格单元格，任意深度） ----------

// 递归收集表格的全部后代 id（深度优先，不含表格自身）
void CollectTableChildIds(const std::shared_ptr<whiteboard::Element>& e,
                          std::vector<std::string>& out) {
    auto* table = dynamic_cast<whiteboard::TableElement*>(e.get());
    if (!table)
        return;
    for (auto& cell : table->cells) {
        for (auto& child : cell) {
            if (!child)
                continue;
            out.push_back(child->id);
            CollectTableChildIds(child, out);
        }
    }
}

// 递归查找 id 对应元素指针（只读/写回均可用；未找到返回 nullptr）
whiteboard::Element* FindElementInList(
    const std::list<std::shared_ptr<whiteboard::Element>>& list,
    const std::string& id) {
    for (const auto& e : list) {
        if (!e)
            continue;
        if (e->id == id)
            return e.get();
        if (auto* table = dynamic_cast<whiteboard::TableElement*>(e.get())) {
            for (auto& cell : table->cells) {
                if (auto* found = FindElementInList(cell, id))
                    return found;
            }
        }
    }
    return nullptr;
}

// 递归删除单个 id（页面级 / 表格单元格内）；删除表格时先把全部后代 id 收集到 cascaded。
// 返回是否有删除发生。
bool RemoveElementById(std::list<std::shared_ptr<whiteboard::Element>>& list,
                       const std::string& id, std::vector<std::string>* cascaded) {
    for (auto it = list.begin(); it != list.end(); ++it) {
        if ((*it)->id == id) {
            if (cascaded && dynamic_cast<whiteboard::TableElement*>(it->get()))
                CollectTableChildIds(*it, *cascaded);
            list.erase(it);
            return true;
        }
        if (auto* table = dynamic_cast<whiteboard::TableElement*>(it->get())) {
            for (auto& cell : table->cells) {
                if (RemoveElementById(cell, id, cascaded))
                    return true;
            }
        }
    }
    return false;
}

// 按 parentId + cellIndex 把元素落位到指定表格单元格（任意深度）；成功返回 true
bool PlaceInTableById(std::list<std::shared_ptr<whiteboard::Element>>& list,
                      const std::string& parentId, int cellIndex,
                      const std::shared_ptr<whiteboard::Element>& element) {
    for (auto& e : list) {
        if (e->id == parentId) {
            auto* table = dynamic_cast<whiteboard::TableElement*>(e.get());
            if (!table || cellIndex < 0 || cellIndex >= static_cast<int>(table->cells.size()))
                return false;
            table->cells[cellIndex].push_back(element);
            return true;
        }
        if (auto* table = dynamic_cast<whiteboard::TableElement*>(e.get())) {
            for (auto& cell : table->cells) {
                if (PlaceInTableById(cell, parentId, cellIndex, element))
                    return true;
            }
        }
    }
    return false;
}

// 页面坐标 → 格局部坐标：绕布局中心逆旋转（rotation 为 Qt 顺时针正角）
// 后平移 −(格原点)。与渲染层表帧（绕中心旋转 ∘ 平移格原点）互为逆映射。
whiteboard::Point PageToCellLocal(const whiteboard::TableElement& table,
                                  const whiteboard::TableElement::Layout& layout,
                                  int cellIndex, double px, double py) {
    double cellOx = 0.0;
    double cellOy = 0.0;
    layout.CellOrigin(cellIndex, cellOx, cellOy);
    const double hw = layout.totalW / 2.0;
    const double hh = layout.totalH / 2.0;
    // 相对布局中心（未旋转表内方向）
    double rx = px - table.origin.x - hw;
    double ry = py - table.origin.y - hh;
    if (std::abs(table.rotation) > 1e-6f) {
        const double rad = table.rotation * 3.14159265358979323846 / 180.0;
        const double cs = std::cos(rad);
        const double sn = std::sin(rad);
        const double nx = rx * cs + ry * sn;
        const double ny = -rx * sn + ry * cs;
        rx = nx;
        ry = ny;
    }
    return whiteboard::Point(static_cast<int>(std::lround(rx + hw - cellOx)),
                             static_cast<int>(std::lround(ry + hh - cellOy)));
}

// 几何入格转换：把元素几何从页面坐标映射为格局部坐标（保持屏幕视觉不变）。
// 笔迹：点集/原始点集重映射并重算包围盒；图形：子路径点集重映射 + Rebuild；
// 文字：锚点映射 + bounds 同步平移 + rotation 减去表角（屏幕视觉不变）。
void ConvertGeometryToCellLocal(const whiteboard::TableElement& table,
                                const whiteboard::TableElement::Layout& layout,
                                int cellIndex, whiteboard::Element& element) {
    if (auto* stroke = dynamic_cast<whiteboard::Stroke*>(&element)) {
        for (auto& p : stroke->points)
            p = PageToCellLocal(table, layout, cellIndex, p.x, p.y);
        for (auto& p : stroke->rawPoints)
            p = PageToCellLocal(table, layout, cellIndex, p.x, p.y);
        stroke->bounding = whiteboard::BoundaryRect();
        for (const auto& p : stroke->points)
            stroke->bounding.Update(p.x, p.y);
        return;
    }
    if (auto* graphic = dynamic_cast<whiteboard::GraphicElement*>(&element)) {
        for (auto& sp : graphic->subpaths) {
            for (auto& p : sp.points)
                p = PageToCellLocal(table, layout, cellIndex, p.x, p.y);
        }
        graphic->Rebuild();
        return;
    }
    if (auto* text = dynamic_cast<whiteboard::TextElement*>(&element)) {
        const whiteboard::Point anchor =
            PageToCellLocal(table, layout, cellIndex, text->x, text->y);
        const int dx = anchor.x - text->x;
        const int dy = anchor.y - text->y;
        text->x = anchor.x;
        text->y = anchor.y;
        text->bounds.x += dx;
        text->bounds.y += dy;
        // 表旋转并入元素自身角度：保持屏幕视觉不变
        text->rotation -= table.rotation;
        return;
    }
}

// 递归查找 id 所在单元格（表格子元素）：返回所属表格 id 与格索引（未找到 false）
bool FindCellOwnerInList(const std::list<std::shared_ptr<whiteboard::Element>>& list,
                         const std::string& id, std::string& parentId, int& cellIndex) {
    for (const auto& e : list) {
        auto* table = dynamic_cast<whiteboard::TableElement*>(e.get());
        if (!table)
            continue;
        for (size_t ci = 0; ci < table->cells.size(); ++ci) {
            for (const auto& child : table->cells[ci]) {
                if (child && child->id == id) {
                    parentId = table->id;
                    cellIndex = static_cast<int>(ci);
                    return true;
                }
            }
        }
        for (const auto& cell : table->cells) {
            if (FindCellOwnerInList(cell, id, parentId, cellIndex))
                return true;
        }
    }
    return false;
}

// 判定点归属：若判定点落在某表格单元格内，把元素放入该单元格并返回 true
// （双端确定性重放的通用归属助手）。仅 笔迹/图形/文字 可入格（白名单；
// 导图/小工具/表格不入格），命中后几何整体转格局部坐标；嵌套递归保留。
// out 非空时回填归属位置。
bool AdoptIntoTableInList(std::list<std::shared_ptr<whiteboard::Element>>& list,
                          const whiteboard::Point& start,
                          const std::shared_ptr<whiteboard::Element>& element,
                          whiteboard::EraserPlacement* out) {
    const bool eligible = dynamic_cast<whiteboard::Stroke*>(element.get()) ||
                          dynamic_cast<whiteboard::GraphicElement*>(element.get()) ||
                          dynamic_cast<whiteboard::TextElement*>(element.get());
    if (!eligible)
        return false;

    for (auto& e : list) {
        auto* table = dynamic_cast<whiteboard::TableElement*>(e.get());
        if (!table)
            continue;
        const int ci = table->CellIndexAt(start);
        if (ci >= 0 && ci < static_cast<int>(table->cells.size())) {
            // 几何转格局部（布局取入格前状态，与判定一致）
            ConvertGeometryToCellLocal(*table, table->ComputeLayout(), ci, *element);
            table->cells[ci].push_back(element);
            if (out) {
                out->parentId = table->id;
                out->cellIndex = ci;
            }
            return true;
        }
        // 外层未命中：深入单元格内的嵌套表格
        for (auto& cell : table->cells) {
            if (AdoptIntoTableInList(cell, start, element, out))
                return true;
        }
    }
    return false;
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
    if (batchSuppress_)
        return;  // 批量操作中：快照已在 BeginBatch 时记录
    auto& h = histories_[CurrentPage()->pageId];
    h.undo.push_back(CurrentPage()->Clone());
    while (h.undo.size() > kHistoryLimit)
        h.undo.pop_front();
    h.redo.clear();
}

// ---------- 批量快照（撤销粒度合并） ----------
// BeginBatch：首个调用产生一次快照并进入抑制态；嵌套调用保持已有快照不重复。
void QtBoardData::BeginBatch() {
    thread_->BeginInvoke([this]() {
        if (batchSuppress_)
            return;
        PushSnapshot();
        batchSuppress_ = true;
    });
}

void QtBoardData::EndBatch() {
    thread_->BeginInvoke([this]() { batchSuppress_ = false; });
}

// 丢弃未完成笔画与橡皮增量（切页/撤销/重做/同步后调用）。仅数据线程调用。
void QtBoardData::ResetTransientState() {
    strokes_.clear();
    eraserRemoved_.clear();
    eraserAdded_.clear();
    eraserAddedPlacements_.clear();
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
        auto stroke = std::make_shared<whiteboard::Stroke>(it->second);
        const std::string id = it->second.id;
        strokes_.erase(it);

        // 起点落在某表格单元格内则归属该单元格（双端确定性重放），否则页面级追加
        whiteboard::EraserPlacement placement;
        if (!pts.empty() && AdoptIntoTableInList(CurrentPage()->elements, pts.front(), stroke, &placement))
        { /* 已入格 */ }
        else
            CurrentPage()->Append(stroke);

        auto cmd = std::make_shared<whiteboard::StrokeEnd>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->strokeId = id;
        cmd->points = pts;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));

        if (onStrokeCommitted_)
            onStrokeCommitted_(pendingToken_, id, placement.parentId, placement.cellIndex);
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
        eraserAddedPlacements_.insert(eraserAddedPlacements_.end(),
                                      r.addedPlacements.begin(), r.addedPlacements.end());

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
        eraserAddedPlacements_.insert(eraserAddedPlacements_.end(),
                                      r.addedPlacements.begin(), r.addedPlacements.end());

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
        eraserAddedPlacements_.insert(eraserAddedPlacements_.end(),
                                      r.addedPlacements.begin(), r.addedPlacements.end());

        // 收集本次拖动全部增量（含最终回调），作为 EraserEnd 命令内容
        auto result = FlushEraser(true);

        auto cmd = std::make_shared<whiteboard::EraserEnd>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->sessionId = sid;
        cmd->removedIds = std::move(result.removedIds);
        cmd->addedElements = std::move(result.addedElements);
        cmd->addedPlacements = std::move(result.addedPlacements);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 收集橡皮增量：过滤已被后续擦除删除的碎片后回调；clearAccum 为 true 时清空累积。
// 返回过滤结果（供 EraserEnd 命令负载；addedPlacements 与 addedElements 对齐）。
QtBoardData::EraserDelta QtBoardData::FlushEraser(bool clearAccum) {
    EraserDelta delta;
    delta.removedIds.assign(eraserRemoved_.begin(), eraserRemoved_.end());
    for (size_t i = 0; i < eraserAdded_.size(); ++i) {
        auto& e = eraserAdded_[i];
        // 碎片可能在后续擦除中又被删掉，以 removed 为准过滤
        if (eraserRemoved_.count(e->id) != 0)
            continue;
        delta.addedElements.push_back(e);
        if (i < eraserAddedPlacements_.size())
            delta.addedPlacements.push_back(eraserAddedPlacements_[i]);
        else
            delta.addedPlacements.push_back(whiteboard::EraserPlacement());  // 防御：无归属记录视为页面级
    }
    if (clearAccum) {
        eraserRemoved_.clear();
        eraserAdded_.clear();
        eraserAddedPlacements_.clear();
    }
    if (onElementsChanged_)
        onElementsChanged_(delta.removedIds, delta.addedElements, delta.addedPlacements);
    return delta;
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

// 按 id 深拷贝元素快照（同步；表格整表重建用）。找不到返回 nullptr。
std::shared_ptr<whiteboard::Element> QtBoardData::GetElementSnapshot(const std::string& id) {
    std::shared_ptr<whiteboard::Element> snapshot;
    thread_->Invoke([this, &snapshot, &id]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        if (found)
            snapshot = whiteboard::CloneElement(*found);
    });
    return snapshot;
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

// ---------- 新增元素（图形/思维导图/表格）：本地操作 ----------

void QtBoardData::AddElement(std::shared_ptr<whiteboard::Element> element) {
    thread_->BeginInvoke([this, element]() {
        if (!element)
            return;
        PushSnapshot();

        // 图形（拖拽矩形中心判定）/文字（锚点判定）尝试入格；笔迹走 PenEnd
        // 专属路径；表格/导图/小工具不入格（白名单在归属助手内）
        whiteboard::EraserPlacement placement;
        whiteboard::Point anchor;
        bool hasAnchor = false;
        if (auto* graphic = dynamic_cast<whiteboard::GraphicElement*>(element.get())) {
            const whiteboard::Rect br = graphic->bounding.ToRect();
            anchor = whiteboard::Point(br.x + br.width / 2, br.y + br.height / 2);
            hasAnchor = true;
        } else if (auto* text = dynamic_cast<whiteboard::TextElement*>(element.get())) {
            anchor = whiteboard::Point(text->x, text->y);
            hasAnchor = true;
        }
        const bool adopted = hasAnchor &&
            AdoptIntoTableInList(CurrentPage()->elements, anchor, element, &placement);

        if (adopted) {
            // 入格：对远端广播整表快照（避免对端把子元素按页面级误置）
            auto* found = FindElementInList(CurrentPage()->elements, placement.parentId);
            auto* table = dynamic_cast<whiteboard::TableElement*>(found);
            auto snapshot = table ? whiteboard::CloneElement(*table) : nullptr;
            if (snapshot) {
                auto cmd = std::make_shared<whiteboard::ElementUpdate>();
                cmd->pageId = CurrentPage()->pageId;
                cmd->element = std::move(snapshot);
                if (onOutgoingCmd_)
                    onOutgoingCmd_(std::move(cmd));
            }
        } else {
            CurrentPage()->Append(element);
            auto cmd = std::make_shared<whiteboard::ElementAdd>();
            cmd->pageId = CurrentPage()->pageId;
            cmd->element = element;
            if (onOutgoingCmd_)
                onOutgoingCmd_(std::move(cmd));
        }

        if (onElementsChanged_) {
            std::vector<std::string> removed;
            std::vector<std::shared_ptr<whiteboard::Element>> added = { element };
            std::vector<whiteboard::EraserPlacement> placements;
            if (adopted)
                placements.push_back(placement);  // 与 added 对齐（空 = 页面级）
            onElementsChanged_(std::move(removed), std::move(added), std::move(placements));
        }
    });
}

void QtBoardData::RemoveElements(const std::vector<std::string>& ids) {
    thread_->BeginInvoke([this, ids]() {
        if (ids.empty())
            return;
        // 先探测任一 id 是否存在（不存在则不产生快照/命令/回调）
        bool any = false;
        for (const auto& id : ids) {
            if (FindElementInList(CurrentPage()->elements, id)) {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        PushSnapshot();

        std::vector<std::string> removedAll;
        std::vector<std::string> broadcastIds;
        for (const auto& id : ids) {
            std::vector<std::string> cascaded;
            if (RemoveElementById(CurrentPage()->elements, id, &cascaded)) {
                broadcastIds.push_back(id);
                removedAll.push_back(id);
                removedAll.insert(removedAll.end(), cascaded.begin(), cascaded.end());
            }
        }

        // 每个顶层 id 广播一次 ElementRemove（表格由远端递归删除时自行级联）
        for (const auto& id : broadcastIds) {
            auto cmd = std::make_shared<whiteboard::ElementRemove>();
            cmd->pageId = CurrentPage()->pageId;
            cmd->elementId = id;
            if (onOutgoingCmd_)
                onOutgoingCmd_(std::move(cmd));
        }

        if (!removedAll.empty() && onElementsChanged_)
            onElementsChanged_(removedAll, {}, {});
    });
}

void QtBoardData::UpdateGraphicGeometry(const std::string& id,
                                        const std::vector<whiteboard::Subpath>& subpaths) {
    thread_->BeginInvoke([this, id, subpaths]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* graphic = dynamic_cast<whiteboard::GraphicElement*>(found);
        if (!graphic)
            return;
        PushSnapshot();  // 变换烘焙是一次可撤销操作
        graphic->subpaths = subpaths;
        graphic->Rebuild();

        // 静默写回：不触发 onElementsChanged（UI 已自行更新），仅广播给远端
        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::GraphicElement>(*graphic);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 整表静默写回（变换烘焙用）：origin/rotation/初始格尺寸 + 全部子元素整组替换。
// UI 已本地重建保证视觉零跳变（不触发 onElementsChanged），仅广播远端。
void QtBoardData::UpdateTableElement(std::shared_ptr<whiteboard::TableElement> table) {
    thread_->BeginInvoke([this, table]() {
        if (!table)
            return;
        auto* found = FindElementInList(CurrentPage()->elements, table->id);
        auto* current = dynamic_cast<whiteboard::TableElement*>(found);
        if (!current)
            return;
        PushSnapshot();  // 变换烘焙是一次可撤销操作（批内由 BeginBatch/EndBatch 合并）
        current->origin = table->origin;
        current->rotation = table->rotation;
        current->rows = table->rows;
        current->cols = table->cols;
        current->width = table->width;
        current->color = table->color;
        current->minCellW = table->minCellW;
        current->minCellH = table->minCellH;
        current->cells = table->cells;  // 子元素整组替换（旧子元素随之释放）

        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = whiteboard::CloneElement(*current);  // 深拷贝：远端序列化脱离活对象
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// ---------- 文字元素 ----------

void QtBoardData::UpdateTextGeometry(const std::string& id, int x, int y, int fontSize,
                                     float rotation) {
    thread_->BeginInvoke([this, id, x, y, fontSize, rotation]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* text = dynamic_cast<whiteboard::TextElement*>(found);
        if (!text)
            return;
        PushSnapshot();  // 变换烘焙是一次可撤销操作
        text->x = x;
        text->y = y;
        text->fontSize = fontSize;
        text->rotation = rotation;

        // 静默写回：不触发 onElementsChanged（UI 已自行更新），仅广播给远端
        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::TextElement>(*text);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

void QtBoardData::UpdateTextContent(const std::string& id, const std::string& text,
                                    const whiteboard::Rect& bounds) {
    thread_->BeginInvoke([this, id, text, bounds]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* element = dynamic_cast<whiteboard::TextElement*>(found);
        if (!element)
            return;
        PushSnapshot();  // 内容编辑是一次可撤销操作
        element->text = text;
        element->bounds = bounds;  // UI 重算的字形包围盒（布局依赖）

        // 格内文本：布局可能变化，改广播整表快照（added = 整表，UI 重建整表）
        std::string parentId;
        int cellIndex = -1;
        if (FindCellOwnerInList(CurrentPage()->elements, id, parentId, cellIndex)) {
            auto* owner = FindElementInList(CurrentPage()->elements, parentId);
            auto* table = dynamic_cast<whiteboard::TableElement*>(owner);
            auto snapshot = table ? whiteboard::CloneElement(*table) : nullptr;
            if (!snapshot)
                return;
            auto cmd = std::make_shared<whiteboard::ElementUpdate>();
            cmd->pageId = CurrentPage()->pageId;
            cmd->element = snapshot;
            if (onOutgoingCmd_)
                onOutgoingCmd_(std::move(cmd));
            if (onElementsChanged_) {
                std::vector<std::string> removed = { id };
                std::vector<std::shared_ptr<whiteboard::Element>> added = { snapshot };
                onElementsChanged_(std::move(removed), std::move(added), {});
            }
            return;
        }

        // 页面级文本：深拷贝快照（脱离数据线程活对象）+ removed/added 重建路径，
        // UI 与远端走同一路径（复用选中态恢复逻辑）
        auto snapshot = whiteboard::CloneElement(*element);
        if (!snapshot)
            return;
        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = snapshot;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
        if (onElementsChanged_) {
            std::vector<std::string> removed = { id };
            std::vector<std::shared_ptr<whiteboard::Element>> added = { snapshot };
            onElementsChanged_(std::move(removed), std::move(added), {});
        }
    });
}

// ---------- 小工具元素 ----------

void QtBoardData::UpdateWidgetGeometry(const std::string& id, int x, int y, float scale) {
    thread_->BeginInvoke([this, id, x, y, scale]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* widget = dynamic_cast<whiteboard::WidgetElement*>(found);
        if (!widget)
            return;
        PushSnapshot();  // 变换烘焙是一次可撤销操作
        widget->x = x;
        widget->y = y;
        widget->scale = scale;

        // 静默写回：不触发 onElementsChanged（UI 已自行更新），仅广播给远端
        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::WidgetElement>(*widget);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 计时器时长写回（静默：不入撤销栈——滚轮逐格调整会刷爆历史；点开始时一次提交）
void QtBoardData::UpdateWidgetDuration(const std::string& id, int durationSec) {
    thread_->BeginInvoke([this, id, durationSec]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* widget = dynamic_cast<whiteboard::WidgetElement*>(found);
        if (!widget)
            return;
        widget->durationSec = durationSec < 1 ? 1 : durationSec;

        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::WidgetElement>(*widget);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 骰子参数写回（静默：设置态点确定时一次提交，不入撤销栈）
void QtBoardData::UpdateWidgetDiceParams(const std::string& id, int sides, int count) {
    thread_->BeginInvoke([this, id, sides, count]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* widget = dynamic_cast<whiteboard::WidgetElement*>(found);
        if (!widget)
            return;
        widget->diceSides = sides;
        widget->diceCount = count < 1 ? 1 : (count > 10 ? 10 : count);

        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::WidgetElement>(*widget);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 转盘/点名器候选项写回（编辑弹窗提交）：PushSnapshot → 改 options → 深拷贝
// 快照广播 + removed/added 重建路径，UI 与远端走同一路径（可撤销）
void QtBoardData::UpdateWidgetOptions(const std::string& id, const std::string& options) {
    thread_->BeginInvoke([this, id, options]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* widget = dynamic_cast<whiteboard::WidgetElement*>(found);
        if (!widget)
            return;
        PushSnapshot();  // 内容编辑是一次可撤销操作
        widget->options = options;

        auto snapshot = whiteboard::CloneElement(*widget);
        if (!snapshot)
            return;
        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = snapshot;
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
        if (onElementsChanged_) {
            std::vector<std::string> removed = { id };
            std::vector<std::shared_ptr<whiteboard::Element>> added = { snapshot };
            onElementsChanged_(std::move(removed), std::move(added), {});
        }
    });
}

// 去重模式开关写回（静默：不入撤销栈）
void QtBoardData::UpdateWidgetDedup(const std::string& id, bool dedup) {
    thread_->BeginInvoke([this, id, dedup]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* widget = dynamic_cast<whiteboard::WidgetElement*>(found);
        if (!widget)
            return;
        widget->dedup = dedup;

        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::WidgetElement>(*widget);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// 点名器一次抽取人数写回（静默：不入撤销栈；范围 1~5）
void QtBoardData::UpdateWidgetPickCount(const std::string& id, int pickCount) {
    thread_->BeginInvoke([this, id, pickCount]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* widget = dynamic_cast<whiteboard::WidgetElement*>(found);
        if (!widget)
            return;
        widget->pickCount = pickCount < 1 ? 1 : (pickCount > 5 ? 5 : pickCount);

        auto cmd = std::make_shared<whiteboard::ElementUpdate>();
        cmd->pageId = CurrentPage()->pageId;
        cmd->element = std::make_shared<whiteboard::WidgetElement>(*widget);
        if (onOutgoingCmd_)
            onOutgoingCmd_(std::move(cmd));
    });
}

// ---------- 思维导图（树状结构操作） ----------

// 变更收尾：深拷贝快照（含节点树，脱离数据线程活对象，防远端序列化/UI 读取竞态），
// 广播 ElementUpdate + removed={id}/added={快照} 增量回调，UI 与远端同一重建路径。
void QtBoardData::NotifyMindMapChanged(const whiteboard::MindMapElement& mind) {
    auto snapshot = whiteboard::CloneElement(mind);
    if (!snapshot)
        return;

    auto cmd = std::make_shared<whiteboard::ElementUpdate>();
    cmd->pageId = CurrentPage()->pageId;
    cmd->element = snapshot;
    if (onOutgoingCmd_)
        onOutgoingCmd_(std::move(cmd));

    if (onElementsChanged_) {
        std::vector<std::string> removed = { mind.id };
        std::vector<std::shared_ptr<whiteboard::Element>> added = { snapshot };
        onElementsChanged_(std::move(removed), std::move(added), {});
    }
}

void QtBoardData::UpdateMindMapGeometry(const std::string& id, const whiteboard::Point& root,
                                        float scaleX, float scaleY, float rotation) {
    thread_->BeginInvoke([this, id, root, scaleX, scaleY, rotation]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* mind = dynamic_cast<whiteboard::MindMapElement*>(found);
        if (!mind)
            return;
        PushSnapshot();  // 变换烘焙是一次可撤销操作
        mind->root = root;
        mind->scaleX = scaleX;
        mind->scaleY = scaleY;
        mind->rotation = rotation;
        NotifyMindMapChanged(*mind);
    });
}

void QtBoardData::MindMapToggleNode(const std::string& id, const std::string& nodeId) {
    thread_->BeginInvoke([this, id, nodeId]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* mind = dynamic_cast<whiteboard::MindMapElement*>(found);
        if (!mind)
            return;
        auto* node = mind->FindNode(nodeId);
        if (!node)
            return;
        PushSnapshot();  // 展开/收缩是一次可撤销操作
        node->collapsed = !node->collapsed;
        NotifyMindMapChanged(*mind);
    });
}

void QtBoardData::MindMapAddChild(const std::string& id, const std::string& nodeId) {
    thread_->BeginInvoke([this, id, nodeId]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* mind = dynamic_cast<whiteboard::MindMapElement*>(found);
        if (!mind)
            return;
        auto* node = mind->FindNode(nodeId);
        if (!node)
            return;
        PushSnapshot();  // 添加节点是一次可撤销操作
        auto child = std::make_shared<whiteboard::MindNode>();
        child->id = whiteboard::MakeMindNodeId();
        node->children.push_back(child);
        node->collapsed = false;  // 父节点若处于折叠态则展开，确保新节点可见
        NotifyMindMapChanged(*mind);
    });
}

void QtBoardData::MindMapRemoveNode(const std::string& id, const std::string& nodeId) {
    thread_->BeginInvoke([this, id, nodeId]() {
        auto* found = FindElementInList(CurrentPage()->elements, id);
        auto* mind = dynamic_cast<whiteboard::MindMapElement*>(found);
        if (!mind)
            return;
        auto* parent = mind->FindParent(nodeId);  // 根节点返回 nullptr → 拒绝
        if (!parent)
            return;
        PushSnapshot();  // 删除节点子树是一次可撤销操作
        auto& children = parent->children;
        children.erase(std::remove_if(children.begin(), children.end(),
                                          [&nodeId](const std::shared_ptr<whiteboard::MindNode>& c) {
                                              return c && c->id == nodeId;
                                          }),
                       children.end());
        NotifyMindMapChanged(*mind);
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
        // 新页插入到当前页之后（第 N 页添加时新页成为第 N+1 页），而非追加到末尾
        pages_.insert(pages_.begin() + currentPage_ + 1, page);
        currentPage_ += 1;
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
        auto stroke = std::make_shared<whiteboard::Stroke>(it->second);
        strokes_.erase(it);

        // 与本地 PenEnd 同套归属规则：起点落在某表格单元格内则归入该单元格
        whiteboard::EraserPlacement placement;
        if (!points.empty() && AdoptIntoTableInList(CurrentPage()->elements, points.front(), stroke, &placement))
        { /* 已入格 */ }
        else
            CurrentPage()->Append(stroke);

        if (onElementsChanged_) {
            std::vector<std::string> removed;
            std::vector<std::shared_ptr<whiteboard::Element>> added = { stroke };
            std::vector<whiteboard::EraserPlacement> placements = { placement };
            onElementsChanged_(std::move(removed), std::move(added), std::move(placements));
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
    const std::vector<std::shared_ptr<whiteboard::Element>>& addedElements,
    const std::vector<whiteboard::EraserPlacement>& addedPlacements) {
    thread_->BeginInvoke([this, sessionId, removedIds, addedElements, addedPlacements]() {
        PushSnapshot();  // 一次远程擦除拖动记录一个快照
        auto& elems = CurrentPage()->elements;
        // 递归删除（含表格单元格内被擦的碎片）
        for (const auto& id : removedIds)
            RemoveElementById(elems, id, nullptr);
        // 碎片按归属落位（parentId 空或定位失败则页面级追加）；effective 回传实际归属
        std::vector<whiteboard::EraserPlacement> effective(addedElements.size());
        for (size_t i = 0; i < addedElements.size(); ++i) {
            const auto& e = addedElements[i];
            bool placed = false;
            if (i < addedPlacements.size() && !addedPlacements[i].parentId.empty())
                placed = PlaceInTableById(elems, addedPlacements[i].parentId,
                                          addedPlacements[i].cellIndex, e);
            if (!placed)
                elems.push_back(e);
            else if (i < addedPlacements.size())
                effective[i] = addedPlacements[i];
        }

        // 清除橡皮预览（UI 先恢复被隐藏的图元，再按下方增量精确删/加）
        if (onToolPreview_)
            onToolPreview_(1, sessionId, {}, {});
        if (onElementsChanged_ && (!removedIds.empty() || !addedElements.empty()))
            onElementsChanged_(removedIds, addedElements, effective);
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
        // 找到更新后的笔画并作为 added 增量回调（removed={id} 保证 UI 先删旧图元）；
        // 递归查我：表格单元格内的笔迹更新同样回调
        std::shared_ptr<whiteboard::Element> updated;
        auto* found = FindElementInList(CurrentPage()->elements, id);
        if (auto* stroke = dynamic_cast<whiteboard::Stroke*>(found))
            updated = std::make_shared<whiteboard::Stroke>(*stroke);
        if (onElementsChanged_) {
            std::vector<std::string> removed = { id };
            std::vector<std::shared_ptr<whiteboard::Element>> added;
            if (updated)
                added.push_back(updated);
            onElementsChanged_(std::move(removed), std::move(added), {});
        }
    });
}

// ---------- 远程元素增删改（不产生命令） ----------

void QtBoardData::RemoteAddElement(const std::shared_ptr<whiteboard::Element>& element) {
    thread_->BeginInvoke([this, element]() {
        if (!element)
            return;
        PushSnapshot();
        CurrentPage()->Append(element);
        if (onElementsChanged_) {
            std::vector<std::string> removed;
            std::vector<std::shared_ptr<whiteboard::Element>> added = { element };
            onElementsChanged_(std::move(removed), std::move(added), {});
        }
    });
}

void QtBoardData::RemoteRemoveElements(const std::vector<std::string>& ids) {
    thread_->BeginInvoke([this, ids]() {
        if (ids.empty())
            return;
        bool any = false;
        for (const auto& id : ids) {
            if (FindElementInList(CurrentPage()->elements, id)) {
                any = true;
                break;
            }
        }
        if (!any)
            return;

        PushSnapshot();
        for (const auto& id : ids)
            RemoveElementById(CurrentPage()->elements, id, nullptr);

        if (onElementsChanged_)
            onElementsChanged_(ids, {}, {});
    });
}

void QtBoardData::RemoteUpdateElement(const std::shared_ptr<whiteboard::Element>& element) {
    thread_->BeginInvoke([this, element]() {
        if (!element)
            return;
        PushSnapshot();
        // 原位替换：先递归删除同 id（含表格嵌套位置），再页面级追加
        RemoveElementById(CurrentPage()->elements, element->id, nullptr);
        CurrentPage()->Append(element);
        if (onElementsChanged_) {
            std::vector<std::string> removed = { element->id };
            std::vector<std::shared_ptr<whiteboard::Element>> added = { element };
            onElementsChanged_(std::move(removed), std::move(added), {});
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
        // 与本地 CreatePage 语义对齐：插入到当前页之后，保持双端页序一致
        pages_.insert(pages_.begin() + currentPage_ + 1, page);
        currentPage_ += 1;
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
