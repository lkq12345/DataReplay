/**
 * @file Server_DataReplay.cpp
 * @brief 服务端动态库 Facade 入口类的实现
 *
 * 所有公开方法均委托给内部模块（ScenarioMgr、ReplayEngine），
 * 内部信号通过 lambda 转换后以统一接口转发给 APP 层。
 */

#include "Server_DataReplay.h"
#include "ScenarioMgr.h"
#include "ReplayEngine.h"
#include "LogService.h"
#include "Communication/Communication_NATS.h"

// ==================== 构造 / 析构 ====================

Server_DataReplay::Server_DataReplay(QObject *parent)
    : QObject(parent)
{
    // 创建内部模块
    m_scenarioMgr  = new ScenarioMgr(this);
    m_replayEngine = new ReplayEngine(this);

    // ---- 转发 ReplayEngine 信号 → 对外信号 ----

    // 状态变更：内部枚举 → 对外枚举（值一一对应，static_cast 即可）
    connect(m_replayEngine, &ReplayEngine::stateChanged, this, [this](ReplayEngine::State s) {
        emit stateChanged(static_cast<EngineState>(s));
    });

    // 仿真时间：直通
    connect(m_replayEngine, &ReplayEngine::simTimeChanged,
            this, &Server_DataReplay::simTimeChanged);

    // 进度：直通
    connect(m_replayEngine, &ReplayEngine::progressChanged,
            this, &Server_DataReplay::progressChanged);

    // 回放完成：直通
    connect(m_replayEngine, &ReplayEngine::replayFinished,
            this, &Server_DataReplay::replayFinished);

    // 错误：直通
    connect(m_replayEngine, &ReplayEngine::errorOccurred,
            this, &Server_DataReplay::errorOccurred);

    // 引擎内部日志 → LogService
    connect(m_replayEngine, &ReplayEngine::logMessage, this, [this](const QString &level, const QString &msg) {
        LogService::instance().log(level, msg);
    });

    // ---- 日志信号转发（LogService → APP） ----
    // APP 层不再直接依赖 LogService，通过 Facade 的 newLog 信号获取日志
    connect(&LogService::instance(), &LogService::newLog,
            this, &Server_DataReplay::newLog);

    // ---- NATS 连接状态（缓存 + 断开自动暂停） ----
    connect(&Communication_NATS::getInstance(), &Communication_NATS::natsConnected,
            this, [this](bool connected) {
                if (!connected) {
                    // NATS 断开时自动暂停回放，避免数据丢失
                    if (m_replayEngine->state() == ReplayEngine::Playing) {
                        m_replayEngine->pause();
                        LogService::instance().log("WARN", "NATS 连接断开，回放已自动暂停");
                    }
                }
                emit natsConnected(connected);
            });
}

Server_DataReplay::~Server_DataReplay()
{
    // 确保停止回放
    if (m_replayEngine) {
        m_replayEngine->stop();
    }
}

// ==================== 初始化 ====================

void Server_DataReplay::initialize(const QString &logDir)
{
    // ① 设置日志目录
    LogService::instance().setLogFile(logDir);
    LogService::instance().log("INFO", "数据回放软件启动");

    // ② 异步发起 NATS 连接（内部含指数退避重连机制，不阻塞 UI）
    Communication_NATS::getInstance().initNATSConnect();

    LogService::instance().log("INFO", "后端服务初始化完成");
}

// ==================== 日志 ====================

void Server_DataReplay::log(const QString &level, const QString &message)
{
    LogService::instance().log(level, message);
}

// ==================== 想定管理 ====================

QList<ScenarioSummary> Server_DataReplay::scanScenarios() const
{
    return m_scenarioMgr->scanScenarios();
}

bool Server_DataReplay::loadScenario(const QString &filePath)
{
    return m_scenarioMgr->loadScenario(filePath);
}

const Scenario *Server_DataReplay::currentScenario() const
{
    return m_scenarioMgr->currentScenario();
}

QMap<QString, QString> Server_DataReplay::loadEntityIdMapping(const QString &scenarioDir)
{
    return m_scenarioMgr->loadEntityIdMapping(scenarioDir);
}

bool Server_DataReplay::saveEntityIdMapping(const QString &scenarioDir,
                                             const QMap<QString, QString> &newMappings)
{
    return m_scenarioMgr->saveEntityIdMapping(scenarioDir, newMappings);
}

bool Server_DataReplay::renameScenario(const QString &oldDirPath, const QString &newName)
{
    return m_scenarioMgr->renameScenario(oldDirPath, newName);
}

bool Server_DataReplay::renameDataFile(const QString &oldFilePath, const QString &newName)
{
    return m_scenarioMgr->renameDataFile(oldFilePath, newName);
}

bool Server_DataReplay::deleteScenario(const QString &scenarioDirPath)
{
    return m_scenarioMgr->deleteScenario(scenarioDirPath);
}

bool Server_DataReplay::deleteDataFile(const QString &filePath)
{
    return m_scenarioMgr->deleteDataFile(filePath);
}

int Server_DataReplay::addDataFiles(const QString &scenarioDir, const QStringList &filePaths)
{
    return m_scenarioMgr->addDataFiles(scenarioDir, filePaths);
}

QStringList Server_DataReplay::refreshDataFiles(const QString &scenarioDir)
{
    return m_scenarioMgr->refreshDataFiles(scenarioDir);
}

ImportPreview Server_DataReplay::previewImport(const QString &xmlSourcePath)
{
    return m_scenarioMgr->previewImport(xmlSourcePath);
}

bool Server_DataReplay::importScenario(const QString &xmlSourcePath,
                                        const QString &targetDirName)
{
    return m_scenarioMgr->importScenario(xmlSourcePath, targetDirName);
}

// ==================== 回放控制 ====================

bool Server_DataReplay::initReplay(const Scenario *scenario, const QString &selectedFile)
{
    return m_replayEngine->initialize(scenario, selectedFile);
}

bool Server_DataReplay::startReplay()
{
    return m_replayEngine->start();
}

bool Server_DataReplay::pauseReplay()
{
    return m_replayEngine->pause();
}

bool Server_DataReplay::resumeReplay()
{
    return m_replayEngine->resume();
}

bool Server_DataReplay::stopReplay()
{
    return m_replayEngine->stop();
}

void Server_DataReplay::setSpeed(int speed)
{
    m_replayEngine->setSpeed(speed);
}

void Server_DataReplay::setEntityIdMapping(const QMap<QString, QString> &mapping)
{
    m_replayEngine->setEntityIdMapping(mapping);
}

int Server_DataReplay::speed() const
{
    return m_replayEngine->speed();
}

Server_DataReplay::EngineState Server_DataReplay::state() const
{
    return static_cast<EngineState>(m_replayEngine->state());
}

double Server_DataReplay::overallProgress() const
{
    return m_replayEngine->overallProgress();
}
