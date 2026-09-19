#include "Whiteboard/element/table.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace whiteboard {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

TableElement::TableElement()
{
    Reset();
}

void TableElement::Reset()
{
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    bounds = Rect();
    rotation = 0.0f;
    rows = 3;
    cols = 3;
    width = 2;
    color = 0x00FFFFFF;
    cells.assign(static_cast<size_t>(rows) * cols, {});
}

int TableElement::CellIndexAt(const Point& p) const
{
    if (rows <= 0 || cols <= 0)
        return -1;
    if (bounds.width <= 0 || bounds.height <= 0)
        return -1;

    const double cx = bounds.x + bounds.width / 2.0;
    const double cy = bounds.y + bounds.height / 2.0;
    const double dx = p.x - cx;
    const double dy = p.y - cy;

    // 逆旋转回表格局部轴对齐坐标：R(-θ)·(p - center)
    double lx = dx;
    double ly = dy;
    if (std::abs(rotation) > 1e-6f)
    {
        const double rad = rotation * kPi / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        lx = dx * c + dy * s;
        ly = -dx * s + dy * c;
    }

    const double wx = lx + cx;
    const double wy = ly + cy;
    if (wx < bounds.x || wy < bounds.y ||
        wx >= bounds.x + bounds.width || wy >= bounds.y + bounds.height)
        return -1;

    int col = static_cast<int>((wx - bounds.x) * cols / bounds.width);
    int row = static_cast<int>((wy - bounds.y) * rows / bounds.height);
    col = std::min(std::max(col, 0), cols - 1);
    row = std::min(std::max(row, 0), rows - 1);
    return row * cols + col;
}

}
