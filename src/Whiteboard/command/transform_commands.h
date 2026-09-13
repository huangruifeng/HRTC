#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/geometry/point.h"
#include <string>
#include <vector>

namespace whiteboard {

// 变换烘焙：移动/旋转/缩放结束后把映射后的新点集写回指定笔画
// （id/color/width 保持不变，接收端用新点集替换并重算包围盒）。
class StrokeUpdate : public Command {
public:
    std::string GetType() const override { return "StrokeUpdate"; }

    std::string        strokeId;
    std::vector<Point> points;
};

} // namespace whiteboard
