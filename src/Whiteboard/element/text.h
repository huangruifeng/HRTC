#pragma once
#include "Whiteboard/element/element.h"
#include <cstdint>
#include <string>

namespace whiteboard {

// 文字元素：纯文本 + 锚点/字号/旋转/颜色参数。
// 数据层不存字形包围盒——字体度量依赖渲染端（Qt）字体引擎，由 UI 端
// 用 QPainterPath::addText 的 boundingRect 计算；橡皮擦仅作用于 Stroke，
// 本类型天然不可擦除。整体变换（移动/缩放/旋转）参数写回本元素。
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
};

}
