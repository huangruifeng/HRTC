#pragma once
#include <memory>
#include <string>

namespace whiteboard {

class Element {
public:
    virtual ~Element() = default;

    // Type name tag: the built-in Stroke returns "Stroke"; external element types
    // may return any custom string without modifying this library.
    virtual std::string GetType() const = 0;

    std::string id;   // identifier shared by all elements
};

using ElementFactory = std::shared_ptr<Element> (*)();

// Registers a factory for an element type so that it can be reconstructed during
// deserialization. Returns false if the type name is already registered.
bool RegisterElementFactory(const std::string& type, ElementFactory factory);

// Creates an element instance from a type name; returns nullptr if unknown.
std::shared_ptr<Element> CreateElement(const std::string& type);

}