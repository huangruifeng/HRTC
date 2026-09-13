# protocol — msgpack 编解码

目录：`src/Whiteboard/protocol/`

网络二进制 ↔ 内存对象的转换层，基于 msgpack。数据类（geometry/element/page/command）本身不依赖 msgpack，适配器全部定义在本层。

| 文件 | 职责 |
| --- | --- |
| [geometry_codec.h](../protocol/geometry_codec.h) | geometry 值类型的 msgpack `pack`/`convert` 适配器 |
| [extend.h](../protocol/extend.h) | `Extend` 模板序列化助手 |
| [element_codec.h](../protocol/element_codec.h) / [.cpp](../protocol/element_codec.cpp) | 元素（Stroke/Page/多态 Element）编解码 |
| [command_codec.h](../protocol/command_codec.h) / [.cpp](../protocol/command_codec.cpp) | 命令编解码（入口函数） |
| [protocol.h](../protocol/protocol.h) | 协议层聚合头 |

## 线格式总述

所有数据打包为 msgpack array，字段位置固定：

| 对象 | 布局 |
| --- | --- |
| 命令 | `[type_str, pageId, payload]`（3 元组；payload 为 array，布局按命令类型，见下表） |
| 多态元素 | `[type_str, fields]`（2 元组；fields 按元素类型） |

### 命令入口

```cpp
std::string SerializeCommand(const Command& cmd);          // 对象 → 二进制
std::shared_ptr<Command> DeserializeCommand(const std::string& data); // 二进制 → 对象
```

`DeserializeCommand`：先读 `type_str` → `CreateCommand(type)` 建实例（未知类型返回 `nullptr`）→ 按类型填充 `pageId` 与 payload；结构非法（非 array / 元素个数不符）抛 `msgpack::type_error`。

## geometry 适配器（geometry_codec.h）

| 类型 | 线布局 |
| --- | --- |
| `PointTemplate<T>`（`Point`） | `[x, y]` |
| `Rect` | `[x, y, width, height]` |
| `BoundaryRect` | `[minx, maxx, miny, maxy]` |
| `Transform` | `[scale, translateX, translateY, rotation]` |

反序列化（`convert`）均校验 array 元素个数，不符抛 `msgpack::type_error`。

## Extend 模板助手（extend.h）

对具备 msgpack 适配器的任意类型做独立序列化：

```cpp
std::string wire = whiteboard::protocol::Extend::Serialize(someRect);
whiteboard::Rect r = whiteboard::protocol::Extend::Deserialize<whiteboard::Rect>(wire);
```

## 元素编码（element_codec.h/.cpp）

多态元素：`[type_str, fields]`。具体字段布局（无 type tag）：

| 类型 | 字段布局 | 说明 |
| --- | --- | --- |
| `Stroke` | `[id, width, color, rawPoints]` | **只传 `rawPoints`**；反序列化后 `points = rawPoints` 并重建 `bounding`，`minDistance` 保持默认 20 |
| `Page` | `[id, pageId, transform, [Element...]]` | 元素列表内嵌多态元素，可递归 |

接口：

```cpp
std::string SerializeStroke(const Stroke& stroke);
Stroke DeserializeStroke(const std::string& data);

std::string SerializePage(const Page& page);
Page DeserializePage(const std::string& data);

std::string SerializeElement(const Element& e);                  // 多态
std::shared_ptr<Element> DeserializeElement(const std::string& data);

void PackElement(msgpack::packer<msgpack::sbuffer>& pk, const Element& e);       // 命令内嵌用
std::shared_ptr<Element> UnpackElement(const msgpack::object& obj);              // 未知类型返回 nullptr
```

`PackElement`/`UnpackElement` 用于把元素嵌入命令 payload（如 `ElementAdd`、`EraserEnd` 的 `addedElements`、`FullSync` 的 `pages`）；未识别类型的元素打包为 `nil`，解包返回 `nullptr` 并被调用方跳过。

## 命令 payload 布局（command_codec.cpp）

命令整体：`[type_str, pageId, payload]`。各命令 payload：

| 命令 | payload 布局 |
| --- | --- |
| `StrokeBegin` | `[strokeId, width, color, points]` |
| `StrokeMove` | `[strokeId, points]` |
| `StrokeEnd` | `[strokeId, points]` |
| `EraserBegin` | `[sessionId, points]` |
| `EraserMove` | `[sessionId, points]` |
| `EraserEnd` | `[sessionId, removedIds, [Element...]]`（addedElements 多态编码） |
| `ElementAdd` | `[Element]`（多态元素） |
| `ElementRemove` | `[elementId]` |
| `FullSync` | `[[Element...]]`（pages，逐页 `PackElement`；解包后 `dynamic_pointer_cast<Page>` 收集） |
| `StrokeUpdate` | `[strokeId, points]` |
| `LassoPreview` | `[sessionId, points]` |
| `SelectionPreview` | `[sessionId, points, selectedIds]` |
| `PageCreate` | `[newPageId]` |
| `PageSelect` / `PageDelete` / `PageClear` / `Undo` / `Redo` | `[]`（空；所有信息在基类 `pageId`） |

## 反序列化注意事项

- **未知命令类型**：`CreateCommand` 返回 nullptr → `DeserializeCommand` 返回 `nullptr`，调用方自行忽略。
- **未知元素类型**：`UnpackElement` 返回 `nullptr`；`EraserEnd`/`FullSync`/`Page` 解包时跳过该元素继续。
- **`Stroke` 只传 `rawPoints`**：线协议不携带插值点，接收端重建 `points` 与 `bounding`（插值可复现，因 `minDistance` 为固定默认值）。
- **`Page` 反序列化后统一 `EnableEraserInsert(true)`**：线协议不携带该标志；若保持默认关闭，远程同步得到的页面在 `Page::Eraser(rc, sid)` 会话插值路径下会静默失效。
- **`FullSync` 的 `Page`**：`UnpackElement` 先重建为多态元素，再 `dynamic_pointer_cast<Page>` 收集；非 Page 元素被跳过。
- **结构校验**：msgpack 结构非法（非 array、元素个数不符、字段类型不符）抛 `msgpack::type_error`，由调用方捕获。
