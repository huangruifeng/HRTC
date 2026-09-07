#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/geometry/point.h"
#include <cstdint>
#include <string>

namespace whiteboard {

class StrokeBegin : public Command {
public:
    std::string GetType() const override { return "StrokeBegin"; }

    std::string strokeId;
    int         width = 0;
    uint32_t    color = 0;
    Point       point;
};

class StrokeMove : public Command {
public:
    std::string GetType() const override { return "StrokeMove"; }

    std::string strokeId;
    Point       point;
};

class StrokeEnd : public Command {
public:
    std::string GetType() const override { return "StrokeEnd"; }

    std::string strokeId;
    Point       point;
};

} // namespace whiteboard