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

    std::vector<Point> points;
};

class EraserMove : public Command {
public:
    std::string GetType() const override { return "EraserMove"; }

    std::vector<Point> points;
};

class EraserEnd : public Command {
public:
    std::string GetType() const override { return "EraserEnd"; }

    std::vector<std::string> removedIds;
    std::vector<std::shared_ptr<Element>> addedElements;
};

} // namespace whiteboard