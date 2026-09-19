#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/element/stroke.h"
#include "Whiteboard/geometry/rect.h"
#include "Whiteboard/geometry/transform.h"
#include <list>
#include <vector>
#include <memory>
#include <string>

namespace whiteboard {

struct EraserResult {
    std::vector<std::string> removedIds;                 // ids removed by this eraser pass
    std::vector<std::shared_ptr<Element>> addedElements; // new Stroke fragments created by this pass
    // 与 addedElements 一一对齐的归属位置：parentId 空 = 页面级，
    // 否则为表格 id + 单元格索引（远端需知碎片归属单元格）。
    std::vector<EraserPlacement> addedPlacements;
};

class Page : public Element {
public:
    Page();

    std::string GetType() const override { return "Page"; }

    void Append(const std::shared_ptr<Element>& e);
    void Append(const Stroke& s);          // convenience: wraps via make_shared<Stroke>(s)
    void Delete(const std::string& id);    // delete by id
    void Clear();
    // 用新的点集替换指定笔画（保持 id/color/width 不变，重算包围盒）。
    // 供 UI 侧移动/旋转/缩放完成后把变换烘焙回数据层使用。
    bool UpdateStroke(const std::string& id, const std::vector<Point>& points);

    // 深拷贝整页（元素逐个克隆，不共享 Stroke），供撤销/重做历史快照使用。
    std::shared_ptr<Page> Clone() const;

    EraserResult Eraser(const Rect& rc, int sid);
    EraserResult Eraser(const Rect& rc);

    void EnableEraserInsert(bool enable);

    Point WorldToScreen(const Point& p) const { return transform.WorldToScreen(p); }
    Point ScreenToWorld(const Point& p) const { return transform.ScreenToWorld(p); }

    std::string pageId;
    std::list<std::shared_ptr<Element>> elements;  // generalized from the old paths list
    Transform transform;                           // replaces the old float scale

    bool enableEraserInsert;
    int  eraserSessionId;
    int  insertInstance;
    std::vector<Point> insertPoints;

private:
    Point PointOfIntersection(const std::vector<Point>::iterator& it, const Rect& orc);

    // 单列表擦除助手：原 Eraser 内笔画 diff 逻辑原样迁移（列表参数化）。
    // parentId 空 = 页面级，否则为表格 id；碎片按同一归属写入 addedPlacements。
    void EraseInList(std::list<std::shared_ptr<Element>>& list, const Rect& rc,
                     EraserResult& result, const std::string& parentId, int cellIndex);
};

}