#pragma once
#include "Whiteboard/geometry/rect.h"

namespace whiteboard {

static constexpr int kDefaultMinValue = 9999999;

struct BoundaryRect
{
    // 空盒（毒值）：maxx/maxy 必须取 -kDefaultMinValue（而非 0），
    // 否则全负坐标笔画的 maxx/maxy 停留 0，包围盒被幽灵原点 (0,0) 污染，
    // 导致缩略图 least 计算失效、内容不可见。
    BoundaryRect() : minx(kDefaultMinValue), maxx(-kDefaultMinValue), miny(kDefaultMinValue), maxy(-kDefaultMinValue) {}
    BoundaryRect(const Rect& rx) : minx(rx.x), maxx(rx.x + rx.width), miny(rx.y), maxy(rx.y + rx.height) {}

    Rect ToRect() const {
        if (minx > maxx || miny > maxy)
            return Rect(0, 0, 0, 0);  // 空盒（从未 Update）：返回空矩形
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