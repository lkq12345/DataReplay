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

            // 发送到 NATS（发布主题从配置文件读取，逐条发到所有发布主题）
            const QStringList publishTopics = Communication_NATS::getInstance().publishTopics();
            QByteArray payload = initRecord.jsonPayload.toUtf8();
            for (const QString &topic : publishTopics) {
                Communication_NATS::getInstance().sendMsgData(
                    (void *)payload.data(), payload.size(), topic);
            }

            emit dataSent(1);
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

QDateTime ReplayEngine::currentSimTime() const
{
    return m_currentSimTime;
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

        // ============================================================
        // 实体ID映射替换
        //
        // 目的：回放数据中 JSON payload 的实体ID是原始值（如 "entity":"car_001"），
        //       下游订阅者可能期望收到映射后的ID（如 "entity":"car_001_mapped"）。
        //       本段在每条数据发送前，将 JSON 中所有 "entity":"<原始ID>" 替换为映射值。
        //
        // 效率设计要点：
        //   1. static const 正则 —— 编译一次，所有 tick() 共享，避免重复编译开销
        //   2. hasNext() 预检 —— 无 entity 字段的记录直接跳过，零字符串操作
        //   3. midRef() 零拷贝切片 —— 只记录指针+偏移，不产生临时 QString 堆分配
        //   4. constFind() 只读查找 —— 不会因查找失败而向 QMap 隐式插入条目
        //   5. 延迟构造 result —— 仅在有匹配时才分配缓冲区，无匹配则原样保留 payload
        //   6. 单次遍历 —— 逐段拼接，只做一次正则全局扫描和一次最终 result 赋值
        //
        // 字符串拼接示意（假设 payload = {"entity":"A","entity":"B","val":1}）：
        //   lastEnd=0                         lastEnd 更新到匹配1的 valEnd
        //     ↓                                    ↓
        //   {"entity":"  A  ","entity":"  B  ","val":1}
        //              ↑    ↑          ↑    ↑
        //       valStart  valEnd  valStart  valEnd
        //   ──────→  midRef(0, valStart-0)  追加 "entity":" 之前的胶水部分
        //            → mapIt.value() 或 原始ID  追加替换后的ID（或保留原ID）
        //   ──────────────────────────→  midRef(lastEnd)  追加尾部剩余内容
        // ============================================================
        if (!m_entityIdMapping.isEmpty()) {
            // 正则：匹配 JSON 中 "entity":"值" 的模式
            // 捕获组1 ([^\"]*) 匹配双引号内的实体ID值（非引号的连续字符）
            // static const 确保正则对象只编译一次，后续每次 tick() 直接复用
            static const QRegularExpression entityRe(QStringLiteral("\"entity\":\"([^\"]*)\""));

            // 按引用遍历，直接修改原始数据，避免 DataRecord 拷贝
            for (DataRecord &record : allRecords) {
                QString &payload = record.jsonPayload;  // 引用，修改即修改 record

                // 对当前 payload 执行全局正则匹配，返回迭代器
                // 全局匹配会找出字符串中所有符合正则的位置（可能有多处 "entity" 字段）
                QRegularExpressionMatchIterator matchIt = entityRe.globalMatch(payload);

                // 快速路径：如果 payload 中没有 "entity" 字段，直接跳过
                // 不做任何字符串操作，避免无意义的 CPU 和内存开销
                if (!matchIt.hasNext())
                    continue;

                // 结果缓冲区 —— 延迟分配，仅当确实存在匹配时才构造
                QString result;
                int lastEnd = 0;  // 追踪上一次匹配结束的位置（原始字符串中的索引）

                // 遍历 payload 中所有的 "entity":"xxx" 匹配
                while (matchIt.hasNext()) {
                    QRegularExpressionMatch match = matchIt.next();

                    // 获取捕获组1（即实体ID值本身，不含引号）在原始字符串中的起止索引
                    // 例如 payload = {"entity":"car_001"} → capturedStart(1)=11, capturedEnd(1)=18
                    int valStart = match.capturedStart(1);  // 实体ID值的起始索引
                    int valEnd   = match.capturedEnd(1);    // 实体ID值的结束索引

                    // 将"上一次匹配结束位置"到"本次实体ID值起始位置"之间的内容原样追加
                    // midRef() 返回 QStringRef（零拷贝视图），不产生临时对象
                    // 这部分是 JSON 的"胶水"字符，如 {"entity":" 、 ,"entity":" 等
                    result += payload.midRef(lastEnd, valStart - lastEnd);

                    // 提取本次匹配到的原始实体ID（如 "car_001"）
                    QString entityId = match.captured(1);

                    // 在映射表中查找（constFind 只读查找，不会隐式插入新条目）
                    auto mapIt = m_entityIdMapping.constFind(entityId);
                    if (mapIt != m_entityIdMapping.constEnd() && !mapIt.value().isEmpty()) {
                        // 找到映射 → 使用替换后的ID
                        result += mapIt.value();
                    } else {
                        // 未找到或映射值为空 → 保留原始ID（安全兜底）
                        result += entityId;
                    }

                    // 更新位置指针，跳过已处理的实体ID值，准备处理下一个匹配
                    lastEnd = valEnd;
                }

                // 将最后一个匹配之后的所有尾部内容追加到结果
                // 同样使用 midRef() 零拷贝
                result += payload.midRef(lastEnd);

                // 用拼接好的结果替换原始 payload
                payload = result;
            }
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
        emit dataSent(allRecords.size());
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
