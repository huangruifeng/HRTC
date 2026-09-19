#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/element/element.h"
#include <memory>
#include <string>
#include <vector>

namespace whiteboard {

class EraserBegin : public Command {
public:
    std::string GetType() const override { return "EraserBegin"; }

    std::string sessionId;  // 一次擦除拖动的会话 id（远端用于关联 Begin/Move/End）
    std::vector<Point> points;
};

class EraserMove : public Command {
public:
    std::string GetType() const override { return "EraserMove"; }

    std::string sessionId;  // 一次擦除拖动的会话 id（远端用于关联 Begin/Move/End）
    std::vector<Point> points;
};

class EraserEnd : public Command {
public:
    std::string GetType() const override { return "EraserEnd"; }

    std::string sessionId;  // 一次擦除拖动的会话 id（远端用于关联 Begin/Move/End）
    std::vector<std::string> removedIds;
    std::vector<std::shared_ptr<Element>> addedElements;
    // 与 addedElements 一一对齐的归属位置（parentId 空 = 页面级）。
    // 双端同版本构建，无向后兼容负担。
    std::vector<EraserPlacement> addedPlacements;
};

} // namespace whiteboard