/**
 * @file ReplayEngine.cpp
 * @brief 回放引擎的实现文件（单文件版本）
 *
 * 实现数据回放的核心状态机和定时步进逻辑。
 * 仅支持单个数据文件，所有多文件相关逻辑已简化为单文件处理。
 */

#include "ReplayEngine.h"
#include "ScenarioMgr.h"
#include "DataFileReader.h"
#include "Communication/Communication_NATS.h"

#include <QDebug>
#include <QRegularExpression>

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
    if (m_reader) {
        m_reader->close();
        delete m_reader;
        m_reader = nullptr;
    }
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

    // 清理旧的 Reader 实例
    if (m_reader) {
        m_reader->close();
        delete m_reader;
        m_reader = nullptr;
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

    m_reader = new DataFileReader(this);
    if (!m_reader->openFile(filePath)) {
        delete m_reader;
        m_reader = nullptr;
        emit logMessage("WARN", QString("无法打开数据文件: %1").arg(filePath));
        emit errorOccurred("初始化失败：无法打开数据文件");
        m_scenario = nullptr;
        return false;
    }

    // 连接进度信号（可选）
    connect(m_reader, &DataFileReader::progressChanged, this, [this](double) {
        // 预留
    });

    // 获取数据文件的实际时间范围
    DataFileInfo info = m_reader->fileInfo();
    m_dataStartTime = info.minTime.isValid() ? info.minTime : QDateTime();
    m_dataEndTime = info.maxTime.isValid() ? info.maxTime : QDateTime();

    // 初始化 NATS 连接（内部含重连机制）
    Communication_NATS::getInstance().initNATSConnect();

    // ---- 读取并发送第一条 Init 记录（若存在） ----
    // 数据文件的第一条记录通常为初始化信息（含 "CMD":"Init"），
    // 在 initialize() 阶段立即发送，其余数据在 start() 后按仿真步长发送。
    {
        DataRecord initRecord = m_reader->readFirstRecord();

        if (!initRecord.fullLine.isEmpty()
            && initRecord.jsonPayload.contains(QStringLiteral("\"CMD\":\"Init\""))) {

            // 对 Init 记录的 payload 执行实体ID映射替换
            applyEntityIdMapping(initRecord.jsonPayload);

            // 发送到 NATS（发布主题从配置文件读取，逐条发到所有发布主题）
            const QStringList publishTopics = Communication_NATS::getInstance().publishTopics();
            QByteArray payload = initRecord.jsonPayload.toUtf8();
            for (const QString &topic : publishTopics) {
                Communication_NATS::getInstance().sendMsgData(
                    (void *)payload.data(), payload.size(), topic);
            }

            emit logMessage("INFO", QString("初始化消息已发送 (simTime: %1)")
                            .arg(initRecord.simTimestamp.toString("HH:mm:ss.zzz")));

            // 游标已前进到 Init 行之后，start() 将从第二条记录开始读取
        } else {
            // 第一条记录不是 Init 消息 → 重置游标，保留数据不丢失
            m_reader->reset();
            if (!initRecord.fullLine.isEmpty()) {
                emit logMessage("INFO", "首条数据非 Init 消息，将在开始回放时发送");
            }
        }
    }

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

    if (!m_reader) {
        emit errorOccurred("开始失败：没有数据文件");
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

    // 重置 Reader 到文件开头
    if (m_reader) {
        m_reader->reset();
    }
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
    if (speed > 10) speed = 10;

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

void ReplayEngine::applyEntityIdMapping(QString &payload)
{
    if (m_entityIdMapping.isEmpty())
        return;

    // ============================================================
    // 实体ID映射替换（支持4种正则模式，依次处理同一payload）
    //
    // 模式1: "entity":4012          —— JSON数字值（key="entity"），捕获数字
    // 模式2: "entityId":[4012]      —— JSON数组值，捕获数组内数字
    // 模式3: 实体ID：4012           —— 中文标记+全角冒号，捕获数字
    // 模式4: "id":"4012"            —— JSON字符串值（key="id"），捕获引号内数字
    //
    // 注：模式的引号前均有 \\? 可选反斜杠，以兼容 JSON 字符串值
    //     内部的转义形式（如 \"entityId\":[2010]），确保无论 pattern 出现在
    //     JSON key 位置还是 JSON string value 内部都能正确匹配。
    //
    // 每种模式的捕获组1提取原始ID，在映射表中查找并替换为映射值。
    // 4个模式按序依次处理，前一个模式的替换结果作为后一个模式的输入。
    // 模式间互斥（不会匹配到同一段文本），顺序处理安全无副作用。
    // ============================================================
    static const QRegularExpression reEntity(      QStringLiteral(R"(\\?"entity\\?":(\d+))"));
    static const QRegularExpression reEntityIdArr( QStringLiteral(R"(\\?"entityId\\?":\[(\d+)\])"));
    static const QRegularExpression reChineseId(   QStringLiteral(R"(实体ID：(\d+))"));
    static const QRegularExpression reIdStr(       QStringLiteral(R"(\\?"id\\?":\\?"(\d+)\\?")"));

    static const QRegularExpression* const patterns[] = {
        &reEntity, &reEntityIdArr, &reChineseId, &reIdStr
    };

    // 依次用每种模式匹配并替换
    for (const auto *re : patterns) {
        QRegularExpressionMatchIterator matchIt = re->globalMatch(payload);
        if (!matchIt.hasNext())
            continue;

        QString result;
        int lastEnd = 0;

        while (matchIt.hasNext()) {
            QRegularExpressionMatch match = matchIt.next();
            int valStart = match.capturedStart(1);
            int valEnd   = match.capturedEnd(1);

            result += payload.midRef(lastEnd, valStart - lastEnd);

            QString entityId = match.captured(1);
            auto mapIt = m_entityIdMapping.constFind(entityId);
            if (mapIt != m_entityIdMapping.constEnd() && !mapIt.value().isEmpty()) {
                result += mapIt.value();
            } else {
                result += entityId;
            }

            lastEnd = valEnd;
        }

        result += payload.midRef(lastEnd);
        payload = result;
    }
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
    if (!m_scenario || m_state != Playing || !m_reader) {
        return;
    }

    QDateTime windowStart = m_currentSimTime;
    int stepMs = m_scenario->simStepMs;

    // 前置判断：仿真时间是否已超过数据实际范围
    if (m_dataEndTime.isValid() && windowStart > m_dataEndTime) {
        m_currentSimTime = m_dataEndTime;
        emit simTimeChanged(m_currentSimTime);
        emit progressChanged(100.0);
        m_timer->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit replayFinished();
        emit logMessage("INFO", "回放完成 - 仿真时间已超过数据范围");
        return;
    }

    // 从唯一 Reader 读取窗口数据
    QList<DataRecord> allRecords;
    if (!m_reader->atEnd()) {
        allRecords = m_reader->readWindow(windowStart, stepMs);
    }

    // 处理读取到的数据
    if (!allRecords.isEmpty()) {
        // 数据按文件行序读取（文件中已按时间有序），不进行排序，
        // 以保证发送顺序与源文件数据先后完全一致。

        // 更新已发送数据最大时间（文件按时间有序，最后一条即时间最大者）
        const QDateTime &lastSimTime = allRecords.last().simTimestamp;
        if (!m_maxDataSimTime.isValid() || lastSimTime > m_maxDataSimTime) {
            m_maxDataSimTime = lastSimTime;
        }

        // 实体ID映射替换：对每条数据的 payload 执行4种正则模式匹配，
        // 将匹配到的原始ID替换为映射值。详见 applyEntityIdMapping()。
        for (DataRecord &record : allRecords) {
            applyEntityIdMapping(record.jsonPayload);
        }

        // 发送到 NATS（发布主题从配置文件读取，逐条发到所有发布主题）
        const QStringList publishTopics = Communication_NATS::getInstance().publishTopics();
        for (const DataRecord &record : allRecords) {
            QByteArray payload = record.jsonPayload.toUtf8();
            for (const QString &topic : publishTopics) {
                Communication_NATS::getInstance().sendMsgData(
                    (void *)payload.data(), payload.size(), topic);
            }
        }
    }

    // ======== 结束判断（优先于时间推进） ========
    if (m_reader->atEnd()) {
        m_currentSimTime = windowStart;   // 停留在最后一条数据的时间
        emit simTimeChanged(m_currentSimTime);
        emit progressChanged(100.0);
        m_timer->stop();
        m_state = Stopped;
        emit stateChanged(m_state);
        emit replayFinished();
        emit logMessage("INFO", "回放完成 - 数据文件已读取完毕");
        return;
    }

    if (m_dataEndTime.isValid() && windowStart >= m_dataEndTime) {
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

    // 推进仿真时间
    m_currentSimTime = windowStart.addMSecs(stepMs);
    emit simTimeChanged(m_currentSimTime);
    emit progressChanged(overallProgress());
}
