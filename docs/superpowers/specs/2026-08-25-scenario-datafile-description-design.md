# 设计文档：想定与数据文件描述功能

- 日期：2026-08-25
- 状态：已批准（用户确认方案后归档）
- 项目：DataReplay（数据回放客户端软件，Qt 5.12 + NATS）

## 一、背景与目标

当前想定（XML）与数据文件（回放数据/*.json）缺少用户可维护的描述信息，无法在界面中快速了解"这个想定/文件是什么、记录了什么"。

目标：为想定和数据文件增加**可编辑并持久化保存**的描述功能，通过**树节点悬停提示**展示、**右键菜单"编辑描述"**修改。

关键约束：**不修改想定 XML 文件内容，不修改数据文件内容**（数据文件为回放数据源，内嵌描述会破坏数据格式）。

## 二、需求决策（用户确认）

| 决策点 | 结论 |
|---|---|
| 描述可编辑性 | 可编辑并保存（想定 + 数据文件均可） |
| 存储位置 | 旁路配置文件 `description.json`（与现有 `mapping.json` 模式一致） |
| UI 展示 | 树节点悬停 tooltip 显示描述 |
| UI 编辑 | 树节点右键菜单「编辑描述…」弹出对话框 |

## 三、存储设计

### 3.1 文件位置

`dataFiles/<想定目录>/description.json`（与 `mapping.json` 同级）

### 3.2 文件结构

```json
{
    "想定": {
        "描述": "海上编队巡逻想定 - 联合巡逻任务说明"
    },
    "数据文件": {
        "data1.json": "阶段一：编队集结",
        "data2.json": "阶段二：对海突击"
    }
}
```

### 3.3 设计要点

- 顶层两个键：`想定.描述`（字符串）、`数据文件`（文件名 → 描述的对象映射）。
- 数据文件键为**文件名**（`回放数据/` 目录内文件名），查找 O(1)，与文件系统天然对应。
- 文件不存在 → 视为全部无描述；首次保存时自动创建。
- JSON 解析失败 → 视为空描述并输出告警日志；保存时重建文件。
- 采用对象映射而非数组：结构更简洁、查找更快，与 `mapping.json` 的对象风格一致。
- 用 `QJsonDocument` 序列化（Indented），自动转义特殊字符/换行。

## 四、Server 层改动（ScenarioMgr）

### 4.1 数据结构扩展（`ScenarioMgr.h`）

```cpp
struct Scenario {
    ...
    QString description;      //!< 想定描述（来自 description.json）
};

struct ScenarioSummary {
    ...
    QString description;      //!< 想定描述（树节点 tooltip 用，scanScenarios 时一并读取）
};
```

### 4.2 ScenarioMgr 新增接口（`ScenarioMgr.h/cpp`）

```cpp
/** @brief 读取想定描述 */
QString loadScenarioDescription(const QString &scenarioDir);

/** @brief 读取该想定全部数据文件描述（文件名 → 描述） */
QMap<QString, QString> loadDataFileDescriptions(const QString &scenarioDir);

/** @brief 增量合并保存描述（想定描述 + 数据文件描述表），写入 description.json */
bool saveDescription(const QString &scenarioDir, const QString &scenarioDesc,
                     const QMap<QString, QString> &dataFileDescs);
```

实现要点：
- 编辑单个数据文件描述时，UI 先调 `loadDataFileDescriptions` 取全量，修改目标条目后整体传给 `saveDescription`（增量合并）。
- `saveDescription` 采用**增量合并**（先读现有文件 → 合并新值 → 写回），与 `saveEntityIdMapping` 同风格；`scenarioDesc` 为空则删除想定描述键，`dataFileDescs` 中值为空的条目删除对应键。
- 内部私有方法 `QJsonObject loadDescriptionFile(const QString &scenarioDir)` / `bool writeDescriptionFile(...)` 拆分读写逻辑。

### 4.3 Facade 转发（`Server_DataReplay.h/cpp`）

增加与上述 4 个接口同签名的转发方法，保持"APP 只依赖 Server_DataReplay.h"的架构约定。

### 4.4 与现有代码衔接

- `parseScenarioXml()` **保持不变**：描述以 `description.json` 为准，XML 的 `Description` 属性不参与解析。
- **首次导入**（`importScenario`）：若源 XML 含 `Description` 属性，解析后作为想定描述的初始值写入目标 `description.json`；之后编辑不触碰 XML。
- `scanScenarios()` 构建 `ScenarioSummary` 时顺带读取该想定目录的 `description.json`（文件极小，性能可忽略）。

## 五、APP 层改动（DataReplayWidget）

### 5.1 展示（tooltip）

- `refreshScenarioTree()` 构建树节点时，调用 `m_server->loadDataFileDescriptions(dir)` 获取全部描述，并按文件名/想定填充。
- 想定目录节点 tooltip：现有内容 + `\n描述: <想定描述>`（无描述时省略描述行）。
- 数据文件节点 tooltip：现有内容 + `\n描述: <文件描述>`。

### 5.2 编辑（右键菜单）

- 想定目录节点右键菜单新增「编辑描述…」。
- 数据文件节点右键菜单新增「编辑描述…」。
- 想定描述编辑流程：`loadScenarioDescription(dir)` 取当前值填入 `DescriptionDialog` → 确定后 `saveDescription(dir, 新值, 现有文件描述表)`。
- 数据文件描述编辑流程：`loadDataFileDescriptions(dir)` 取全量 → 以该文件名为 key 取出当前值填入 `DescriptionDialog` → 确定后更新该 key 并整体 `saveDescription(...)`。
- 保存成功后 `refreshScenarioTree()` 刷新 tooltip。

### 5.3 新增 UI 部件

- `DescriptionDialog`（`DescriptionDialog.h/cpp`，仿现有 `ImportDialog` 风格）：
  - 标题：「编辑描述」
  - 控件：`QLabel`（对象名称）+ 多行 `QPlainTextEdit` + `QDialogButtonBox`（确定/取消）
  - 数据：构造时传入 `title`（想定名/文件名）与初始描述；`description()` 返回编辑结果。

## 六、数据流

```
启动/刷新树 → loadDataFileDescriptions(dir) → 读 description.json
           → 想定节点/数据文件节点 tooltip 附加描述

右键「编辑描述」→ DescriptionDialog 输入
           → saveDescription(dir, scenarioDesc, dataFileDescs) → 增量合并写回 description.json
           → refreshScenarioTree() 更新 tooltip
```

## 七、错误处理

| 场景 | 处理 |
|---|---|
| `description.json` 不存在 | 视为空描述；首次保存时创建 |
| JSON 解析失败 | 告警日志（qWarning）+ 按空描述处理；保存时重建文件 |
| 想定/数据文件被重命名或删除 | 描述键基于文件名，旧键自然失效；本期不做键迁移（见"非目标"） |
| 描述含特殊字符/换行 | `QJsonDocument` 自动转义；tooltip 用 `setToolTip` 安全显示 |

## 八、测试

1. 加载想定：悬停想定节点与数据文件节点，tooltip 正确显示描述。
2. 编辑保存：右键编辑想定描述与数据文件描述 → 确定 → tooltip 更新 → 重启程序描述仍在。
3. 边界：无 description.json、描述为空、长文本/中文/换行/引号转义。
4. 增量合并：只改数据文件描述不丢想定描述（反之亦然）；清空描述后键被删除。
5. 回归：现有实体映射、回放、导入、重命名/删除功能不受影响（描述为纯元数据，不进入回放数据流）。

## 九、非目标（YAGNI）

- 不在想定 XML 中回写描述（避免破坏现有 XML 格式与兼容性）。
- 不做描述键随文件重命名/删除的迁移（文件名变更后旧描述自然失效，可后续按需补充）。
- 不做富文本/多语言/描述搜索等高阶能力。
- 不在数据文件内嵌描述。

## 十、涉及文件清单

| 文件 | 变更 | 说明 |
|---|---|---|
| `src/Server_DataReplay/ScenarioMgr.h` | 修改 | `Scenario`/`ScenarioSummary` 加 `description`；新增 4 个接口声明 |
| `src/Server_DataReplay/ScenarioMgr.cpp` | 修改 | 实现描述读写（load/save）与增量合并；`scanScenarios` 填充 description |
| `src/Server_DataReplay/ScenarioMgr.cpp`（导入） | 修改 | `importScenario` 写入 XML Description 作为初始想定描述 |
| `src/Server_DataReplay/Server_DataReplay.h` | 修改 | 新增 4 个 Facade 转发声明 |
| `src/Server_DataReplay/Server_DataReplay.cpp` | 修改 | Facade 转发实现 |
| `src/APP_DataReplay/DataReplayWidget.cpp` | 修改 | 树节点 tooltip 附加描述；右键菜单两项 + 槽函数 |
| `src/APP_DataReplay/DataReplayWidget.h` | 修改 | 新增 `onEditScenarioDescription` / `onEditDataFileDescription` 槽 |
| `src/APP_DataReplay/DescriptionDialog.h` | 新增 | 描述编辑对话框声明 |
| `src/APP_DataReplay/DescriptionDialog.cpp` | 新增 | 描述编辑对话框实现 |
| `src/APP_DataReplay/APP_DataReplay.pro` | 修改 | 注册 `DescriptionDialog.cpp/h` |
