#pragma once
#include "Whiteboard/geometry/rect.h"

namespace whiteboard {

static constexpr int kDefaultMinValue = 9999999;

struct BoundaryRect
{
    BoundaryRect() : minx(kDefaultMinValue), maxx(0), miny(kDefaultMinValue), maxy(0) {}
    BoundaryRect(const Rect& rx) : minx(rx.x), maxx(rx.x + rx.width), miny(rx.y), maxy(rx.y + rx.height) {}

    Rect ToRect() const {
        int w = maxx - minx;
        int h = maxy - miny;
        if (0 == w)
            w = 1;
        if (0 == h)
            h = 1;
        return Rect(minx, miny, w, h);
    }

    void Update(int x, int y)
    {
        if (x > maxx)
            maxx = x;
        if (x < minx)
            minx = x;
        if (y > maxy)
            maxy = y;
        if (y < miny)
            miny = y;
    }

public:
    int minx, maxx, miny, maxy;
};

}