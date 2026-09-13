#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/geometry/point.h"
#include <string>
#include <vector>

namespace whiteboard {

// 套索路径预览：操作端实时采样点集，远端以浅蓝虚线渲染；End 广播空 points 表示清除。
class LassoPreview : public Command {
public:
    std::string GetType() const override { return "LassoPreview"; }

    std::string sessionId;  // 一次套索操作的会话 id（远端用于区分来源）
    std::vector<Point> points;
};

// 选择框预览：操作端选择框 4 顶点（含旋转）+ 选中笔画 id（供远端高亮，可为空）；
// 清除选择时广播空 points。
class SelectionPreview : public Command {
public:
    std::string GetType() const override { return "SelectionPreview"; }

    std::string sessionId;  // 一次选择操作的会话 id（远端用于区分来源）
    std::vector<Point> points;          // 选择框 4 顶点（按序连线）
    std::vector<std::string> selectedIds;  // 选中笔画 id
};

} // namespace whiteboard
