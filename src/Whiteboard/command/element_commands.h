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

} // namespace whiteboard