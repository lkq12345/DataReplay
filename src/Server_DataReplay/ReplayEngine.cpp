/**
 * @file ReplayEngine.cpp
 * @brief 回放引擎的实现文件
 *
 * 实现数据回放的核心状态机和定时步进逻辑。
 * 耗时的数据读取、实体ID替换与 NATS 发布已下沉到 ReplayWorker（工作线程），
 * 本类（主线程）仅负责状态机、定时器、进度计算与 UI 信号发射。
 *
 * 线程模型：
 *   - 主线程：QTimer 定时触发 tick()，异步派发窗口处理任务，处理工作线程回传结果
 *   - 工作线程：ReplayWorker 执行 openFile/processWindow/reset/close
 *   - 代次号（m_worker->epoch()）为原子变量，保证 stop()/initialize() 能
 *     立即失效队列中尚未执行的旧窗口任务，避免"停止后仍发送旧数据"。
 */

#include "ReplayEngine.h"
#include "ScenarioMgr.h"
#include "Communication/Communication_NATS.h"

#include <QEventLoop>

ReplayEngine::ReplayEngine(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ReplayEngine::tick);

    // 创建专用工作线程并启动
    m_workerThread = new QThread(this);
    m_worker = new ReplayWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_worker, &ReplayWorker::logMessage, this, &ReplayEngine::logMessage);
    connect(m_worker, &ReplayWorker::windowProcessed, this, &ReplayEngine::onWindowProcessed);
    connect(m_worker, &ReplayWorker::fileOpened, this, &ReplayEngine::onFileOpened);
    connect(m_worker, &ReplayWorker::entityStatesUpdated,
            this, &ReplayEngine::entityStatesUpdated);

    m_workerThread->start();
}

ReplayEngine::~ReplayEngine()
{
    stop();

    if (m_workerThread) {
        // 先让 worker 线程同步关闭 reader，确保 DataFileReader 在其所属线程内释放，
        // 避免跨线程删除 QObject 的隐患。
        if (m_worker) {
            QMetaObject::invokeMethod(m_worker, "close", Qt::BlockingQueuedConnection);
        }
        // 停止工作线程事件循环并等待其退出，再释放 worker
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete m_worker;
    m_worker = nullptr;
}

bool ReplayEngine::initialize(const Scenario *scenario, const QString &selectedFile)
{
    if (!scenario) {
        emit errorOccurred("初始化失败：想定为空");
        return false;
    }

    // 如果正在播放或暂停中，先停止
    if (m_state == Playing || m_state == Paused) {
        stop();
    }

    m_scenario = scenario;

    // 确定数据文件路径：如果传入了有效路径则使用，否则回退到想定的第一个数据文件
    QString filePath = selectedFile;
    if (filePath.isEmpty()) {
        if (scenario->dataFiles.isEmpty()) {
            emit errorOccurred("初始化失败：没有可用的数据文件");
            m_scenario = nullptr;
            return false;
        }
        filePath = scenario->dataFiles.first();
    }

    if (!m_worker || !m_workerThread || !m_workerThread->isRunning()) {
        emit errorOccurred("初始化失败：工作线程未就绪");
        m_scenario = nullptr;
        return false;
    }

    // 递增代次号，使工作线程队列中尚未执行的旧窗口任务失效
    m_worker->bumpEpoch();
    const quint64 epoch = m_worker->epoch();
    m_processing = false;

    // ---- 异步请求工作线程打开文件，并用局部事件循环等待结果 ----
    // openFile 仅做首行 + 尾部 4KB 预扫描与首条 Init 记录读取，耗时极短；
    // 等待期间主线程继续处理 UI 事件，界面不冻结。
    QEventLoop loop;
    m_openOk = false;
    m_openInfo = DataFileInfo();
    m_openError.clear();
    m_openLoop = &loop;

    QMetaObject::invokeMethod(m_worker, "openFile", Qt::QueuedConnection,
                              Q_ARG(QString, filePath),
                              Q_ARG(quint64, epoch));

    loop.exec();
    m_openLoop = nullptr;

    if (!m_openOk) {
        emit logMessage("WARN", QString("无法打开数据文件: %1").arg(filePath));
        emit errorOccurred(m_openError.isEmpty() ? "初始化失败：无法打开数据文件" : m_openError);
        m_scenario = nullptr;
        return false;
    }

    // 获取数据文件的实际时间范围
    m_dataStartTime = m_openInfo.minTime.isValid() ? m_openInfo.minTime : QDateTime();
    m_dataEndTime = m_openInfo.maxTime.isValid() ? m_openInfo.maxTime : QDateTime();

    // 初始化 NATS 连接（内部含重连机制）
    Communication_NATS::getInstance().initNATSConnect();

    // 设置初始仿真时间 = 数据文件最早时间
    m_currentSimTime = m_dataStartTime;
    m_windowStart = m_dataStartTime;
    m_maxDataSimTime = QDateTime();
    m_state = Ready;
    emit stateChanged(m_state);
    emit logMessage("INFO", QString("回放引擎初始化完成 - %1 (数据范围: %2 ~ %3)")
                    .arg(scenario->name)
                    .arg(m_dataStartTime.isValid() ? m_dataStartTime.toString("HH:mm:ss.zzz") : "?")
                    .arg(m_dataEndTime.isValid() ? m_dataEndTime.toString("HH:mm:ss.zzz") : "?"));
    emit simTimeChanged(m_currentSimTime);
    emit progressChanged(0.0);

    return true;
}

bool ReplayEngine::start()
{
    if (m_state != Ready) {
        emit errorOccurred("开始失败：当前状态不允许开始（需要 Ready 状态）");
        return false;
    }

    m_state = Playing;
    emit stateChanged(m_state);

    updateTimerInterval();
    m_timer->start();

    emit logMessage("INFO", "回放已开始");
    return true;
}

bool ReplayEngine::pause()
{
    if (m_state != Playing) {
        return false;
    }

    m_timer->stop();
    m_state = Paused;
    emit stateChanged(m_state);
    emit logMessage("INFO", "回放已暂停");
    return true;
}

bool ReplayEngine::resume()
{
    if (m_state != Paused) {
        return false;
    }

    m_state = Playing;
    emit stateChanged(m_state);

    updateTimerInterval();
    m_timer->start();

    emit logMessage("INFO", "回放已继续");
    return true;
}

bool ReplayEngine::stop()
{
    if (m_state != Ready && m_state != Playing && m_state != Paused) {
        return false;
    }

    m_timer->stop();

    // 递增代次号（原子），使工作线程队列中尚未执行的旧窗口任务立即失效
    // 避免"停止后仍继续发送旧数据"。
    if (m_worker) {
        m_worker->bumpEpoch();
        // 异步通知 worker 重置游标（若紧接着重新 initialize，openFile 会重建 reader）
        QMetaObject::invokeMethod(m_worker, "reset", Qt::QueuedConnection);
    }

    m_processing = false;

    // 重置仿真时间和进度追踪
    m_currentSimTime = m_dataStartTime;
    m_windowStart = m_dataStartTime;
    m_maxDataSimTime = QDateTime();
    m_dataStartTime = QDateTime();
    m_dataEndTime = QDateTime();

    m_state = Stopped;
    emit stateChanged(m_state);
    emit logMessage("INFO", "回放已停止");
    emit simTimeChanged(m_currentSimTime);
    emit progressChanged(0.0);

    return true;
}

void ReplayEngine::setSpeed(int speed)
{
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;

    if (m_speed != speed) {
        m_speed = speed;
        emit logMessage("INFO", QString("倍速已设置为 %1x").arg(speed));

        if (m_state == Playing) {
            updateTimerInterval();
            m_timer->start();
        }
    }
}

void ReplayEngine::setEntityIdMapping(const QMap<QString, QString> &mapping)
{
    if (!m_worker) {
        return;
    }

    // 直接调用（worker 内部用 QMutex 保护，跨线程安全），立即同步生效。
    // 调用发生在 initialize() 之前，保证 Init 记录发送时映射已生效。
    m_worker->setEntityIdMapping(mapping);

    if (!mapping.isEmpty()) {
        emit logMessage("INFO", QString("已设置实体ID映射，共 %1 条").arg(mapping.size()));
    }
}

int ReplayEngine::speed() const
{
    return m_speed;
}

QStringList ReplayEngine::trackedAttributes() const
{
    return m_worker ? m_worker->trackedAttributes() : QStringList();
}

ReplayEngine::State ReplayEngine::state() const
{
    return m_state;
}

double ReplayEngine::overallProgress() const
{
    if (!m_dataStartTime.isValid() || !m_dataEndTime.isValid() ||
        m_dataStartTime >= m_dataEndTime) {
        return 0.0;
    }

    qint64 totalMs = m_dataStartTime.msecsTo(m_dataEndTime);
    if (totalMs <= 0) {
        return 0.0;
    }

    qint64 elapsedMs = m_dataStartTime.msecsTo(m_currentSimTime);
    return qBound(0.0, (double)elapsedMs / totalMs * 100.0, 100.0);
}

void ReplayEngine::updateTimerInterval()
{
    if (!m_scenario || m_speed <= 0) {
        return;
    }

    int intervalMs = m_scenario->simStepMs / m_speed;
    if (intervalMs < 1) intervalMs = 1;
    m_timer->setInterval(intervalMs);
}

void ReplayEngine::tick()
{
    if (!m_scenario || m_state != Playing || !m_worker) {
        return;
    }

    const QDateTime windowStart = m_currentSimTime;
    const int stepMs = m_scenario->simStepMs;

    // 前置判断：仿真时间是否已超过数据实际范围
    if (m_dataEndTime.isValid() && windowStart > m_dataEndTime) {
        finishReplay(m_dataEndTime, "回放完成 - 仿真时间已超过数据范围");
        return;
    }

    // 工作线程仍在处理上一个窗口 → 跳过本拍（背压），保证单文件游标不被并发访问
    if (m_processing) {
        return;
    }

    // 异步派发窗口处理任务到工作线程，tick 立即返回，主线程不被阻塞
    m_processing = true;
    const quint64 epoch = m_worker->epoch();
    QMetaObject::invokeMethod(m_worker, "processWindow", Qt::QueuedConnection,
                              Q_ARG(QDateTime, windowStart),
                              Q_ARG(int, stepMs),
                              Q_ARG(quint64, epoch));
}

void ReplayEngine::onWindowProcessed(const WindowResult &result)
{
    m_processing = false;

    // 丢弃停止/重初始化之前的过期结果
    if (!m_worker || result.epoch != m_worker->epoch()) {
        return;
    }
    if (m_state != Playing) {
        return;
    }

    // 更新已发送数据最大时间（文件按时间有序，最后一条即时间最大者）
    if (result.valid && result.lastSimTime.isValid()) {
        if (!m_maxDataSimTime.isValid() || result.lastSimTime > m_maxDataSimTime) {
            m_maxDataSimTime = result.lastSimTime;
        }
    }

    const QDateTime windowStart = result.windowStart;
    const int stepMs = m_scenario->simStepMs;

    // ======== 结束判断（优先于时间推进） ========
    if (result.valid && result.atEnd) {
        finishReplay(windowStart, "回放完成 - 数据文件已读取完毕");
        return;
    }

    if (m_dataEndTime.isValid() && windowStart >= m_dataEndTime) {
        finishReplay(m_dataEndTime, "回放完成 - 仿真时间已到达数据末尾");
        return;
    }

    // 推进仿真时间
    m_currentSimTime = windowStart.addMSecs(stepMs);
    emit simTimeChanged(m_currentSimTime);
    emit progressChanged(overallProgress());
}

void ReplayEngine::onFileOpened(bool ok, const DataFileInfo &info, const QString &error)
{
    m_openOk = ok;
    m_openInfo = info;
    m_openError = error;
    if (m_openLoop) {
        m_openLoop->quit();
    }
}

void ReplayEngine::finishReplay(const QDateTime &stopSimTime, const QString &reason)
{
    m_currentSimTime = stopSimTime;
    emit simTimeChanged(m_currentSimTime);
    emit progressChanged(100.0);
    m_timer->stop();
    m_state = Stopped;
    emit stateChanged(m_state);
    emit replayFinished();
    emit logMessage("INFO", reason);
}
