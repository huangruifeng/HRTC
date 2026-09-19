#pragma once
#include "Whiteboard/element/element.h"
#include "Whiteboard/geometry/point.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace whiteboard {

// 思维导图节点：树结构。节点位置不存点——由布局算法
// （geometry/mind_map_layout.h）按树结构计算（左右平衡布局）。
// v1 节点无文字；collapsed = true 时子树不参与布局与渲染。
struct MindNode {
    std::string id;  // 节点 id（chrono 唯一生成，远端同步/编辑定位用）
    bool collapsed = false;
    std::vector<std::shared_ptr<MindNode>> children;
};

// 生成思维导图节点唯一 id（纳秒时间 + 原子计数）；运行期添加子节点也用它
std::string MakeMindNodeId();

// 思维导图元素：根节点树 + 根位置/整体缩放/旋转参数。
// 与表格同为容器型元素（内部承载树状结构），橡皮擦仅作用于 Stroke，
// 本类型天然不可擦除。整体变换（移动/缩放/旋转）参数写回本元素。
class MindMapElement : public Element {
public:
    MindMapElement();

    std::string GetType() const override { return "MindMap"; }

    // chrono id + 默认树：根 + 4 一级分支（左右各 2）+ 每分支 1 个二级子节点
    void Reset();

    // 递归查找节点（空 id / 未找到返回 nullptr）
    MindNode* FindNode(const std::string& nodeId);
    // 递归查找父节点（删除节点用）；根节点或无父时返回 nullptr
    MindNode* FindParent(const std::string& nodeId);

    Point root;             // 根节点中心（页面绝对坐标）
    float scaleX = 1.0f;    // 整体缩放（布局几何 = 自然几何 × scale）
    float scaleY = 1.0f;
    float rotation = 0.0f;  // 度，正值 = Qt rotate 顺时针（渲染约定）
    int width = 3;          // 描边线宽
    uint32_t color = 0x00FFFFFF;
    std::shared_ptr<MindNode> rootNode;  // 树根（恒非空）
};

}
