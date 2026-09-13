#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/geometry/point.h"
#include <cstdint>
#include <string>
#include <vector>

namespace whiteboard {

class StrokeBegin : public Command {
public:
    std::string GetType() const override { return "StrokeBegin"; }

    std::string        strokeId;
    int                width = 0;
    uint32_t           color = 0;
    std::vector<Point> points;
};

class StrokeMove : public Command {
public:
    std::string GetType() const override { return "StrokeMove"; }

    std::string        strokeId;
    std::vector<Point> points;
};

class StrokeEnd : public Command {
public:
    std::string GetType() const override { return "StrokeEnd"; }

    std::string        strokeId;
    std::vector<Point> points;
};

} // namespace whiteboard