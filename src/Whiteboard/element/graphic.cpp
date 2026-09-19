#include "Whiteboard/element/graphic.h"
#include <chrono>

namespace whiteboard {

GraphicElement::GraphicElement()
{
    Reset();
}

void GraphicElement::Reset()
{
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    kind = 0;
    width = 3;
    color = 0x00FFFFFF;
    subpaths.clear();
    // 空包围盒（含毒值），Rebuild 后收敛为真实范围；
    // 不可初始化为 Rect(0,0,0,0)，否则每个图形包含幽灵原点(0,0)，
    // 导致缩略图计算 least 矩形时跨度被撑爆、内容不可见（同 Stroke 约定）。
    bounding = BoundaryRect();
}

void GraphicElement::Rebuild()
{
    bounding = BoundaryRect();
    for (const auto& sp : subpaths)
    {
        for (const auto& p : sp.points)
            bounding.Update(p.x, p.y);
    }
}

}
