# geometry — 几何值类型

目录：`src/Whiteboard/geometry/`

纯值类型模块，无序列化、无网络依赖。类型清单：

| 类型 | 头文件 | 用途 |
| --- | --- | --- |
| `PointTemplate<T>` / `Point` / `FloatPoint` | [point.h](../geometry/point.h) | 二维点（整数/浮点） |
| `Rect` | [rect.h](../geometry/rect.h) | 轴对齐矩形 |
| `BoundaryRect` | [boundary_rect.h](../geometry/boundary_rect.h) | min/max 增量包围盒 |
| `Linesegment` | [line_segment.h](../geometry/line_segment.h) | 线段（含矩形求交） |
| `Transform` | [transform.h](../geometry/transform.h) | 世界 ↔ 屏幕坐标变换 |

## PointTemplate

```cpp
template<class T>
class PointTemplate {
public:
    T x, y;
    static uint32_t Distance(const PointTemplate<T>& pt1, const PointTemplate<T>& pt2);
};

using Point      = PointTemplate<int>;    // 整数点，笔画点集使用
using FloatPoint = PointTemplate<float>;  // 浮点，插值计算使用
```

- 两个 `int` 构造参数的重载构造函数（`PointTemplate(int xx, int yy)`），对 `Point` 可直接 `Point(x, y)`。
- `Distance` 返回欧氏距离取整（`uint32_t`）。
- 提供模板 `operator==` 逐字段比较。

## Rect

```cpp
class Rect {
public:
    Rect();                               // (0,0,0,0)
    Rect(int xx, int yy, int ww, int hh); // x, y, width, height
    int x, y, width, height;
};
```

访问器与判定：

| 成员 | 说明 |
| --- | --- |
| `GetLeft/GetTop` | 左/上边界（即 x/y） |
| `GetRight/GetBottom` | 右/下边界，闭区间语义：`x + width - 1`、`y + height - 1` |
| `GetTopLeft/GetBottomRight/GetTopRight/GetBottomLeft` | 四角 `Point` |
| `Contains(int, int)` / `Contains(const Point&)` | 点是否在矩形内（含边界，`< width`/`< height` 上限开） |
| `Intersect(const Rect&)` | 求交集（原地与拷贝两个重载）；不相交时结果宽高为 0 |
| `Intersects(const Rect&)` | 是否有非空交集 |

注意：右/下边界是**闭区间**（`x + width - 1`），而 `Contains` 的上限是开的，两者配合保证"包含下边界、不包含右上开边界"的语义，橡皮擦判定依赖此行为。

## BoundaryRect

```cpp
struct BoundaryRect {
    BoundaryRect();               // min = kDefaultMinValue(9999999)，max = 0
    BoundaryRect(const Rect& rx); // 由矩形换算
    int minx, maxx, miny, maxy;

    Rect ToRect() const;          // 转 Rect；宽/高为 0 时强制为 1
    void Update(int x, int y);    // 增量扩展：逐点并入包围盒
};
```

- 设计为增量包围盒：初始 min 极大、max 为 0，`Update` 逐点收紧边界。
- `ToRect` 保证非零尺寸（0 宽/高强制 1），便于后续 `Rect::Intersects` 等几何判定。
- 由 `Rect` 构造时，`maxx = rx.x + rx.width`（右开边界），与 `Rect::GetRight` 的闭区间语义不同，仅作快速包围判定用。

## Linesegment

```cpp
class Linesegment {
public:
    Linesegment(Point b, Point e);
    Point Intersection(const Rect& rc);                 // 与矩形四条边求交
    bool  Intersection(const Linesegment& L2, Point& p); // 线段与线段求交
private:
    Point start, end;
};
```

- `Intersection(const Rect&)`：依次与矩形左、右、上、下四边求交，返回第一个交点；调用方需保证线段与矩形相交，否则返回默认值或 `start`。
- `Intersection(const Linesegment&, Point&)`：参数化线段求交，成功返回 true 并写入交点（四舍五入为整数）；平行/共线/不相交返回 false。
- 主要被 `Page::Eraser` 用于计算笔画与擦除矩形边界的交点（见 [page.md](page.md)）。

## Transform

世界坐标 ↔ 屏幕坐标的仿射变换（缩放 + 平移 + 旋转）：

```cpp
class Transform {
public:
    float scale = 1.0f;       // 缩放因子
    float translateX = 0.0f;  // 平移 X
    float translateY = 0.0f;  // 平移 Y
    float rotation = 0.0f;    // 旋转角（弧度，绕原点逆时针）

    Point WorldToScreen(const Point& p) const;
    Point ScreenToWorld(const Point& p) const;
    void  ZoomAt(float factor, const Point& anchor);
    void  PanBy(float dx, float dy);
    void  RotateBy(float radians);
    void  Reset();
};
```

映射公式：

\[
\text{screen} = \text{Rotate}(\text{rotation}) \cdot (\text{world} \times \text{scale}) + \text{translate}
\]

- `ZoomAt(factor, anchor)`：以世界坐标锚点 `anchor` 为中心缩放（缩放后锚点屏幕位置不变），适合"以鼠标位置为中心"的缩放。
- `PanBy` / `RotateBy`：增量平移 / 增量旋转。
- `Reset`：恢复恒等变换。
- `Page` 持有该变换用于画布视图状态（见 [page.md](page.md)），序列化布局见 [protocol.md](protocol.md)。
