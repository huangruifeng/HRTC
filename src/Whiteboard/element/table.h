#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/rect.h"
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

namespace whiteboard {

// 表格元素：网格由 origin/rotation/rows/cols + 每格初始尺寸描述。
// 单元格内存放通用 Element（坐标为格局部坐标——相对所在格左上角）；
// 入格类型为笔迹/图形/文字。表格实际宽高不落数据，由 ComputeLayout 从
// 各格内容推导（列宽/行高 = 该列/行内最大内容 + kCellPad，下限 = 初始
// 格尺寸 minCellW/H），数据层与渲染层共用同一布局保证单源一致。
// 橡皮擦仅作用于 Stroke，网格本身与格内非笔画元素天然不可擦除。
class TableElement : public Element {
public:
    TableElement();

    std::string GetType() const override { return "Table"; }

    void Reset();  // chrono id + 默认 3x3

    int CellCount() const { return rows * cols; }

    // 内容驱动布局（布局局部坐标：左上角为 (0,0)，未旋转）。
    struct Layout {
        std::vector<double> colW;  // 各列宽（≥ minCellW）
        std::vector<double> rowH;  // 各行高（≥ minCellH）
        double totalW = 0.0;       // 总宽 = ΣcolW
        double totalH = 0.0;       // 总高 = ΣrowH

        // 第 idx 格（row*cols+col）左上角（布局局部坐标；越界返回 (0,0)）
        void CellOrigin(int idx, double& ox, double& oy) const;
        // 第 idx 格矩形（布局局部坐标；越界返回空矩形）
        Rect CellRect(int idx) const;
    };
    Layout ComputeLayout() const;

    // 元素内容外接矩形（元素自身坐标系；无内容/不支持的类型返回 false）。
    // Stroke/Graphic 用包围盒，Text 用字形包围盒 bounds。
    static bool ContentBounds(const Element& e, Rect& out);

    // 返回点所在的单元格索引（row*cols+col）；不在表格内返回 -1。
    // 旋转按逆变换回表内轴对齐坐标后，按列宽/行高累计定位。
    int CellIndexAt(const Point& p) const;

    static constexpr double kCellPad = 4.0;  // 内容与格边的内边距

    Point origin;           // 网格左上角（未旋转页面坐标）
    float rotation = 0.0f;  // 度，正值 = Qt rotate 顺时针（渲染约定）
    int rows = 3;
    int cols = 3;
    int width = 2;          // 网格线宽
    uint32_t color = 0x00FFFFFF;
    float minCellW = 120.0f;  // 初始格宽（创建拖拽矩形均分）= 回缩下限
    float minCellH = 80.0f;   // 初始格高（同上）
    // 单元格子元素容器（row*cols+col 索引，坐标为格局部坐标）
    std::vector<std::list<std::shared_ptr<Element>>> cells;
};

}
