#pragma once
#include "Whiteboard/element/mind_map.h"
#include "Whiteboard/geometry/point.h"
#include "Whiteboard/geometry/rect.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace whiteboard {

// 思维导图布局（纯几何，无 UI 依赖）：按树结构计算可见节点框与连线。
// 样式为经典左右平衡思维导图——根居中，直接子节点偶数序在右 / 奇数序在左，
// 更深层继承父侧向；同侧子树块垂直居中堆叠，同深度层列对齐。
// collapsed 节点的子树不参与布局；结果几何为"数据态"（含 root 平移与 scale 缩放）。

namespace mind_layout_detail {

inline int Round(double v) { return static_cast<int>(std::lround(v)); }

// 节点尺寸档位（scale=1 自然坐标）：根 / 一级 / 二级及更深
inline double NodeWidth(int depth) { return depth == 0 ? 132.0 : (depth == 1 ? 104.0 : 88.0); }
inline double NodeHeight(int depth) { return depth == 0 ? 56.0 : (depth == 1 ? 44.0 : 36.0); }

constexpr double kHGap = 48.0;       // 相邻层水平间距（边缘到边缘）
constexpr double kRowGap = 20.0;     // 同侧相邻子树块垂直间距
constexpr double kBtnRadius = 9.0;   // 钮圆半径（折叠 / + / ×）
constexpr double kBtnOffset = 14.0;  // 钮圆心距框边缘

// 从矩形中心沿方向 (dx, dy) 到矩形边界的交点（连线端点到节点边缘）。
// 括号包裹 min：防御 Windows min/max 宏（包含 windows.h 的宿主目标）
inline Point RectEdgePoint(const Rect& r, double dx, double dy)
{
    const double cx = r.x + r.width / 2.0;
    const double cy = r.y + r.height / 2.0;
    double t = 1e18;
    if (std::abs(dx) > 1e-9)
        t = (std::min)(t, (r.width / 2.0) / std::abs(dx));
    if (std::abs(dy) > 1e-9)
        t = (std::min)(t, (r.height / 2.0) / std::abs(dy));
    if (t > 1e17)
        return Point(Round(cx), Round(cy));
    return Point(Round(cx + dx * t), Round(cy + dy * t));
}

// 自然坐标（根在原点、scale=1）先序节点
struct RawNode {
    const MindNode* node;
    int depth;
    bool right;  // 位于根右侧；根记为 true
    double cx, cy;  // 节点中心
};

struct Builder {
    std::vector<RawNode> nodes;

    // 子树块高：叶子 = 自身高；否则 = 子树块之和 + 行距（不小于自身高）
    double BlockHeight(const MindNode* n, int depth) const
    {
        const double self = NodeHeight(depth);
        if (n->collapsed || n->children.empty())
            return self;
        double sum = 0;
        size_t cnt = 0;
        for (const auto& c : n->children)
        {
            if (!c)
                continue;
            sum += BlockHeight(c.get(), depth + 1);
            ++cnt;
        }
        if (cnt == 0)
            return self;
        sum += kRowGap * (cnt - 1);
        return (std::max)(self, sum);
    }

    void AssignGroup(const std::vector<const MindNode*>& list, bool right,
                     int depth, double cx, double cy)
    {
        if (list.empty())
            return;
        std::vector<double> bh(list.size());
        double total = 0;
        for (size_t i = 0; i < list.size(); ++i)
        {
            bh[i] = BlockHeight(list[i], depth + 1);
            total += bh[i];
        }
        total += kRowGap * (list.size() - 1);
        const double dir = right ? 1.0 : -1.0;
        double cy0 = cy - total / 2.0;
        for (size_t i = 0; i < list.size(); ++i)
        {
            const double childCy = cy0 + bh[i] / 2.0;
            const double childCx =
                cx + dir * (NodeWidth(depth) / 2.0 + kHGap + NodeWidth(depth + 1) / 2.0);
            Assign(list[i], depth + 1, right, childCx, childCy);
            cy0 += bh[i] + kRowGap;
        }
    }

    void Assign(const MindNode* n, int depth, bool right, double cx, double cy)
    {
        nodes.push_back({ n, depth, right, cx, cy });
        if (n->collapsed || n->children.empty())
            return;
        if (depth == 0)
        {
            // 根：直接子节点偶数序在右 / 奇数序在左，两侧组各自垂直居中
            std::vector<const MindNode*> rightList, leftList;
            for (size_t i = 0; i < n->children.size(); ++i)
            {
                if (!n->children[i])
                    continue;
                (i % 2 == 0 ? rightList : leftList).push_back(n->children[i].get());
            }
            AssignGroup(rightList, true, depth, cx, cy);
            AssignGroup(leftList, false, depth, cx, cy);
        }
        else
        {
            std::vector<const MindNode*> list;
            for (const auto& c : n->children)
            {
                if (c)
                    list.push_back(c.get());
            }
            AssignGroup(list, right, depth, cx, cy);
        }
    }
};

}  // namespace mind_layout_detail

// 可见节点框（值化：只存 nodeId 字符串，不持数据层指针，可安全跨线程缓存）
struct MindNodeBox {
    std::string nodeId;
    int depth = 0;
    bool right = true;    // 位于根右侧（左侧为镜像）；根记为 true
    Rect frame;           // 节点框（数据态坐标）
    bool hasFold = false; // 有子节点 → 显示折叠钮
    bool collapsed = false; // 子树折叠状态（折叠钮符号 +/− 用）
    Point foldBtn;        // 折叠钮圆心（hasFold 时有效；位于"远离根一侧"外缘）
};

// 连线：父框边缘 → 子框边缘（数据态坐标）
struct MindLink {
    Point a, b;
};

struct MindLayout {
    std::vector<MindNodeBox> boxes;
    std::vector<MindLink> links;
    Rect bounds;  // 全部节点框并集 + 外扩（包住折叠钮与聚焦 +/× 钮）

    const MindNodeBox* FindNode(const std::string& nodeId) const
    {
        for (const auto& b : boxes)
        {
            if (b.nodeId == nodeId)
                return &b;
        }
        return nullptr;
    }

    // 命中节点框（数据坐标）
    const MindNodeBox* FindByFrame(int x, int y) const
    {
        for (const auto& b : boxes)
        {
            if (b.frame.Contains(x, y))
                return &b;
        }
        return nullptr;
    }

    // 命中折叠钮（数据坐标，容差 kBtnRadius+3）
    const MindNodeBox* FindFoldBtn(int x, int y) const
    {
        const double r = mind_layout_detail::kBtnRadius + 3;
        for (const auto& b : boxes)
        {
            if (!b.hasFold)
                continue;
            const double dx = x - b.foldBtn.x;
            const double dy = y - b.foldBtn.y;
            if (dx * dx + dy * dy <= r * r)
                return &b;
        }
        return nullptr;
    }
};

// 聚焦节点 "+"（顶边中点上方）与 "×"（底边中点下方）钮圆心；
// 聚焦态只有 UI 知道，故不在布局结果里，由渲染/命中层按框补算。
inline Point MindAddBtnPos(const MindNodeBox& box)
{
    return Point(box.frame.x + box.frame.width / 2,
                 box.frame.y - mind_layout_detail::Round(mind_layout_detail::kBtnOffset));
}

inline Point MindDelBtnPos(const MindNodeBox& box)
{
    return Point(box.frame.x + box.frame.width / 2,
                 box.frame.y + box.frame.height + mind_layout_detail::Round(mind_layout_detail::kBtnOffset));
}

inline MindLayout ComputeMindMapLayout(const MindMapElement& m)
{
    using namespace mind_layout_detail;
    MindLayout layout;
    if (!m.rootNode)
        return layout;

    Builder builder;
    builder.Assign(m.rootNode.get(), 0, true, 0.0, 0.0);

    // 退化防御：scale 为 0/负时取极小正值（保持可命中）
    const double sx = std::abs(m.scaleX) > 1e-6 ? m.scaleX : 1e-6;
    const double sy = std::abs(m.scaleY) > 1e-6 ? m.scaleY : 1e-6;

    // 自然坐标 → 数据态（root 平移 + scale 缩放），并建 nodeId 索引
    std::unordered_map<std::string, size_t> indexOf;
    indexOf.reserve(builder.nodes.size());
    layout.boxes.reserve(builder.nodes.size());
    for (const auto& rn : builder.nodes)
    {
        const double w = NodeWidth(rn.depth) * sx;
        const double h = NodeHeight(rn.depth) * sy;
        const double dcx = m.root.x + rn.cx * sx;
        const double dcy = m.root.y + rn.cy * sy;
        MindNodeBox box;
        box.nodeId = rn.node->id;
        box.depth = rn.depth;
        box.right = rn.right;
        box.frame = Rect(Round(dcx - w / 2), Round(dcy - h / 2),
                         (std::max)(Round(w), 1), (std::max)(Round(h), 1));
        box.hasFold = !rn.node->children.empty();
        box.collapsed = rn.node->collapsed;
        if (box.hasFold)
        {
            const int cyCenter = box.frame.y + box.frame.height / 2;
            box.foldBtn = rn.right
                ? Point(box.frame.x + box.frame.width + Round(kBtnOffset), cyCenter)
                : Point(box.frame.x - Round(kBtnOffset), cyCenter);
        }
        indexOf[box.nodeId] = layout.boxes.size();
        layout.boxes.push_back(std::move(box));
    }

    // 连线：父框边缘 → 子框边缘（折叠子树不画）
    for (size_t i = 0; i < builder.nodes.size(); ++i)
    {
        const MindNode* n = builder.nodes[i].node;
        if (n->collapsed)
            continue;
        const MindNodeBox& pbox = layout.boxes[i];
        for (const auto& c : n->children)
        {
            if (!c)
                continue;
            const auto it = indexOf.find(c->id);
            if (it == indexOf.end())
                continue;
            const MindNodeBox& cbox = layout.boxes[it->second];
            const double dx = (cbox.frame.x + cbox.frame.width / 2.0)
                            - (pbox.frame.x + pbox.frame.width / 2.0);
            const double dy = (cbox.frame.y + cbox.frame.height / 2.0)
                            - (pbox.frame.y + pbox.frame.height / 2.0);
            MindLink link;
            link.a = RectEdgePoint(pbox.frame, dx, dy);
            link.b = RectEdgePoint(cbox.frame, -dx, -dy);
            layout.links.push_back(link);
        }
    }

    // 包围盒：全部节点框并集 + 外扩（包住折叠钮与聚焦 +/× 钮）
    if (!layout.boxes.empty())
    {
        int minX = layout.boxes[0].frame.x;
        int minY = layout.boxes[0].frame.y;
        int maxX = layout.boxes[0].frame.x + layout.boxes[0].frame.width;
        int maxY = layout.boxes[0].frame.y + layout.boxes[0].frame.height;
        for (const auto& b : layout.boxes)
        {
            minX = (std::min)(minX, b.frame.x);
            minY = (std::min)(minY, b.frame.y);
            maxX = (std::max)(maxX, b.frame.x + b.frame.width);
            maxY = (std::max)(maxY, b.frame.y + b.frame.height);
        }
        const int margin = Round(kBtnOffset + kBtnRadius);
        layout.bounds = Rect(minX - margin, minY - margin,
                             (std::max)(maxX - minX + margin * 2, 1),
                             (std::max)(maxY - minY + margin * 2, 1));
    }
    return layout;
}

}
