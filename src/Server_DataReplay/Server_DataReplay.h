/**
 * @file Server_DataReplay.h
 * @brief 服务端动态库统一入口（Facade 模式）
 *
 * Server_DataReplay 作为 DLL 的唯一导出入口，封装所有后端模块
 *（ScenarioMgr、ReplayEngine、Communication_NATS、LogService），
 * APP 层只与 Server_DataReplay 交互，不直接接触内部模块。
 *
 * 职责：
 *   - 统一初始化：日志 → NATS → 各模块按序启动
 *   - 想定管理：委托 ScenarioMgr
 *   - 回放控制：委托 ReplayEngine
 *   - 信号转发：将内部模块信号统一暴露给 APP 层
 */

#ifndef SERVER_DATAREPLAY_H
#define SERVER_DATAREPLAY_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QList>
#include "Server_DataReplay_global.h"
#include "publicDefineAndStruct.h"    // EntityState
#include "ScenarioMgr.h"    // for Scenario, EntityInfo, ScenarioSummary 结构体（API 层需要完整定义）

// 前置声明内部模块（APP 层不感知）
class ReplayEngine;

/**
 * @brief 服务端动态库 Facade 入口类
 *
 * APP 层仅需创建 Server_DataReplay 实例，所有后端能力通过此类暴露。
 * 内部自动管理 ScenarioMgr、ReplayEngine、NATS 的生命周期和交互顺序。
 */
class SERVER_DATAREPLAY_EXPORT Server_DataReplay : public QObject
{
    Q_OBJECT

public:
    /** @brief 回放引擎状态（对外暴露，屏蔽内部 ReplayEngine::State） */
    enum EngineState {
        Idle,
        Ready,
        Playing,
        Paused,
        Stopped
    };
    Q_ENUM(EngineState)

    explicit Server_DataReplay(QObject *parent = nullptr);
    ~Server_DataReplay();

    // ==================== 初始化 ====================

    /**
     * @brief 统一初始化后端服务
     * @param logDir 日志文件目录路径
     *
     * 按序执行：设置日志目录 → 启动 NATS 异步连接。
     * 其他模块（ScenarioMgr、ReplayEngine）在构造时已就绪，无需额外初始化。
     */
    void initialize(const QString &logDir);

    // ==================== 日志 ====================

    /** @brief 记录日志（内部委托 LogService） */
    void log(const QString &level, const QString &message);

    // ==================== 想定管理（委托 ScenarioMgr） ====================

    /** @brief 扫描 dataFiles/ 目录，返回所有想定摘要列表 */
    QList<ScenarioSummary> scanScenarios() const;

    /** @brief 加载指定想定（解析 XML + 关联数据文件） */
    bool loadScenario(const QString &filePath);

    /** @brief 获取当前想定完整信息 */
    const Scenario *currentScenario() const;

    /** @brief 从想定目录加载实体ID映射表 */
    QMap<QString, QString> loadEntityIdMapping(const QString &scenarioDir);

    /** @brief 增量保存实体ID映射到想定目录下的 mapping.json */
    bool saveEntityIdMapping(const QString &scenarioDir, const QMap<QString, QString> &newMappings);

    /** @brief 读取想定描述（来自想定目录下的 description.json） */
    QString loadScenarioDescription(const QString &scenarioDir);

    /** @brief 读取该想定全部数据文件描述（文件名 → 描述） */
    QMap<QString, QString> loadDataFileDescriptions(const QString &scenarioDir);

    /** @brief 增量合并保存描述到想定目录下的 description.json（仅读写旁路配置，不改 XML/数据文件） */
    bool saveDescription(const QString &scenarioDir, const QString &scenarioDesc,
                         const QMap<QString, QString> &dataFileDescs);

    /** @brief 重命名想定文件夹及其 XML 文件 */
    bool renameScenario(const QString &oldDirPath, const QString &newName);

    /** @brief 重命名数据文件 */
    bool renameDataFile(const QString &oldFilePath, const QString &newName);

    /** @brief 删除整个想定目录（含所有数据文件和配置） */
    bool deleteScenario(const QString &scenarioDirPath);

    /** @brief 删除单个数据文件 */
    bool deleteDataFile(const QString &filePath);

    /** @brief 向已有想定追加数据文件（复制到回放数据/ 目录），返回成功复制的数量 */
    int addDataFiles(const QString &scenarioDir, const QStringList &filePaths);

    /** @brief 重新扫描想定的数据文件目录，返回当前数据文件路径列表 */
    QStringList refreshDataFiles(const QString &scenarioDir);

    /** @brief 预览导入：解析 XML + 扫描源目录，不执行实际复制 */
    ImportPreview previewImport(const QString &xmlSourcePath);

    /** @brief 执行导入：创建目录 + 复制 XML + 创建空的 回放数据/ */
    bool importScenario(const QString &xmlSourcePath,
                        const QString &targetDirName);

    // ==================== 回放控制（委托 ReplayEngine） ====================

    /** @brief 初始化回放引擎（创建 Reader、扫描时间范围、连接 NATS） */
    bool initReplay(const Scenario *scenario, const QString &selectedFile);

    /** @brief 开始回放 */
    bool startReplay();

    /** @brief 暂停回放 */
    bool pauseReplay();

    /** @brief 继续回放 */
    bool resumeReplay();

    /** @brief 停止回放 */
    bool stopReplay();

    /** @brief 设置回放倍速（1~100） */
    void setSpeed(int speed);

    /** @brief 设置实体ID映射表 */
    void setEntityIdMapping(const QMap<QString, QString> &mapping);

    /** @brief 获取当前倍速 */
    int speed() const;

    /** @brief 获取当前引擎状态 */
    EngineState state() const;

    /** @brief 获取整体回放进度（0.0 ~ 100.0） */
    double overallProgress() const;

    /** @brief 当前跟踪/显示的实体属性名列表（转发自 ReplayEngine） */
    QStringList trackedAttributes() const;

signals:
    /** @brief 引擎状态变更 */
    void stateChanged(Server_DataReplay::EngineState state);

    /** @brief 仿真时间更新 */
    void simTimeChanged(const QDateTime &time);

    /** @brief 回放进度更新（百分比） */
    void progressChanged(double percent);

    /** @brief 回放完成 */
    void replayFinished();

    /** @brief 错误发生 */
    void errorOccurred(const QString &error);

    /** @brief NATS 连接状态变化 */
    void natsConnected(bool connected);

    /** @brief 日志消息（转发自内部 LogService，APP 连接此信号即可展示日志） */
    void newLog(const QString &formattedMessage);

    /** @brief NATS 消息到达（转发自 Communication_NATS，APP 通过此信号接收外部指令） */
    void natsMessageReceived(const QString &topic, const QByteArray &data);

    /** @brief 实体状态更新（转发自 ReplayEngine，APP 通过此信号刷新实体状态面板） */
    void entityStatesUpdated(const QList<EntityState> &changed);

private:
    ScenarioMgr  *m_scenarioMgr  = nullptr;
    ReplayEngine *m_replayEngine = nullptr;
};

#endif // SERVER_DATAREPLAY_H
