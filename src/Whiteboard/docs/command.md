# command — 命令体系

目录：`src/Whiteboard/command/`

白板网络命令的纯数据结构定义。命令不包含任何序列化逻辑，网络二进制转换由协议层完成（见 [protocol.md](protocol.md)）。

## Command 基类

```cpp
class Command {
public:
    virtual ~Command() = default;
    virtual std::string GetType() const = 0;  // 命令类型名（即线格式中的 type_str）
    std::string pageId;                        // 每条命令都作用于特定页面
};
```

- `GetType()` 与工厂注册表中的 key 一致，是序列化时写入线格式的类型标识。
- `pageId` 为公共字段，几乎所有命令都携带目标页面（`FullSync` 也随基类带 pageId，但实际以 payload 中的 pages 为准）。

### 工厂注册

```cpp
using CommandFactory = std::shared_ptr<Command> (*)();

bool RegisterCommandFactory(const std::string& type, CommandFactory factory); // 已注册返回 false
std::shared_ptr<Command> CreateCommand(const std::string& type);              // 未知类型返回 nullptr
```

内置 18 个命令全部注册于 [command.cpp](../command/command.cpp)。反序列化时协议层按 `type_str` 调 `CreateCommand` 创建实例。

## 命令清单

### 笔画（stroke_commands.h）

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `StrokeBegin` | `strokeId`、`width`、`color`、`points` | 笔画开始：携带样式与初始点集 |
| `StrokeMove` | `strokeId`、`points` | 笔画进行中：增量点集 |
| `StrokeEnd` | `strokeId`、`points` | 笔画结束：收尾点集 |

### 橡皮（eraser_commands.h）

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `EraserBegin` | `sessionId`、`points` | 擦除拖动开始；`sessionId` 关联一次拖动的 Begin/Move/End |
| `EraserMove` | `sessionId`、`points` | 擦除拖动中：本次矩形轨迹点 |
| `EraserEnd` | `sessionId`、`removedIds`、`addedElements` | 擦除结束：携带精确增量结果（同 [page.md](page.md) 的 `EraserResult`） |

协作语义（操作端为主）：远端收到 `EraserBegin`/`EraserMove` 时仅做 **UI 视觉擦除**（数据层不动）；`EraserEnd` 到达时按 `removedIds` + `addedElements` 同步数据层，保证两端最终一致。

### 元素（element_commands.h、transform_commands.h）

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `ElementAdd` | `element`（多态 `Element`） | 增量添加元素（如整条笔画） |
| `ElementRemove` | `elementId` | 按 id 删除元素 |
| `StrokeUpdate` | `strokeId`、`points` | 变换烘焙：移动/旋转/缩放结束后的新点集写回笔画（见 [page.md](page.md) `UpdateStroke`） |

### 页面（page_commands.h）

`pageId` 复用基类字段。

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `PageCreate` | `newPageId` | 新建页；接收端按此 id 创建空白页并切换 |
| `PageSelect` | —（pageId） | 切换当前页到 `cmd.pageId` |
| `PageDelete` | —（pageId） | 删除 `cmd.pageId` 指定页面 |
| `PageClear` | —（pageId） | 清空 `cmd.pageId` 指定页面 |

### 历史（undo_redo_commands.h）

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `Undo` | —（pageId） | 对 `cmd.pageId` 页面执行一次撤销 |
| `Redo` | —（pageId） | 对 `cmd.pageId` 页面执行一次重做 |

接收端维护与发送端一致的操作历史（保留最近 20 次），依赖 `Page::Clone` 快照（见 [page.md](page.md)）。

### 全量同步（full_sync_command.h）

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `FullSync` | `pages`（`std::vector<Page>`） | 全量页面状态同步；`Page` 按多态元素嵌套序列化 |

### 预览（preview_commands.h）

| 命令 | 字段 | 用途 |
| --- | --- | --- |
| `LassoPreview` | `sessionId`、`points` | 套索路径实时预览：操作端采样点集，远端以虚线渲染；**空 `points` 表示清除** |
| `SelectionPreview` | `sessionId`、`points`、`selectedIds` | 选择框预览：`points` 为选择框 4 顶点（按序连线，含旋转），`selectedIds` 为选中笔画 id（供远端高亮，可为空）；**空 `points` 表示清除** |

预览语义（操作端为主）：远端仅渲染虚线预览与高亮，**不执行任何数据层操作**；`sessionId` 用于区分操作来源。

## 新增命令扩展指引

1. 定义命令类（继承 `Command`，实现 `GetType()` 返回唯一类型名，字段为公开数据成员）。
2. 在 [command.cpp](../command/command.cpp) 的 `Factories()` 注册表中添加 `{ "TypeName", [] { return std::make_shared<TypeName>(); } }`。
3. 在 [command_codec.cpp](../protocol/command_codec.cpp) 的 `SerializeCommand` / `DeserializeCommand` 中补充分支（payload 布局见 [protocol.md](protocol.md)）。
