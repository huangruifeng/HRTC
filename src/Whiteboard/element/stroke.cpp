#include "Whiteboard/element/stroke.h"
#include <chrono>
#include <list>
#include <cmath>

namespace whiteboard {

Stroke::Stroke()
{
    Reset();
}

void Stroke::Reset()
{
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    width = 0;
    color = 0;
    points.clear();
    rawPoints.clear();
    // 空包围盒（含毒值），首个点 Append/Update 后收敛为真实范围；
    // 不可初始化为 Rect(0,0,0,0)，否则每个笔画会包含幽灵原点(0,0)，
    // 导致缩略图计算 least 矩形时跨度被撑爆、内容不可见。
    bounding = BoundaryRect();
    minDistance = 20;
}

void Stroke::Append(const Point& point)
{
    InsertPoint(points, point, minDistance);
    points.push_back(point);
    rawPoints.push_back(point);
    bounding.Update(point.x, point.y);
}

void Stroke::InsertPoint(std::vector<Point>& points, const Point& end, uint32_t minDistance)
{
    if (points.empty())
        return;
    auto begin = *points.rbegin();
    auto distance = Point::Distance(begin, end);
    if (distance > minDistance)
    {
        const auto count = static_cast<int>(distance / minDistance);
        std::list<Point> path;
        for (auto i = 0; i < count; i++)
        {
            const auto pt = GetInterPoint({ begin.x, begin.y }, { end.x, end.y }, minDistance * (count - i));
            if (path.empty() && pt == *points.rbegin())
                continue;
            if (!path.empty() && pt == *path.rbegin())
                continue;
            path.push_back({ static_cast<int>(pt.x), static_cast<int>(pt.y) });
        }
        points.insert(points.end(), path.rbegin(), path.rend());
    }
}

Point Stroke::GetInterPoint(const FloatPoint& begin, const FloatPoint& end, uint32_t distance)
{
    if (std::abs(end.x - begin.x) < 0.0001) // vertical (slope does not exist)
    {
        if ((end.y - begin.y) >= 0)
        {
            return Point((int)std::round(begin.x), (int)std::round(begin.y) + (int)distance);
        }
        else
        {
            return Point((int)std::round(begin.x), (int)std::round(begin.y) - (int)distance);
        }
    }
    else
    {
        const auto k = (end.y - begin.y) / (end.x - begin.x);
        const auto b = begin.y - k * begin.x;
        /*
        * Pythagorean theorem: distance^2 = x^2 + y^2
        * y = kx + b
        * Solve the quadratic equation.
        */
        const auto A = std::pow(k, 2) + 1;
        const auto B = 2 * ((b - begin.y) * k - begin.x);

        const auto C = std::pow(b - begin.y, 2) + std::pow(begin.x, 2) - std::pow(distance, 2);

        const auto x1 = (-B + std::sqrt(std::pow(B, 2) - 4 * A * C)) / (2 * A);
        const auto x2 = (-B - std::sqrt(std::pow(B, 2) - 4 * A * C)) / (2 * A);
        auto x = x1;

        if (x1 == x2) {
            x = x1;
        }
        else if (begin.x <= x1 && x1 <= end.x || end.x <= x1
            && x1 <= begin.x) {
            x = x1;
        }
        else if (begin.x <= x2 && x2 <= end.x || end.x <= x2
            && x2 <= begin.x) {
            x = x2;
        }

        const auto y = k * x + b;

        return Point((int)x, (int)y);
    }
}

}