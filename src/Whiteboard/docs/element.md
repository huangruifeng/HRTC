# element — 元素模型

目录：`src/Whiteboard/element/`

白板内容的抽象基类与内置笔画类型。元素以 `std::shared_ptr<Element>` 存于 `Page::elements`（见 [page.md](page.md)）。

| 类型 | 头文件 | 说明 |
| --- | --- | --- |
| `Element` | [element.h](../element/element.h) | 抽象基类 + 工厂注册 |
| `Stroke` | [stroke.h](../element/stroke.h) | 内置笔画元素，类型名 "Stroke" |

## Element 基类

```cpp
class Element {
public:
    virtual ~Element() = default;
    virtual std::string GetType() const = 0;  // 类型名标签
    std::string id;                            // 所有元素共享的标识
};
```

- `GetType()` 返回类型名：内置 `Stroke` 返回 `"Stroke"`、`Page` 返回 `"Page"`（`Page` 本身也是元素，见 [page.md](page.md)）；外部元素类型可返回任意自定义字符串，无需修改本库。
- `id` 是元素的唯一标识，UI 增量更新按 id 精确增删。

### 工厂注册

```cpp
using ElementFactory = std::shared_ptr<Element> (*)();

bool RegisterElementFactory(const std::string& type, ElementFactory factory); // 已注册返回 false
std::shared_ptr<Element> CreateElement(const std::string& type);               // 未知类型返回 nullptr
```

- 内置注册表（[element.cpp](../element/element.cpp)）：`"Stroke"`、`"Page"`。
- 反序列化时协议层调用 `CreateElement(type)` 重建对象（见 [protocol.md](protocol.md)）。
- 扩展新元素类型：实现 `Element` 子类 → `RegisterElementFactory` 注册 → 在协议层补齐序列化分支。

## Stroke

```cpp
class Stroke : public Element {
public:
    Stroke();
    std::string GetType() const override { return "Stroke"; }

    void Append(const Point& point);
    void Reset();   // 重新生成 chrono 纳秒 id 并清空全部字段

    // 静态插值工具
    static void  InsertPoint(std::vector<Point>& points, const Point& end, uint32_t minDistance);
    static Point GetInterPoint(const FloatPoint& begin, const FloatPoint& end, uint32_t distance);

    int                width;
    uint32_t           color;
    std::vector<Point> points;    // 渲染点集（含插值点）
    std::vector<Point> rawPoints; // 仅用户绘制点，不含插值点
    int                minDistance;
    BoundaryRect       bounding;
};
```

### 字段

| 字段 | 说明 |
| --- | --- |
| `width` | 线宽（像素） |
| `color` | 颜色（RGBA 打包为 `uint32_t`） |
| `points` | 渲染点集：用户绘制点 + 自动插值点（线段平滑） |
| `rawPoints` | 仅用户绘制点，不含插值点；**序列化只传此字段**，反序列化后重建 `points`（见 [protocol.md](protocol.md)） |
| `minDistance` | 插值最小间距，`Reset()` 设为 20 |
| `bounding` | 增量包围盒，随 `Append` 更新；用于橡皮擦快速命中判定 |

### 关键行为

- `Reset()`：以 `system_clock` 纳秒时间戳字符串作为 `id`（`std::to_string(chrono::duration_cast<nanoseconds>(...).count())`），并把 width/color/点集/包围盒全部复位、`minDistance` 置 20。构造函数即调用 `Reset()`，因此每次新建/切分出的笔画都带唯一 id。
- `Append(point)`：先按 `minDistance` 在旧末尾与新增点之间插值补点，再把原始点分别追加进 `points` 与 `rawPoints`，并更新 `bounding`。
- `InsertPoint(points, end, minDistance)`：若 `end` 与点集末尾距离大于 `minDistance`，在两点之间按间距插值补点（写入 `points`，不写 `rawPoints`）。
- `GetInterPoint(begin, end, distance)`：直线（含竖直）上距 `begin` 为 `distance` 的插值点；竖直情形直接沿 y 轴计算，其余按斜截式解二次方程取线段内的解。

### 典型用法

```cpp
whiteboard::Stroke stroke;                 // 构造即分配唯一 id
stroke.width = 4;
stroke.color = 0xFFE53935;                 // RGBA
stroke.Append(Point(100, 100));            // 首个点：直接追加
stroke.Append(Point(180, 160));            // 与上一点距离 > 20 时自动插值
```

距离较远的相邻采样点会被插值补点，保证远程端复现出的线段连续平滑；`rawPoints` 保留原始采样用于序列化压缩。
