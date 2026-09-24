#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/boundary_rect.h"
#include <cstdint>
#include <vector>

namespace whiteboard {

// 图形种类（仅记录生成来源；渲染完全由 subpaths 决定）
enum class GraphicKind {
    Rect = 0,
    Circle,
    Ellipse,
    Triangle,
    Pentagon,
    Star,
    Polygon,  // 任意多边形（顶点由调用方给定，AI 轨迹美化用）
    Line,     // 直线（两点开放子路径，AI 轨迹美化用）
};

// 子路径：一条折线（closed = 描边时首尾闭合）
struct Subpath {
    bool closed = false;
    std::vector<Point> points;
};

// 矢量图形元素：多子路径描边（矩形/圆形/椭圆/多边形/五角星统一承载）。
// 与 Stroke 同构（纯点集 + 描边参数），完整复用选择/命中/变换烘焙/缩略图管线；
// 橡皮擦仅作用于 Stroke，本类型天然不可擦除。
class GraphicElement : public Element {
public:
    GraphicElement();

    std::string GetType() const override { return "Graphic"; }

    void Reset();    // chrono id + 空包围盒（毒值，同 Stroke 约定）
    void Rebuild();  // 由 subpaths 重算包围盒

    int kind = 0;                   // GraphicKind，仅记录生成来源
    int width = 3;                  // 描边线宽
    uint32_t color = 0x00FFFFFF;
    std::vector<Subpath> subpaths;  // 多子路径点集（页面绝对坐标）
    BoundaryRect bounding;          // 由 subpaths 重算（Rebuild）
};

}
