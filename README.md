# DataReplay — 数据回放客户端软件

## 项目概述

DataReplay 是一个基于 **Qt 5.12** 和 **NATS 消息中间件** 的数据回放客户端软件，采用**前后端分离架构**。它将预先录制的推演数据文件按照仿真步长时间窗口逐条读取，通过 NATS 主题重新发送，实现仿真推演过程的回放。

### 应用场景

- 仿真推演系统的数据回溯与复现
- 试验数据的离线回放分析
- 多路数据源的同步回放与重发

---

## 功能特性

### 想定管理 (ScenarioMgr)

| 功能 | 说明 |
|------|------|
| 自动扫描 | 扫描 `dataFiles/` 目录下所有想定文件夹 |
| XML 解析 | 解析想定 XML 文件（名称、描述、仿真步长、时间范围、仿真实体列表） |
| 文件关联 | 约定 `想定目录/回放数据/*.txt` 自动关联为数据文件 |
| UI 展示 | 树形列表展示想定和数据文件，表格展示仿真实体 |

### 数据文件读取 (DataFileReader)

| 功能 | 说明 |
|------|------|
| 游标式读取 | **不加载全文到内存**，维护文件游标按需读取 |
| 超大文件支持 | 支持几百 MB 的数据文件 |
| 仿真时间窗口 | 按 simTime（仿真时间）窗口切片，一个步长窗口内多条数据 |
| 智能预扫描 | 快速扫描文件首尾行获取时间范围和估算记录数 |

### 回放引擎 (ReplayEngine)

| 功能 | 说明 |
|------|------|
| 状态机 | Idle → Ready → Playing ↔ Paused ↔ Stopped |
| 定时器步进 | 物理定时器间隔 = 仿真步长 / 倍速 |
| 倍速控制 | 1~100 倍，回放中实时生效 |
| 跨文件合并 | 多数据文件按 simTime 全局排序后发送 |
| 进度计算 | 基于数据文件实际时间范围，非想定理论结束时间 |

### NATS 通信 (Communication_NATS)

| 功能 | 说明 |
|------|------|
| 消息发布 | 通过 NATS 主题 `DataReplay` 发送回放数据 |
| XML 配置 | 服务器地址、订阅主题在 `config/NATS/NatsConfig.xml` 配置 |
| 断线重连 | 指数退避策略自动重连 |
| 单例模式 | 全局唯一 NATS 通信实例 |

### 日志服务 (LogService)

| 功能 | 说明 |
|------|------|
| 双通道输出 | Qt 信号（前端显示）+ 文件写入 |
| 自动分割 | 按日期自动创建日志文件 |
| 日志格式 | `[yyyy-MM-dd HH:mm:ss] [LEVEL] 消息内容` |
| 级别支持 | INFO, WARN, ERROR, DEBUG |

### UI 界面

| 区域 | 说明 |
|------|------|
| 文件管理 | QTreeView 树形展示想定和数据文件，支持选中单个文件回放 |
| 实体配置 | QTableView 表格展示仿真实体（ID/名称/类型/状态） |
| 导调控制 | 初始化/开始/暂停/继续/停止 五按钮，倍速输入(1-100) |
| 进度显示 | 当前仿真时间 + QProgressBar 进度条 |
| 日志信息 | QTextEdit 只读日志显示区 |
| 状态栏 | NATS 连接状态 / 当前想定 / 进度 / 引擎状态 |

---

## 架构设计

### 前后端分离

```
┌──────────────────────────────────────────────────────────────┐
│                   APP_DataReplay (exe)                        │
│                     前端 — UI 层 + 控制编排                      │
│                                                                │
│  职责: 界面展示、用户交互、调用后端接口                             │
│  依赖: 只链接 Server_DataReplay.dll，不接触 NATS                 │
└──────────────────────────┬───────────────────────────────────┘
                           │ 函数调用 + Qt 信号/槽
                           ▼
┌──────────────────────────────────────────────────────────────┐
│                  Server_DataReplay (dll)                       │
│                     后端 — 核心业务引擎                          │
│                                                                │
│  职责: 所有业务逻辑 + 数据处理 + NATS 通信                        │
│  依赖: libnats.dll (NATS 细节完全封装在内部)                      │
└──────────────────────────────────────────────────────────────┘
```

### 模块依赖关系

```
APP_DataReplay (exe)
  └── 链接 Server_DataReplay.dll

Server_DataReplay (dll)
  ├── 业务逻辑层
  │   ├── ScenarioMgr          ← 想定 XML 解析、目录扫描
  │   ├── ReplayEngine         ← 回放状态机、步进、倍速
  │   │   ├── DataFileReader   ← 大文件游标式读取（多实例）
  │   │   └── Communication_NATS ← NATS 消息发送
  │   └── LogService           ← 日志收集(信号 + 文件)
  └── 第三方依赖
      └── 链接 libnats.dll
```

---

## 类关系与调用流程

### 核心类图

```
┌──────────────────┐
│  DataReplayWidget │  (APP 层 - 主界面)
│  (信号/槽驱动)    │
└────┬──────┬──────┘
     │      │
     │ 使用 │ 使用
     ▼      ▼
┌──────────┐ ┌──────────────┐
│ScenarioMgr│ │ReplayEngine  │  (Server 层)
│ 想定管理  │ │ 回放引擎      │
└────┬─────┘ └──┬──┬──┬─────┘
     │          │  │  │
     │          │  │  └──► Communication_NATS (NATS 通信单例)
     │          │  │         └──► NATSClient (C 库封装)
     │          │  └────► DataFileReader × N (游标式文件读取)
     │          └───────► QTimer (步进定时器)
     │
     └──────► LogService (单例 - 日志服务)
```

### 启动流程

```
[程序启动]
  APP → ScenarioMgr::scanScenarios()
   ← 扫描 dataFiles/ 目录 → 返回想定摘要列表
  APP → 显示在左侧树形列表中

[选择想定 → 点击加载]
  APP → ScenarioMgr::loadScenario(xmlPath)
   ← 解析 XML → 关联数据文件 → 返回完整想定
  APP → 展开树形列表 + 填充实体表格

[选中数据文件 → 初始化]
  APP → ReplayEngine::initialize(scenario, {selectedFile})
         ├── 创建 DataFileReader（只打开选中的数据文件）
         ├── 扫描文件最晚 simTime → m_dataEndTime
         ├── 初始化 NATS 连接
         └── 状态 → Ready

[开始回放]
  APP → ReplayEngine::start()
         └── QTimer 启动（间隔 = simStepMs / speed）
              └── 每次 tick():
                   ├── DataFileReader::readWindow() 读取窗口数据
                   ├── 跨文件合并 → 按 simTime 排序
                   ├── Communication_NATS::sendMsgData() → NATS 发送
                   ├── 推进仿真时间
                   └── 检查结束条件（allEnd / m_dataEndTime）
```

### 回放状态机

```
         Idle ──initialize()──► Ready ──start()──► Playing
          ▲                      ▲                   │
          │                      │              pause()
          │                      │                   │
          │                  stop()                  ▼
          │                      │               Paused
          │                      │                   │
          └──────stop()──────────┘             resume()
                                                   │
                                                   ▼
                                               Playing
```

### 结束条件（三层保障）

```
tick() →
  ① 前置判断: windowStart > m_dataEndTime? → 停止
  ② 读取窗口数据 → 发送 →
  ③ allEnd? (Reader 全部到文件尾) → 停在 windowStart，停止
  ④ windowStart >= m_dataEndTime? → 停在 m_dataEndTime，停止
  ⑤ 正常推进 simTime → 继续
```

### 关键数据结构

```cpp
// 仿真实体
struct EntityInfo {
    QString id, name, type;
    double x, y, z, speed, heading;
    QString status;
};

// 想定
struct Scenario {
    QString name, description, filePath;
    QDateTime startTime, endTime;
    int simStepMs;           // 仿真步长(ms)
    QStringList dataFiles;   // 关联数据文件
    QList<EntityInfo> entities;
};

// 数据记录
struct DataRecord {
    QDateTime simTimestamp;  // 仿真时间
    QString fullLine;        // 原始行
    QString jsonPayload;     // JSON 部分（NATS 发送）
};

// 数据文件信息
struct DataFileInfo {
    QString filePath;
    qint64 fileSize;
    quint64 recordCount;
    QDateTime minTime, maxTime;
};
```

---

## 项目结构

```
D:/QTProject/DataReplay/
├── bin/                              # 可执行文件和 DLL
│   ├── APP_DataReplay.exe
│   └── libnats.dll
├── config/
│   └── NATS/
│       └── NatsConfig.xml            # NATS 连接配置
├── dataFiles/                        # 想定数据目录
│   └── 想定-XXX/
│       ├── 想定.xml                  # 想定描述文件
│       └── 回放数据/
│           └── *_data*.txt           # 回放数据文件
├── include/                          # 公共头文件
│   ├── publicDefineAndStruct.h
│   ├── Service_Communication_Factory.h
│   └── NATS/                        # NATS C 库头文件
├── src/
│   ├── DataReplay.pro                # 根项目文件 (subdirs)
│   ├── APP_DataReplay/               # 前端应用
│   │   ├── APP_DataReplay.pro
│   │   ├── main.cpp
│   │   ├── DataReplayWidget.h/cpp/ui
│   └── Server_DataReplay/            # 后端 DLL
│       ├── Server_DataReplay.pro
│       ├── Server_DataReplay.h/cpp
│       ├── ScenarioMgr.h/cpp         # 想定管理
│       ├── DataFileReader.h/cpp      # 文件读取
│       ├── ReplayEngine.h/cpp        # 回放引擎
│       ├── LogService.h/cpp          # 日志服务
│       └── Communication/            # NATS 通信
│           ├── NATSClient.h/cpp
│           ├── Communication_NATS.h/cpp
│           └── Communication_Interior.h/cpp
├── .gitignore
└── README.md
```

---

## 构建与运行

### 环境要求

- **Qt 5.12.6** (MinGW 64-bit)
- **NATS Server**（运行回放时需要启动）
- **MinGW 7.3+**

### 构建步骤

1. 打开 Qt Creator，选择 `src/DataReplay.pro`
2. **Build → Run qmake**（刷新项目配置）
3. 按 `Ctrl+B` 构建（subdirs 会自动先编译 Server 库，再编译 APP）
4. 设置工作目录为 `D:/QTProject/DataReplay/bin/`

### 运行准备

1. 启动 NATS Server（默认 `127.0.0.1:4222`）
2. 确保 `dataFiles/` 目录下有想定文件夹（含 XML 和 `回放数据/*.txt`）
3. 运行 `bin/APP_DataReplay.exe`

### 使用方法

1. 启动程序后，左侧树形列表自动扫描显示所有想定
2. 选中一个想定节点，点击 **加载想定**（或先展开查看数据文件）
3. 选中想定节点（回放所有文件）或**具体数据文件节点**（回放单个文件）
4. 点击 **初始化** → **开始**，开始回放
5. 可随时调整倍速、暂停/继续、停止

---

## 数据文件格式

### 想定 XML

```xml
<Scenario>
    <ScenarioInfo>
        <Name>想定名称</Name>
        <Description>描述</Description>
        <StartTime>2025-07-01 14:32:05</StartTime>
        <EndTime>2025-07-01 22:32:05</EndTime>
        <SimStep>100</SimStep>
    </ScenarioInfo>
    <Entities>
        <Entity ID="1001" Name="实体名" type="类型">
            <Attribute X="50000.0" Y="50000.0" Z="0.0" 
                       Speed="0.0" Heading="45.0" Status="待命"/>
        </Entity>
    </Entities>
</Scenario>
```

### 回放数据 TXT

每行格式：`外层时间戳 JSON数据`

```
2026-07-12 09:00:00.000 {"data":{"simTime":{"formatted":"2025-07-01 14:32:05.000","epochMillis":1751380325000},"entity":"1001","cmd":"status","speed":8,"x":50000,"y":50000}}
```

- **外层时间戳**：记录/录制时间（固定 23 字符 `YYYY-MM-DD HH:MM:SS.zzz`）
- **simTime.formatted**：仿真时间（回放引擎以此时序推进）
- **simTime.epochMillis**：仿真时间的时间戳（备选，优先使用 formatted）

---

## 配置文件

### NATS 配置 `config/NATS/NatsConfig.xml`

```xml
<Root>
    <AttrIP IP="127.0.0.1" />
    <AttrTOPIC TOPIC="DataReplay" />
</Root>
```

- `AttrIP`：NATS 服务器地址
- `AttrTOPIC`：`DataReplay` 为回放数据发送主题

---

## 未来扩展

- **网络接收推演数据**：利用 `Communication_NATS::messageReceived` 信号接收并存储外部数据
- **值替换**：在 ReplayEngine::tick() 中插入替换层，修改数据字段值后发送
- **日志增强**：日志级别筛选、搜索、按日期自动切割
