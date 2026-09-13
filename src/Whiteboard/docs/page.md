# page — 页面

目录：`src/Whiteboard/page/`（[page.h](../page/page.h)、[page.cpp](../page/page.cpp)）

`Page` 是白板的单页数据容器，本身也是一个 `Element`（类型名 `"Page"`），因此可以随 `FullSync` 命令整体序列化传输。

## Page

```cpp
class Page : public Element {
public:
    Page();
    std::string GetType() const override { return "Page"; }

    void Append(const std::shared_ptr<Element>& e);
    void Append(const Stroke& s);                    // 便捷重载：包装为 shared_ptr<Stroke>
    void Delete(const std::string& id);
    void Clear();
    bool UpdateStroke(const std::string& id, const std::vector<Point>& points);

    std::shared_ptr<Page> Clone() const;             // 深拷贝（撤销/重做历史快照）

    EraserResult Eraser(const Rect& rc, int sid);     // 带会话插值的擦除
    EraserResult Eraser(const Rect& rc);              // 单次矩形擦除

    void EnableEraserInsert(bool enable);

    Point WorldToScreen(const Point& p) const;        // 委托 transform
    Point ScreenToWorld(const Point& p) const;

    std::string pageId;
    std::list<std::shared_ptr<Element>> elements;
    Transform transform;
    // ... 橡皮插值内部状态（enableEraserInsert / eraserSessionId / insertInstance / insertPoints）
};
```

| 成员 | 说明 |
| --- | --- |
| `pageId` | 页面标识；网络命令基类 `Command::pageId` 即指向它 |
| `elements` | 元素列表（`std::list`，橡皮擦切分时需在迭代中插入/删除） |
| `transform` | 画布视图变换（缩放/平移/旋转），见 [geometry.md](geometry.md) |

## 元素管理

- `Append(e)` / `Append(s)`：追加元素到列表尾。
- `Delete(id)`：按 id 删除元素（`remove_if`）。
- `Clear()`：清空全部元素。
- `UpdateStroke(id, points)`：**变换烘焙**——移动/旋转/缩放完成后，UI 层把映射后的新点集写回指定笔画；id/color/width 保持不变，`rawPoints` 一并覆盖，包围盒按新点集重算。找到目标返回 true，否则返回 false。

## 橡皮擦

### EraserResult

```cpp
struct EraserResult {
    std::vector<std::string> removedIds;                  // 本次擦除删掉的笔画 id
    std::vector<std::shared_ptr<Element>> addedElements;  // 本次切分产生的新笔画片段
};
```

一次擦除的精确增量结果：远端按 `removedIds` 删、按 `addedElements` 加，即可与操作端数据层完全一致（`EraserEnd` 命令携带同一结构，见 [command.md](command.md)）。

### Eraser(rc) — 单矩形擦除算法

对与 `rc` 相交的每条 `Stroke`：

1. 逐点判定 `rc.Contains`，收集"内→外 / 外→内"的边界迭代器到 `partPoints`。
2. 按结果分三种情形：
   - 全部点在内（`partPoints` 空）→ 整条笔画删除；
   - 仅一个边界且首点在外 → 无交集，跳过；
   - 其余 → 把笔画**切分为多段新 `Stroke`**：每段保留原 color/width，调用 `Reset()` 分配唯一 id，首尾补上与擦除矩形边界的交点（`PointOfIntersection` 用 `Linesegment` 求交，矩形外扩 1px 保证交点落入段内），包围盒逐点重建。
3. 原笔画删除，新片段插入原位；`removedIds` 记原 id、`addedElements` 记新片段。

### Eraser(rc, sid) — 会话插值擦除

一次拖动擦除会产生连续多个小矩形。带会话 id 的版本：

- 需先 `EnableEraserInsert(true)`（`Page` 构造默认关闭；**反序列化后协议层统一开启**，见 [protocol.md](protocol.md)）。
- 同一 `sid` 内，把相邻擦除矩形左上角按 `insertInstance`（=5）间距插值补点，形成连续轨迹后逐点执行单矩形擦除；`sid` 变化时清空轨迹重来。
- 结果合并所有子擦除的 `removedIds`（去重）与 `addedElements`。

## Clone — 深拷贝

撤销/重做历史快照用：

- 复制 `pageId`、`transform`、橡皮插值状态；
- 元素逐个克隆：`Stroke` 深拷贝（`make_shared<Stroke>(*stroke)`，点集独立）；未知类型保持共享引用（防御）。

接收端维护与发送端一致的操作历史（最近 20 次），依赖快照与 `Undo`/`Redo` 命令（见 [command.md](command.md)）。

## 坐标转换

`WorldToScreen` / `ScreenToWorld` 直接委托 `transform`，详见 [geometry.md](geometry.md) 的 `Transform`。
