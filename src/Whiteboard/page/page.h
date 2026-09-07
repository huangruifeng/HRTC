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
};

class Page : public Element {
public:
    Page();

    std::string GetType() const override { return "Page"; }

    void Append(const std::shared_ptr<Element>& e);
    void Append(const Stroke& s);          // convenience: wraps via make_shared<Stroke>(s)
    void Delete(const std::string& id);    // delete by id
    void Clear();

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
};

}