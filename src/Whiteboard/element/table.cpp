#include "Whiteboard/element/table.h"
#include "Whiteboard/element/graphic.h"
#include "Whiteboard/element/stroke.h"
#include "Whiteboard/element/text.h"
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
    origin = Point();
    rotation = 0.0f;
    rows = 3;
    cols = 3;
    width = 2;
    color = 0x00FFFFFF;
    minCellW = 120.0f;
    minCellH = 80.0f;
    cells.assign(static_cast<size_t>(rows) * cols, {});
}

void TableElement::Layout::CellOrigin(int idx, double& ox, double& oy) const
{
    ox = 0.0;
    oy = 0.0;
    const int c = static_cast<int>(colW.size());
    if (idx < 0 || c <= 0)
        return;
    const int row = idx / c;
    const int col = idx % c;
    if (row >= static_cast<int>(rowH.size()))
        return;
    for (int i = 0; i < col; ++i)
        ox += colW[static_cast<size_t>(i)];
    for (int i = 0; i < row; ++i)
        oy += rowH[static_cast<size_t>(i)];
}

Rect TableElement::Layout::CellRect(int idx) const
{
    const int c = static_cast<int>(colW.size());
    if (idx < 0 || c <= 0)
        return Rect();
    const int row = idx / c;
    const int col = idx % c;
    if (row >= static_cast<int>(rowH.size()))
        return Rect();
    double ox = 0.0;
    double oy = 0.0;
    CellOrigin(idx, ox, oy);
    return Rect(static_cast<int>(std::lround(ox)), static_cast<int>(std::lround(oy)),
                static_cast<int>(std::lround(colW[static_cast<size_t>(col)])),
                static_cast<int>(std::lround(rowH[static_cast<size_t>(row)])));
}

bool TableElement::ContentBounds(const Element& e, Rect& out)
{
    if (const auto* stroke = dynamic_cast<const Stroke*>(&e))
    {
        if (stroke->points.empty())
            return false;  // 空点集包围盒为毒值，不参与布局
        out = stroke->bounding.ToRect();
        return true;
    }
    if (const auto* graphic = dynamic_cast<const GraphicElement*>(&e))
    {
        if (graphic->subpaths.empty())
            return false;
        out = graphic->bounding.ToRect();
        return true;
    }
    if (const auto* text = dynamic_cast<const TextElement*>(&e))
    {
        if (text->bounds.width <= 0 || text->bounds.height <= 0)
            return false;  // 空文本不参与布局
        out = text->bounds;
        return true;
    }
    return false;
}

TableElement::Layout TableElement::ComputeLayout() const
{
    Layout layout;
    const int c = std::max(cols, 0);
    const int r = std::max(rows, 0);
    layout.colW.assign(static_cast<size_t>(c),
                       std::max(static_cast<double>(minCellW), 1.0));
    layout.rowH.assign(static_cast<size_t>(r),
                       std::max(static_cast<double>(minCellH), 1.0));

    for (int row = 0; row < r; ++row)
    {
        for (int col = 0; col < c; ++col)
        {
            const size_t idx = static_cast<size_t>(row) * c + col;
            if (idx >= cells.size())
                break;
            for (const auto& child : cells[idx])
            {
                if (!child)
                    continue;
                Rect cb;
                if (!ContentBounds(*child, cb))
                    continue;
                // 内容右/下边界 + 内边距 = 格所需尺寸（左/上溢出不扩格）
                const double needW = cb.x + cb.width + kCellPad;
                const double needH = cb.y + cb.height + kCellPad;
                if (needW > layout.colW[col])
                    layout.colW[col] = needW;
                if (needH > layout.rowH[row])
                    layout.rowH[row] = needH;
            }
        }
    }

    layout.totalW = 0.0;
    for (const double w : layout.colW)
        layout.totalW += w;
    layout.totalH = 0.0;
    for (const double h : layout.rowH)
        layout.totalH += h;
    return layout;
}

int TableElement::CellIndexAt(const Point& p) const
{
    if (rows <= 0 || cols <= 0)
        return -1;

    const Layout layout = ComputeLayout();
    if (layout.totalW <= 0.0 || layout.totalH <= 0.0)
        return -1;

    const double cx = origin.x + layout.totalW / 2.0;
    const double cy = origin.y + layout.totalH / 2.0;
    const double dx = p.x - cx;
    const double dy = p.y - cy;

    // 逆旋转回表格局部轴对齐坐标：R(-θ)·(p - center)
    double lx = dx;
    double ly = dy;
    if (std::abs(rotation) > 1e-6f)
    {
        const double rad = rotation * kPi / 180.0;
        const double cs = std::cos(rad);
        const double sn = std::sin(rad);
        lx = dx * cs + dy * sn;
        ly = -dx * sn + dy * cs;
    }

    const double wx = lx + cx;
    const double wy = ly + cy;
    if (wx < origin.x || wy < origin.y ||
        wx >= origin.x + layout.totalW || wy >= origin.y + layout.totalH)
        return -1;

    // 按列宽/行高累计定位（总宽/高已保证命中）
    int col = cols - 1;
    double acc = origin.x;
    for (int i = 0; i < cols; ++i)
    {
        if (wx < acc + layout.colW[static_cast<size_t>(i)])
        {
            col = i;
            break;
        }
        acc += layout.colW[static_cast<size_t>(i)];
    }
    int row = rows - 1;
    acc = origin.y;
    for (int i = 0; i < rows; ++i)
    {
        if (wy < acc + layout.rowH[static_cast<size_t>(i)])
        {
            row = i;
            break;
        }
        acc += layout.rowH[static_cast<size_t>(i)];
    }
    return row * cols + col;
}

}
