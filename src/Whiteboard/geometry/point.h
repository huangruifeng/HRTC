#pragma once
#include <cstdint>
#include <cmath>

namespace whiteboard {

template<class T>
class PointTemplate {
public:
    T x, y;
    PointTemplate() : x(0), y(0) { }
    PointTemplate(int xx, int yy) : x(xx), y(yy) { }

    static uint32_t Distance(const PointTemplate<T>& pt1, const PointTemplate<T>& pt2)
    {
        return (uint32_t)std::sqrt(std::pow((pt2.x - pt1.x), 2) + std::pow((pt2.y - pt1.y), 2));
    }
};

template<class T>
inline bool operator==(const PointTemplate<T>& p1, const PointTemplate<T>& p2)
{
    return p1.x == p2.x && p1.y == p2.y;
}

using Point = PointTemplate<int>;
using FloatPoint = PointTemplate<float>;

}