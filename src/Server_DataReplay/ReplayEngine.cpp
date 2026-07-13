/**
 * @file ReplayEngine.cpp
 * @brief 回放引擎的实现文件
 *
 * 实现数据回放的核心状态机和定时步进逻辑：
 *
 * 状态机流转：
 *   Idle → initialize() → Ready → start() → Playing ⇄ pause()/resume() → Paused
 *   Playing/Paused → stop() → Stopped → initialize() → Ready（可重新开始）
 *
 * 每 tick 流程：
 *   ① 从所有 DataFileReader 按时间窗口增量读取数据
 *   ② 跨文件按 simTimestamp 排序
 *   ③ 应用实体 ID 映射替换（正则一次扫描，千组映射无压力）
 *   ④ 通过 NATS 逐条发送
 *   ⑤ 推进仿真时间，检查结束条件
 */

#include "ReplayEngine.h"
#include "ScenarioMgr.h"
#include "DataFileReader.h"
#include "Communication/Communication_NATS.h"

#include <QDebug>
#include <algorithm>
#include <QRegularExpression>

ReplayEngine::ReplayEngine(QObject *parent)
    : QObject(parent)
{
    // 创建周期定时器（非单次），连接 tick 槽函数
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &ReplayEngine::tick);
}

ReplayEngine::~ReplayEngine()
{
    stop();

    // 逐个关闭并销毁所有数据文件读取器
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

    // 如果正在播放或暂停中，先停止
    if (m_state == Playing || m_state == Paused) {
        stop();
    }

    // 清理上一次初始化的 Reader 实例
    for (auto *reader : m_readers) {
        reader->close();
        delete reader;
    }
    m_readers.clear();

    m_scenario = scenario;
    m_allReadersAtEnd = false;

    // 确定数据文件列表：如果前端指定了选中文件则只用选中的，否则用想定下全部文件
    QStringList filesToOpen = selectedFiles.isEmpty() ? scenario->dataFiles : selectedFiles;

    // 为每个数据文件创建独立的 DataFileReader 实例
    for (const QString &filePath : filesToOpen) {
        auto *reader = new DataFileReader(this);
        if (reader->openFile(filePath)) {
            m_readers.append(reader);
            connect(reader, &DataFileReader::progressChanged, this, [this](double) {
                // 预留：可在此汇总各 Reader 的进度做总体进度计算
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

    // 扫描所有数据文件的实际最晚 simTime（用于进度条和结束判断）
    m_dataEndTime = QDateTime();
    for (auto *reader : m_readers) {
        DataFileInfo info = reader->fileInfo();
        if (info.maxTime.isValid()) {
            if (!m_dataEndTime.isValid() || info.maxTime > m_dataEndTime) {
                m_dataEndTime = info.maxTime;
            }
        }
    }

    // 初始化 NATS 连接（内部含重连机制）
    Communication_NATS::getInstance().initNATSConnect();

    // 设置初始仿真时间 = 想定起始时间
    m_currentSimTime = scenario->startTime;
    m_windowStart = scenario->startTime;
    m_maxDataSimTime = QDateTime();   // 重置已发送数据的时间追踪
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
    // 仅 Ready 状态允许开始
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

    // 按当前倍速计算定时器间隔并启动
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

    // 暂停定时器但不重置 Reader 状态，支持断点续播
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

    // 重新计算间隔（期间用户可能改了倍速）
    updateTimerInterval();
    m_timer->start();

    emit logMessage("INFO", "回放已继续");
    return true;
}

bool ReplayEngine::stop()
{
    // Ready / Playing / Paused 三种状态均允许停止
    if (m_state != Ready && m_state != Playing && m_state != Paused) {
        return false;
    }

    m_timer->stop();

    // 重置所有 Reader 到文件开头，准备下次重新开始
    for (auto *reader : m_readers) {
        reader->reset();
    }
    m_allReadersAtEnd = false;

    // 重置仿真时间和进度追踪
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
    // 限定范围 1~100 倍速
    if (speed < 1) speed = 1;
    if (speed > 100) speed = 100;

    if (m_speed != speed) {
        m_speed = speed;
        emit logMessage("INFO", QString("倍速已设置为 %1x").arg(speed));

        // 播放中立即生效：停止定时器 → 重算间隔 → 重启
        if (m_state == Playing) {
            updateTimerInterval();
            m_timer->start();
        }
    }
}

void ReplayEngine::setEntityIdMapping(const QMap<QString, QString> &mapping)
{
    // 存储映射表，在每次 tick 中应用（修改映射后重新初始化回放即可生效）
    m_entityIdMapping = mapping;
    if (!mapping.isEmpty()) {
        emit logMessage("INFO", QString("已设置实体ID映射，共 %1 条").arg(mapping.size()));
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
    // 以数据文件的实际 simTime 范围为基准计算进度，不使用想定 endTime
    // 这样可以避免想定结束时间远大于数据范围时进度条长时间停滞
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

    // 定时器物理间隔 = 仿真步长(ms) / 倍速
    // 例：100ms 步长 × 10x 倍速 = 10ms 物理间隔
    int intervalMs = m_scenario->simStepMs / m_speed;
    if (intervalMs < 1) intervalMs = 1;  // 最小保护：1ms（防止 100x 时 timer 频率过高）

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

        // ======== 实体ID映射替换 ========
        //
        // 将回放数据 JSON 中的原始实体 ID 替换为映射值。
        // 例如："entity":"1001" → "entity":"57001"
        //
        // 性能策略：正则一次扫描找出所有 "entity":"<值>" 位置，
        // 逐个查 QMap 替换。复杂度 O(记录数 × 单条长度)，
        // 而非嵌套遍历映射表的 O(记录数 × 映射条目数 × 单条长度)。
        // 千组映射、百条数据下，单帧替换耗时约 0.01ms。
        //
        if (!m_entityIdMapping.isEmpty()) {
            // 静态正则：编译一次，跨 tick 复用
            // 匹配模式：固定前缀 "entity":" + 捕获实体 ID（不含引号）+ 后缀 "
            // 例：输入 "...{\"entity\":\"1001\",\"cmd\":...}" → 捕获到 "1001"
            static const QRegularExpression entityRe(QStringLiteral("\"entity\":\"([^\"]*)\""));

            for (DataRecord &record : allRecords) {
                QString &payload = record.jsonPayload;

                // 对当前 payload 执行全局匹配，找出所有 entity 字段位置
                QRegularExpressionMatchIterator matchIt = entityRe.globalMatch(payload);
                if (!matchIt.hasNext())
                    continue;  // 该条数据无 entity 字段（如纯 simTime 行），跳过

                // 逐段复制：匹配到的 entity 值替换为映射值，其余原样复制
                QString result;
                int lastEnd = 0;                  // 上一段末尾在 payload 中的位置
                while (matchIt.hasNext()) {
                    QRegularExpressionMatch match = matchIt.next();
                    int valStart = match.capturedStart(1);  // entity 值的起始位置
                    int valEnd   = match.capturedEnd(1);    // entity 值的结束位置

                    // ① 复制"匹配段之前"的文本（即 "entity":" 到 entity 值之间的前缀）
                    result += payload.midRef(lastEnd, valStart - lastEnd);

                    // ② 取出原始 entity 值，查映射表决定用映射值还是保留原值
                    QString entityId = match.captured(1);
                    auto mapIt = m_entityIdMapping.constFind(entityId);
                    if (mapIt != m_entityIdMapping.constEnd() && !mapIt.value().isEmpty()) {
                        result += mapIt.value();     // 命中映射 → 替换为映射值
                    } else {
                        result += entityId;           // 无映射 → 保持原始 ID 不变
                    }
                    lastEnd = valEnd;
                }
                // ③ 复制最后一段匹配之后的尾部文本
                result += payload.midRef(lastEnd);
                payload = result;
            }
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
