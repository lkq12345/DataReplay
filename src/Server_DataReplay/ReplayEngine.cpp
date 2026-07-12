#include "ReplayEngine.h"
#include "ScenarioMgr.h"
#include "DataFileReader.h"
#include "Communication/Communication_NATS.h"

#include <QDebug>
#include <algorithm>

ReplayEngine::ReplayEngine(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ReplayEngine::tick);
}

ReplayEngine::~ReplayEngine()
{
    stop();

    // 清理 Reader
    for (auto *reader : m_readers) {
        reader->close();
        delete reader;
    }
    m_readers.clear();
}

bool ReplayEngine::initialize(const Scenario *scenario, const QStringList &selectedFiles)
{
    if (!scenario) {
        emit errorOccurred("初始化失败：想定为空");
        return false;
    }

    if (m_state == Playing || m_state == Paused) {
        stop();
    }

    // 清理旧 Reader
    for (auto *reader : m_readers) {
        reader->close();
        delete reader;
    }
    m_readers.clear();

    m_scenario = scenario;
    m_allReadersAtEnd = false;

    // 确定要使用的数据文件列表
    QStringList filesToOpen = selectedFiles.isEmpty() ? scenario->dataFiles : selectedFiles;

    // 为每个数据文件创建 DataFileReader
    for (const QString &filePath : filesToOpen) {
        auto *reader = new DataFileReader(this);
        if (reader->openFile(filePath)) {
            m_readers.append(reader);
            connect(reader, &DataFileReader::progressChanged, this, [this](double) {
                // 可在此汇总所有 Reader 的进度
            });
        } else {
            delete reader;
            emit logMessage("WARN", QString("无法打开数据文件: %1").arg(filePath));
        }
    }

    if (m_readers.isEmpty()) {
        emit errorOccurred("初始化失败：没有可用的数据文件");
        m_scenario = nullptr;
        return false;
    }

    // 扫描所有数据文件的实际最晚 simTime（用于进度计算和结束判断）
    m_dataEndTime = QDateTime();
    for (auto *reader : m_readers) {
        DataFileInfo info = reader->fileInfo();
        if (info.maxTime.isValid()) {
            if (!m_dataEndTime.isValid() || info.maxTime > m_dataEndTime) {
                m_dataEndTime = info.maxTime;
            }
        }
    }

    // 连接 NATS
    Communication_NATS::getInstance().initNATSConnect();

    // 设置初始仿真时间
    m_currentSimTime = scenario->startTime;
    m_windowStart = scenario->startTime;
    m_maxDataSimTime = QDateTime();   // 重置为无效
    m_state = Ready;
    emit stateChanged(m_state);
    emit logMessage("INFO", QString("回放引擎初始化完成 - %1 (%2个数据文件, 数据范围: %3 ~ %4)")
                    .arg(scenario->name)
                    .arg(m_readers.size())
                    .arg(scenario->startTime.toString("HH:mm:ss"))
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

    if (m_readers.isEmpty()) {
        emit errorOccurred("开始失败：没有数据文件");
        return false;
    }

    m_state = Playing;
    emit stateChanged(m_state);

    // 启动定时器
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

    // 重置所有 Reader
    for (auto *reader : m_readers) {
        reader->reset();
    }
    m_allReadersAtEnd = false;

    // 重置仿真时间
    if (m_scenario) {
        m_currentSimTime = m_scenario->startTime;
        m_windowStart = m_scenario->startTime;
    }
    m_maxDataSimTime = QDateTime();
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

        // 如果在播放中，立即更新定时器间隔
        if (m_state == Playing) {
            updateTimerInterval();
            m_timer->start();  // 重新用新间隔启动
        }
    }
}

int ReplayEngine::speed() const
{
    return m_speed;
}

ReplayEngine::State ReplayEngine::state() const
{
    return m_state;
}

QDateTime ReplayEngine::currentSimTime() const
{
    return m_currentSimTime;
}

double ReplayEngine::overallProgress() const
{
    // 以实际数据文件的 simTime 范围为基准，而不是想定的 endTime
    if (!m_scenario || !m_dataEndTime.isValid() ||
        m_scenario->startTime >= m_dataEndTime) {
        return 0.0;
    }

    qint64 totalMs = m_scenario->startTime.msecsTo(m_dataEndTime);
    if (totalMs <= 0) {
        return 0.0;
    }

    qint64 elapsedMs = m_scenario->startTime.msecsTo(m_currentSimTime);
    return qBound(0.0, (double)elapsedMs / totalMs * 100.0, 100.0);
}

QString ReplayEngine::stateName(State state)
{
    switch (state) {
        case Idle:     return "Idle";
        case Ready:    return "Ready";
        case Playing:  return "Playing";
        case Paused:   return "Paused";
        case Stopped:  return "Stopped";
    }
    return "Unknown";
}

void ReplayEngine::updateTimerInterval()
{
    if (!m_scenario || m_speed <= 0) {
        return;
    }

    // 物理间隔 = 仿真步长 / 倍速
    int intervalMs = m_scenario->simStepMs / m_speed;
    if (intervalMs < 1) intervalMs = 1;  // 最小 1ms

    m_timer->setInterval(intervalMs);
}

void ReplayEngine::tick()
{
    if (!m_scenario || m_state != Playing) {
        return;
    }

    QDateTime windowStart = m_currentSimTime;
    int stepMs = m_scenario->simStepMs;

    // ======== 前置判断：仿真时间是否已超过数据实际范围 ========
    // 基于初始化时扫描的数据文件最晚 simTime 做精确判断
    if (m_dataEndTime.isValid() && windowStart > m_dataEndTime) {
        m_currentSimTime = m_dataEndTime;
        emit simTimeChanged(m_currentSimTime);
        emit progressChanged(100.0);
        m_timer->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit replayFinished();
        emit logMessage("INFO", "回放完成 - 仿真时间已超过所有数据范围");
        return;
    }

    // 从所有 Reader 读取窗口数据
    QList<DataRecord> allRecords;

    for (auto *reader : m_readers) {
        if (reader->atEnd()) {
            continue;
        }

        QList<DataRecord> records = reader->readWindow(windowStart, stepMs);
        if (!records.isEmpty()) {
            allRecords.append(records);
        }
    }

    // 检查是否所有 Reader 都已到达末尾
    bool allEnd = true;
    for (auto *reader : m_readers) {
        if (!reader->atEnd()) {
            allEnd = false;
            break;
        }
    }
    m_allReadersAtEnd = allEnd;

    // 跨文件排序 + NATS 发送
    if (!allRecords.isEmpty()) {
        std::sort(allRecords.begin(), allRecords.end(),
                  [](const DataRecord &a, const DataRecord &b) {
                      return a.simTimestamp < b.simTimestamp;
                  });

        // 更新已发送数据的最大 simTime
        const QDateTime &lastSimTime = allRecords.last().simTimestamp;
        if (!m_maxDataSimTime.isValid() || lastSimTime > m_maxDataSimTime) {
            m_maxDataSimTime = lastSimTime;
        }

        QString replayTopic = "DataReplay";
        for (const DataRecord &record : allRecords) {
            QByteArray payload = record.jsonPayload.toUtf8();
            Communication_NATS::getInstance().sendMsgData(
                (void *)payload.data(), payload.size(), replayTopic);
        }
        emit dataSent(allRecords.size());
    }

    // ======== 在推进 simTime 之前判断结束 ========
    // （确保最终显示的 simTime 不超过最后一条数据的时间）
    if (allEnd) {
        m_currentSimTime = windowStart;   // 停留在当前窗口起始（即最后数据时间）
        emit simTimeChanged(m_currentSimTime);
        emit progressChanged(100.0);
        m_timer->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit replayFinished();
        emit logMessage("INFO", "回放完成 - 所有数据文件已读取完毕");
        return;
    }

    if (m_dataEndTime.isValid() && windowStart >= m_dataEndTime) {
        // 即使 Reader 没到末尾，仿真时间已到达数据范围终点
        m_currentSimTime = m_dataEndTime;
        emit simTimeChanged(m_currentSimTime);
        emit progressChanged(100.0);
        m_timer->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit replayFinished();
        emit logMessage("INFO", "回放完成 - 仿真时间已到达数据末尾");
        return;
    }

    // 推进仿真时间 + 发射信号（正常路径）
    m_currentSimTime = windowStart.addMSecs(stepMs);
    emit simTimeChanged(m_currentSimTime);
    emit progressChanged(overallProgress());

    // 到达想定的仿真结束时间
    if (m_currentSimTime >= m_scenario->endTime) {
        m_timer->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit replayFinished();
        emit logMessage("INFO", "回放完成 - 已到达想定结束时间");
    }
}
