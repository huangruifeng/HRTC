#pragma once
#include "Whiteboard/geometry/point.h"

namespace whiteboard {

class Rect
{
public:
    Rect() : x(0), y(0), width(0), height(0) { }
    Rect(int xx, int yy, int ww, int hh) : x(xx), y(yy), width(ww), height(hh) { }

    int GetLeft()   const { return x; }
    int GetTop()    const { return y; }
    int GetBottom() const { return y + height - 1; }
    int GetRight()  const { return x + width - 1; }

    Point GetTopLeft()     const { return Point(x, y); }
    Point GetLeftTop()     const { return GetTopLeft(); }
    Point GetBottomRight() const { return Point(GetRight(), GetBottom()); }
    Point GetTopRight()    const { return Point(GetRight(), GetTop()); }
    Point GetBottomLeft()  const { return Point(GetLeft(), GetBottom()); }

    // return true if the point is (not strictly) inside the rect
    bool Contains(int cx, int cy) const {
        return (cx >= x) && (cy >= y) && ((cy - y) < height) && ((cx - x) < width);
    }
    bool Contains(const Point& pt) const { return Contains(pt.x, pt.y); }

    Rect& Intersect(const Rect& rect) {
        int x2 = GetRight(),
            y2 = GetBottom();

        if (x < rect.x)
            x = rect.x;
        if (y < rect.y)
            y = rect.y;
        if (x2 > rect.GetRight())
            x2 = rect.GetRight();
        if (y2 > rect.GetBottom())
            y2 = rect.GetBottom();

        width = x2 - x + 1;
        height = y2 - y + 1;

        if (width <= 0 || height <= 0)
        {
            width =
            height = 0;
        }

        return *this;
    }

    Rect Intersect(const Rect& rect) const {
        Rect r = *this;
        r.Intersect(rect);
        return r;
    }

    // return true if the rectangles have a non empty intersection
    bool Intersects(const Rect& rect) const {
        Rect r = Intersect(rect);
        return r.width != 0;
    }

public:
    int x, y, width, height;
};

}