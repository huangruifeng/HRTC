#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/geometry/rect.h"
#include <cstdint>
#include <string>

namespace whiteboard {

// 文字元素：纯文本 + 锚点/字号/旋转/颜色参数。
// bounds 为字形包围盒（未旋转，多行硬换行一并包含；与 x/y 同坐标系：
// 页面级为页面绝对坐标，表格格内为格局部坐标），由 UI 端用 QPainterPath
// addText 的 boundingRect 计算并维护——表格局部布局依赖它，数据层不自算。
// 橡皮擦仅作用于 Stroke，本类型天然不可擦除。整体变换（移动/缩放/旋转）
// 参数写回本元素。
class TextElement : public Element {
public:
    TextElement();

    std::string GetType() const override { return "Text"; }

    // chrono id + 默认值（空文本 / 32 号 / 白色 / 无旋转）
    void Reset();

    std::string text;       // UTF-8 文本（'\n' 为硬换行，支持多行）
    int x = 0;              // 锚点：文字左上角（页面绝对坐标）
    int y = 0;
    int fontSize = 32;      // 像素字号（数据坐标）
    float rotation = 0.0f;  // 度，正值 = Qt rotate 顺时针（渲染约定）
    uint32_t color = 0x00FFFFFF;
    Rect bounds;            // 字形包围盒（未旋转；UI 计算维护，供布局）
};

}
