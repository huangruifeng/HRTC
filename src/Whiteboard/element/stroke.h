#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/boundary_rect.h"
#include <vector>
#include <cstdint>

namespace whiteboard {

class Stroke : public Element {
public:
    Stroke();

    std::string GetType() const override { return "Stroke"; }

    void Append(const Point& point);
    void Reset();  // generates a chrono-based id (written into the base-class id)

    static void InsertPoint(std::vector<Point>& points, const Point& end, uint32_t minDistance);
    static Point GetInterPoint(const FloatPoint& begin, const FloatPoint& end, uint32_t distance);

    int                width;
    uint32_t           color;
    std::vector<Point> points;
    std::vector<Point> rawPoints;   // user-drawn points only, excludes interpolated points
    int                minDistance;
    BoundaryRect       bounding;
};

}