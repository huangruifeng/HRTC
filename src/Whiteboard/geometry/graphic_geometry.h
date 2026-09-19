#pragma once
#include "Whiteboard/element/graphic.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/rect.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace whiteboard {

// 图形轮廓生成（纯几何，无 UI 依赖）。
// 全部形状以拖动矩形 rc 为范围生成；颜色/线宽由调用方（工具层）填充。

namespace graphic_detail {

constexpr double kPi = 3.14159265358979323846;

inline int Round(double v) { return static_cast<int>(std::lround(v)); }

// 椭圆采样（rx/ry 为半轴；segments 段闭合点列）
inline std::vector<Point> SampleEllipse(double cx, double cy, double rx, double ry, int segments)
{
    std::vector<Point> pts;
    pts.reserve(segments);
    for (int i = 0; i < segments; ++i)
    {
        const double a = 2.0 * kPi * i / segments;
        pts.push_back(Point(Round(cx + rx * std::cos(a)), Round(cy + ry * std::sin(a))));
    }
    return pts;
}

// 正多边形顶点（r 外接圆半径，n 顶点，startDeg 起始角；-90 表示首个顶点朝上）
inline std::vector<Point> SamplePolygon(double cx, double cy, double r, int n, double startDeg)
{
    std::vector<Point> pts;
    pts.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const double a = (startDeg + 360.0 * i / n) * kPi / 180.0;
        pts.push_back(Point(Round(cx + r * std::cos(a)), Round(cy + r * std::sin(a))));
    }
    return pts;
}

// n 角星顶点（外径 r，内径 ratio*r，外/内顶点交替）
inline std::vector<Point> SampleStar(double cx, double cy, double r, double innerRatio, int n)
{
    std::vector<Point> pts;
    pts.reserve(n * 2);
    for (int i = 0; i < n * 2; ++i)
    {
        const double radius = (i % 2 == 0) ? r : r * innerRatio;
        const double a = (-90.0 + 180.0 * i / n) * kPi / 180.0;
        pts.push_back(Point(Round(cx + radius * std::cos(a)), Round(cy + radius * std::sin(a))));
    }
    return pts;
}

// 矩形四角闭合子路径
inline Subpath RectSubpath(const Rect& r)
{
    Subpath sp;
    sp.closed = true;
    sp.points = { Point(r.x, r.y), Point(r.x + r.width, r.y),
                  Point(r.x + r.width, r.y + r.height), Point(r.x, r.y + r.height) };
    return sp;
}

}  // namespace graphic_detail

// 由拖动矩形生成指定种类形状的轮廓子路径集（kind 取 GraphicKind 整数值）。
// 圆形强制 1:1（以矩形中心取最小边正方形）；多边形/星形外接圆半径取 min(w,h)/2。
inline std::vector<Subpath> BuildShape(int kind, const Rect& rc)
{
    using namespace graphic_detail;
    std::vector<Subpath> subpaths;
    if (rc.width <= 0 || rc.height <= 0)
        return subpaths;

    const double cx = rc.x + rc.width / 2.0;
    const double cy = rc.y + rc.height / 2.0;
    const double r = (std::min)(rc.width, rc.height) / 2.0;

    Subpath sp;
    switch (static_cast<GraphicKind>(kind))
    {
    case GraphicKind::Rect:
        subpaths.push_back(RectSubpath(rc));
        break;
    case GraphicKind::Circle:
        sp.closed = true;
        sp.points = SampleEllipse(cx, cy, r, r, 64);
        subpaths.push_back(sp);
        break;
    case GraphicKind::Ellipse:
        sp.closed = true;
        sp.points = SampleEllipse(cx, cy, rc.width / 2.0, rc.height / 2.0, 64);
        subpaths.push_back(sp);
        break;
    case GraphicKind::Triangle:
        sp.closed = true;
        sp.points = SamplePolygon(cx, cy, r, 3, -90.0);
        subpaths.push_back(sp);
        break;
    case GraphicKind::Pentagon:
        sp.closed = true;
        sp.points = SamplePolygon(cx, cy, r, 5, -90.0);
        subpaths.push_back(sp);
        break;
    case GraphicKind::Star:
        sp.closed = true;
        sp.points = SampleStar(cx, cy, r, 0.382, 5);
        subpaths.push_back(sp);
        break;
    default:
        break;
    }
    return subpaths;
}

}
