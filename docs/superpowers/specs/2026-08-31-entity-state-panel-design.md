# 设计文档：实体信息查看与实时更新

- 日期：2026-08-31
- 状态：已批准（用户确认方案后归档）
- 项目：DataReplay（数据回放客户端软件，Qt 5.12 + NATS）

## 一、背景与目标

当前程序只能查看想定实体的 ID/名称（用于映射配置），无法查看实体属性，也无法在回放过程中观察实体位置变化。

目标：
1. 查看当前初始化想定中**所有实体**的实体ID、名称、X/Y/Z（初始值从想定 XML 读取）。
2. 支持按**实体ID或名称搜索**，命中时面板**只显示/更新该实体**；清空搜索后恢复显示/更新全部。
3. 回放过程中根据数据**实时更新** X/Y/Z；**不修改想定 XML 原内容**。
4. 倍速（最高 100x）、实体最多数千时**界面不卡顿**。

## 二、需求决策（用户确认）

| 决策点 | 结论 |
|---|---|
| 展示形式 | 新增独立**实体状态面板**（可控制打开/关闭） |
| 单实体查看 | 面板自带**搜索框**（实体ID 或 名称模糊过滤），命中只显示/更新该实体，清空恢复全部 |
| 初始数据来源 | 想定 XML 中 Entity 的 Attribute X/Y/Z（只读解析） |
| 实时数据来源 | 回放数据每行的 `x/y/z` 字段 |
| 架构方案 | 方案 A：工作线程解析 + 增量信号 + 主线程节流刷新 |
| 规模 | 实体最多可能数千 → 严格性能设计 |

## 三、总体架构（方案 A）

```
想定 XML ──只读解析──► EntityInfo(含x/y/z) ──► 实体状态面板初始显示
                                                    ▲
回放数据 ──ReplayWorker(工作线程)──► 每窗口"变化实体列表"信号
                                          │ QueuedConnection(值拷贝)
                                          ▼
                              ReplayEngine(转发) → Facade(转发)
                                          ▼
                              APP 主线程: 更新内存状态表(QHash) + 脏标记
                                          │
                                          ▼
                              QTimer 节流(100~200ms) → 表格增量刷新
```

关键点：
- **解析在工作线程**（ReplayWorker），UI 线程只做轻量状态更新与节流刷新。
- **增量信号**：每窗口只发送位置发生变化的实体（去重），非全量。
- **UI 节流**：刷新频率固定，与回放 tick 频率（步长/倍速）解耦。

## 四、Server 层改动

### 4.1 `EntityInfo` 扩展（`ScenarioMgr.h`）

```cpp
struct EntityInfo {
    QString id;
    QString name;
    double x = 0.0, y = 0.0, z = 0.0;   //!< 初始位置（来自想定 XML 的 Attribute）
};
```

### 4.2 `parseScenarioXml` 解析实体 Attribute

- 在现有 Entity ID/Name 解析基础上，读取 Entity 子元素 `<Attribute X="..." Y="..." Z="..."/>` 到 `EntityInfo.x/y/z`。
- **只读解析，不修改 XML**。

### 4.3 `ReplayWorker` 实时位置解析（性能核心）

新增运行时结构（与想定初始信息分离）：

```cpp
struct EntityState {          //!< 运行时实体状态
    QString id;
    double x = 0.0, y = 0.0, z = 0.0;
};
```

- ReplayWorker 内部维护 `QHash<QString, EntityState> m_entityStates`。
- `processWindow()` 读取窗口数据后、**实体ID替换之前**，解析每条 `jsonPayload` 的 `entity` / `x` / `y` / `z`：
  - 更新状态表；
  - 若位置发生变化，将该实体收集进"变化列表"（按 id 去重）。
- 窗口处理结束：`emit entityStatesUpdated(const QList<EntityState> &changed);`（每窗口一次）。
- 说明：解析在替换前进行，用**原始实体 ID** 与想定实体匹配，不受 mapping.json 映射影响。

### 4.4 信号转发链路

```
ReplayWorker::entityStatesUpdated ──(Queued)──► ReplayEngine::entityStatesUpdated
      ──► Server_DataReplay::entityStatesUpdated ──► DataReplayWidget::onEntityStatesUpdated
```

- `EntityState` 需 `Q_DECLARE_METATYPE(EntityState)` + `qRegisterMetaType`（跨线程 QueuedConnection 传参）。
- `ReplayEngine`/`Server_DataReplay` 仅做信号-信号转发（无 lambda）。

## 五、APP 层改动

### 5.1 实体状态面板（可开关 + 自带搜索）

- 界面新增**实体状态面板**（`QTableView` + `QStandardItemModel`，只读）：
  - 列：实体ID | 名称 | X | Y | Z
  - 面板顶部 `QLineEdit` 搜索框（按实体ID 或 名称模糊匹配）
  - 过滤通过**新增**的 `EntityStateFilterProxyModel`（仿现有 `EntityFilterProxyModel`，按 ID/名称过滤）
- 控制区新增 **"实体状态"按钮**，点击切换面板显示/隐藏（满足"可控制打开或关闭"）。
- 与现有"实体配置"（映射编辑）并存，互不影响。

### 5.2 实时更新（节流）

- 主线程槽 `onEntityStatesUpdated(const QList<EntityState> &changed)`：
  - **只更新内存状态表** `QHash<QString, EntityState> m_entityStates`；
  - 记录脏标记（或待更新 id 列表），**不直接触碰控件**。
- `QTimer`（100~200ms）`m_uiRefreshTimer` 触发 UI 刷新：
  - 遍历待更新实体，按 `QHash<QString, int> m_entityRowMap`（id→行号）定位行，
    只对 X/Y/Z 列执行 `setItem`（增量更新源 model，代理/视图自动反映）。
- 搜索过滤时：源 model 全量存在，过滤后视图只显示匹配行，增量更新同样只改变化的行——天然满足"命中时只更新该实体"。

### 5.3 初始化填充与重置

- **初始化时**（`onInit()` 成功，状态进入 Ready）：用 `scenario->entities`（含 x/y/z）填充实体状态表初始值，并建立 `id→行号` 映射；同时清空 worker 的运行时状态表（`ReplayWorker::initialize` 时重置）。
- **停止/回放完成**（`onStop()` / `replayFinished`）：将实体状态表**恢复为想定初始值**（重新从当前想定填充），**不修改 XML**，等待下次初始化/回放。

### 5.4 搜索交互

- 搜索框 `returnPressed` / 搜索按钮触发：设置代理过滤（实体ID 或 名称包含关键词，大小写不敏感）。
- 关键词清空：清除过滤，恢复显示全部。

## 六、性能设计（需求 4，针对数千实体）

| 手段 | 说明 |
|---|---|
| 解析在工作线程 | 实体位置解析不占 UI 线程 |
| 增量信号 | 每窗口只发**变化的实体**（去重），非全量 |
| 主线程轻量更新 | 信号到达只更新 QHash（O(1)），不触碰控件 |
| UI 节流刷新 | QTimer 100~200ms 聚合批量刷新，频率固定 |
| 行定位 O(1) | `QHash<id, row>` 映射，数千行不遍历 |
| 倍速自适应 | UI 刷新频率与 tick 频率解耦，倍速再高不卡 |

## 七、不修改想定文件

- 想定 XML 仅**只读解析**（`parseScenarioXml` 只读）。
- 实时实体状态全部在内存（worker 状态表 + 主线程状态表），**绝不写回 XML**。

## 八、错误与边界

| 场景 | 处理 |
|---|---|
| 数据行缺 `x/y/z` 或 `entity` 字段 | 跳过该字段，保留原值 |
| 数据中出现想定外的实体 ID | 忽略（不新增行） |
| 实体ID映射（mapping.json） | 不影响状态解析（解析在替换前，用原始 ID） |
| 停止/重新初始化 | 状态表重置为想定初始值（内存），XML 不动 |
| 搜索无结果 | 面板为空（过滤后 0 行），清空搜索恢复 |

## 九、测试

1. 加载想定：实体状态面板显示全部实体的初始 X/Y/Z（与 XML 一致）。
2. 搜索：输入实体ID/名称命中一条 → 面板只显示该实体且实时更新它；清空 → 恢复全部。
3. 回放实时性：回放时 X/Y/Z 随数据变化；倍速 100x、数千实体下界面不卡顿。
4. 面板开关：按钮显示/隐藏正常。
5. 停止/重初始化：状态重置为初始值。
6. 回归：现有映射配置、描述、回放功能不受影响。

## 十、涉及文件清单

| 文件 | 变更 | 说明 |
|---|---|---|
| `src/Server_DataReplay/ScenarioMgr.h` | 修改 | `EntityInfo` 加 x/y/z |
| `src/Server_DataReplay/ScenarioMgr.cpp` | 修改 | `parseScenarioXml` 解析 Attribute X/Y/Z（只读） |
| `src/Server_DataReplay/ReplayWorker.h` | 修改 | 新增 `EntityState` 结构 + `entityStatesUpdated` 信号 + 内部状态表 |
| `src/Server_DataReplay/ReplayWorker.cpp` | 修改 | `processWindow` 解析实体位置、维护状态表、发射变化列表 |
| `src/Server_DataReplay/ReplayEngine.h/cpp` | 修改 | 转发 `entityStatesUpdated` 信号 |
| `src/Server_DataReplay/Server_DataReplay.h/cpp` | 修改 | Facade 转发 `entityStatesUpdated` 信号 |
| `src/APP_DataReplay/DataReplayWidget.h/cpp` | 修改 | 实体状态面板、搜索、节流刷新定时器、`onEntityStatesUpdated` 槽 |
| `src/APP_DataReplay/DataReplayWidget.ui` | 修改 | 新增实体状态面板、搜索框、"实体状态"开关按钮 |
| `src/APP_DataReplay/EntityStateFilterProxyModel.h/cpp` | 新增 | 实体状态表过滤代理（仿 `EntityFilterProxyModel`） |
| `src/APP_DataReplay/APP_DataReplay.pro` | 修改 | 注册新文件 |
