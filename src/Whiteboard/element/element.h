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

// 元素归属位置：parentId 为空 = 页面级；否则为所属表格元素的 id，
// cellIndex 为单元格索引（row*cols+col）。橡皮擦碎片等新增元素用其标注归属。
struct EraserPlacement {
    std::string parentId;
    int cellIndex = -1;
};

// 按具体类型深拷贝元素（Stroke/Graphic/Table/Page 递归；未知类型返回 nullptr）。
// 供 Page::Clone 与表格单元格子元素深拷贝使用。
std::shared_ptr<Element> CloneElement(const Element& e);

}