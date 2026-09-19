#include "Whiteboard/element/mind_map.h"
#include <atomic>
#include <chrono>

namespace whiteboard {

// 纳秒时间 + 原子计数：单次 Reset 循环内批量生成节点 id 也保证全局唯一
std::string MakeMindNodeId()
{
    static std::atomic<uint64_t> counter{ 0 };
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count())
        + "-" + std::to_string(counter.fetch_add(1));
}

namespace {

MindNode* FindNodeRec(const std::shared_ptr<MindNode>& node, const std::string& nodeId)
{
    if (!node)
        return nullptr;
    if (node->id == nodeId)
        return node.get();
    for (const auto& child : node->children)
    {
        if (MindNode* found = FindNodeRec(child, nodeId))
            return found;
    }
    return nullptr;
}

MindNode* FindParentRec(const std::shared_ptr<MindNode>& node, const std::string& nodeId)
{
    if (!node)
        return nullptr;
    for (const auto& child : node->children)
    {
        if (!child)
            continue;
        if (child->id == nodeId)
            return node.get();
        if (MindNode* found = FindParentRec(child, nodeId))
            return found;
    }
    return nullptr;
}

}  // namespace

MindMapElement::MindMapElement()
{
    Reset();
}

void MindMapElement::Reset()
{
    std::chrono::system_clock::duration d =
        std::chrono::system_clock::now().time_since_epoch();
    id = std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
    root = Point();
    scaleX = 1.0f;
    scaleY = 1.0f;
    rotation = 0.0f;
    width = 3;
    color = 0x00FFFFFF;

    // 默认树：根 + 4 一级分支（偶数序在右 / 奇数序在左）+ 每分支 1 个二级子节点，
    // 保证放置后即可演示折叠/展开与节点增删。
    rootNode = std::make_shared<MindNode>();
    rootNode->id = MakeMindNodeId();
    for (int i = 0; i < 4; ++i)
    {
        auto branch = std::make_shared<MindNode>();
        branch->id = MakeMindNodeId();
        auto leaf = std::make_shared<MindNode>();
        leaf->id = MakeMindNodeId();
        branch->children.push_back(leaf);
        rootNode->children.push_back(branch);
    }
}

MindNode* MindMapElement::FindNode(const std::string& nodeId)
{
    if (nodeId.empty())
        return nullptr;
    return FindNodeRec(rootNode, nodeId);
}

MindNode* MindMapElement::FindParent(const std::string& nodeId)
{
    if (nodeId.empty() || !rootNode || rootNode->id == nodeId)
        return nullptr;
    return FindParentRec(rootNode, nodeId);
}

}
