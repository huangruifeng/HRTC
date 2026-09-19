#include "Whiteboard/page/page.h"
#include "Whiteboard/element/table.h"
#include "Whiteboard/geometry/line_segment.h"
#include <algorithm>

namespace whiteboard {

namespace {

// 单列表内按 id 更新笔画点集并重算包围盒；找到目标返回 true。
bool UpdateStrokeInList(std::list<std::shared_ptr<Element>>& list,
                        const std::string& id, const std::vector<Point>& points)
{
    for (auto& e : list)
    {
        if (e->id != id)
            continue;
        auto* stroke = dynamic_cast<Stroke*>(e.get());
        if (!stroke)
            return false;
        stroke->points = points;
        stroke->rawPoints = points;
        // 从空包围盒重新收敛（不能用 Rect(0,0,0,0)，否则含幽灵原点导致缩略图异常）
        stroke->bounding = BoundaryRect();
        for (const auto& p : points)
            stroke->bounding.Update(p.x, p.y);
        return true;
    }
    return false;
}

}  // namespace

Page::Page()
{
    EnableEraserInsert(false);
}

void Page::Append(const std::shared_ptr<Element>& e)
{
    elements.push_back(e);
}

void Page::Append(const Stroke& s)
{
    elements.push_back(std::make_shared<Stroke>(s));
}

void Page::Delete(const std::string& id)
{
    elements.remove_if([&](const std::shared_ptr<Element>& e) { return e->id == id; });
}

void Page::Clear()
{
    elements.clear();
}

bool Page::UpdateStroke(const std::string& id, const std::vector<Point>& points)
{
    if (UpdateStrokeInList(elements, id, points))
        return true;
    // 表格单元格内的笔迹（坐标为页面绝对坐标，与页面级同一套更新逻辑）
    for (auto& e : elements)
    {
        if (auto* table = dynamic_cast<TableElement*>(e.get()))
        {
            for (auto& cell : table->cells)
            {
                if (UpdateStrokeInList(cell, id, points))
                    return true;
            }
        }
    }
    return false;
}

std::shared_ptr<Page> Page::Clone() const
{
    auto page = std::make_shared<Page>();
    page->pageId = pageId;
    page->transform = transform;
    page->enableEraserInsert = enableEraserInsert;
    page->eraserSessionId = eraserSessionId;
    page->insertInstance = insertInstance;
    for (const auto& e : elements)
    {
        // 元素逐个克隆：按类型深拷贝（Table 递归单元格子元素）；未知类型保持共享引用（防御）
        auto copy = e ? CloneElement(*e) : nullptr;
        page->elements.push_back(copy ? copy : e);
    }
    return page;
}

void Page::EnableEraserInsert(bool enable)
{
    eraserSessionId = -1;
    enableEraserInsert = enable;
    insertInstance = 5;
}

EraserResult Page::Eraser(const Rect& rc, int sid)
{
    if (enableEraserInsert) {
        if (sid != eraserSessionId) {
            insertPoints.clear();
            eraserSessionId = sid;
        }
        else {
            if (!insertPoints.empty()) {
                Stroke::InsertPoint(insertPoints, rc.GetLeftTop(), insertInstance);
            }
        }

        insertPoints.push_back(rc.GetLeftTop());
    }

    EraserResult result;
    for (auto& p : insertPoints) {
        EraserResult r = Eraser(Rect(p.x, p.y, rc.width, rc.height));
        for (auto& id : r.removedIds) {
            if (std::find(result.removedIds.begin(), result.removedIds.end(), id) == result.removedIds.end())
                result.removedIds.push_back(id);
        }
        result.addedElements.insert(result.addedElements.end(), r.addedElements.begin(), r.addedElements.end());
        result.addedPlacements.insert(result.addedPlacements.end(), r.addedPlacements.begin(), r.addedPlacements.end());
    }

    if (enableEraserInsert) {
        insertPoints.clear();
        insertPoints.push_back(rc.GetLeftTop());
    }

    return result;
}

EraserResult Page::Eraser(const Rect& rc)
{
    EraserResult result;
    EraseInList(elements, rc, result, "", -1);

    // 表格递归：仅擦除各单元格内的 Stroke（碎片原地替换、归属不变），
    // 网格本身与其他类型元素天然跳过。先收集表指针再处理，避免迭代器失效。
    std::vector<TableElement*> tables;
    for (auto& e : elements)
    {
        if (auto* table = dynamic_cast<TableElement*>(e.get()))
            tables.push_back(table);
    }
    for (auto* table : tables)
    {
        for (size_t ci = 0; ci < table->cells.size(); ++ci)
            EraseInList(table->cells[ci], rc, result, table->id, static_cast<int>(ci));
    }

    return result;
}

void Page::EraseInList(std::list<std::shared_ptr<Element>>& list, const Rect& rc,
                       EraserResult& result, const std::string& parentId, int cellIndex)
{
    EraserPlacement placement;
    placement.parentId = parentId;
    placement.cellIndex = cellIndex;

    for (auto it = list.begin(); it != list.end();)
    {
        auto* stroke = dynamic_cast<Stroke*>(it->get());
        if (!stroke)
        {
            ++it;
            continue;
        }

        if (!rc.Intersects(stroke->bounding.ToRect()))
        {
            ++it;
            continue;
        }

        auto& points = stroke->points;
        bool contain = rc.Contains(*points.begin());
        const auto firstContain = contain;
        bool lastContained = contain;

        std::vector<std::vector<Point>::iterator> partPoints;
        if (!firstContain)
        {
            partPoints.push_back(points.begin());
        }
        for (auto p = points.begin(); p != points.end(); ++p)
        {
            contain = rc.Contains(*p);
            if (lastContained != contain)
            {
                partPoints.push_back(p);
                lastContained = contain;
            }
        }

        if (partPoints.empty())
        {
            result.removedIds.push_back(stroke->id);
            it = list.erase(it);
        }
        else if (1 == partPoints.size() && !firstContain)
        {
            ++it;
        }
        else
        {
            std::vector<std::shared_ptr<Stroke>> newPaths;
            if (partPoints.size() > 1)
            {
                for (size_t i = 0; i + 1 < partPoints.size(); i += 2)
                {
                    auto newPath = std::make_shared<Stroke>();
                    newPath->Reset();  // 分配唯一 id，保证 UI 增量按 key 精确删/加
                    newPath->color = stroke->color;
                    newPath->width = stroke->width;

                    if (!firstContain && i == 0)
                    {
                        newPath->points.resize(partPoints[i + 1] - partPoints[i] + 1);

                        auto begin = newPath->points.begin();

                        while (partPoints[0] != partPoints[1])
                        {
                            newPath->bounding.Update(partPoints[0]->x, partPoints[0]->y);
                            *begin++ = *partPoints[0]++;
                        }

                        Point point = PointOfIntersection(partPoints[1], rc);

                        *begin = point;
                        newPath->bounding.Update(point.x, point.y);
                    }
                    else
                    {
                        newPath->points.resize(partPoints[i + 1] - partPoints[i] + 2);

                        auto begin = newPath->points.begin();
                        Point bpoint = PointOfIntersection(partPoints[i], rc);
                        newPath->bounding.Update(bpoint.x, bpoint.y);
                        *begin++ = bpoint;
                        while (partPoints[i] != partPoints[i + 1])
                        {
                            newPath->bounding.Update(partPoints[i]->x, partPoints[i]->y);
                            *begin++ = *partPoints[i]++;
                        }

                        Point epoint = PointOfIntersection(partPoints[i + 1], rc);

                        newPath->bounding.Update(epoint.x, epoint.y);
                        *begin = epoint;
                    }

                    newPaths.emplace_back(newPath);
                }
            }

            const auto endConatain = rc.Contains(*points.rbegin());
            if (!endConatain)
            {
                auto newPath = std::make_shared<Stroke>();
                newPath->Reset();  // 分配唯一 id，保证 UI 增量按 key 精确删/加
                newPath->color = stroke->color;
                newPath->width = stroke->width;
                auto oBegin = *partPoints.rbegin();

                newPath->points.resize(points.end() - 1 - oBegin + 2); // add two extra points
                Point point = PointOfIntersection(oBegin, rc);

                auto begin = newPath->points.begin();
                *begin++ = point;
                newPath->bounding.Update(point.x, point.y);

                while (oBegin != points.end())
                {
                    newPath->bounding.Update(oBegin->x, oBegin->y);
                    *begin++ = *oBegin++;
                }
                newPaths.emplace_back(newPath);
            }

            if (!newPaths.empty())
            {
                for (auto& np : newPaths)
                    np->rawPoints = np->points;

                result.removedIds.push_back(stroke->id);
                for (auto& np : newPaths)
                {
                    result.addedElements.push_back(np);
                    result.addedPlacements.push_back(placement);
                }

                it = list.erase(it);
                it = list.insert(it, newPaths.begin(), newPaths.end());
                size_t i = 0;
                while (i < newPaths.size())
                {
                    ++it;
                    ++i;
                }
            }
            else
            {
                ++it;
            }
        }
    }
}

Point Page::PointOfIntersection(const std::vector<Point>::iterator& it, const Rect& orc)
{
    Rect rc{ orc.x - 1, orc.y - 1, orc.width + 2, orc.height + 2 };
    return Linesegment(*(it - 1), *it).Intersection(orc);
}

}