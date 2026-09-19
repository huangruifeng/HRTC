#pragma once
#include "Whiteboard/command/command.h"
#include "Whiteboard/element/element.h"
#include <memory>
#include <string>

namespace whiteboard {

class ElementAdd : public Command {
public:
    std::string GetType() const override { return "ElementAdd"; }

    std::shared_ptr<Element> element;
};

class ElementRemove : public Command {
public:
    std::string GetType() const override { return "ElementRemove"; }

    std::string elementId;
};

// 整元素按 id 替换（用于图形/表格变换烘焙的远端同步）。
class ElementUpdate : public Command {
public:
    std::string GetType() const override { return "ElementUpdate"; }

    std::shared_ptr<Element> element;
};

} // namespace whiteboard