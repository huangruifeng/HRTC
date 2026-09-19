#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/rect.h"
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

namespace whiteboard {

// 表格元素：等宽等行高网格 + 单元格子元素容器（按 row*cols+col 索引）。
// 单元格内存放通用 Element（坐标为页面绝对坐标；v1 实际存笔迹，
// 未来可直接容纳图形/文本/嵌套表格）。橡皮擦仅作用于 Stroke，
// 网格本身与单元格内非笔画元素天然不可擦除。
class TableElement : public Element {
public:
    TableElement();

    std::string GetType() const override { return "Table"; }

    void Reset();  // chrono id + 默认 3x3

    int CellCount() const { return rows * cols; }

    // 返回点所在的单元格索引（row*cols+col）；不在表格内返回 -1。
    // 旋转按逆变换回表内轴对齐坐标后判定（rotation 为 Qt 顺时针正角）。
    int CellIndexAt(const Point& p) const;

    Rect bounds;            // 未旋转外框（页面绝对坐标）
    float rotation = 0.0f;  // 度，正值 = Qt rotate 顺时针（渲染约定）
    int rows = 3;
    int cols = 3;
    int width = 2;          // 网格线宽
    uint32_t color = 0x00FFFFFF;
    // 单元格子元素容器（row*cols+col 索引，坐标为页面绝对坐标）
    std::vector<std::list<std::shared_ptr<Element>>> cells;
};

}
