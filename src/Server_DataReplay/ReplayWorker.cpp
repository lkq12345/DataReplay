/**
 * @file ReplayWorker.cpp
 * @brief 回放工作线程对象的实现
 *
 * 实现 ReplayWorker，承担 DataFileReader 操作与 NATS 发布的耗时工作。
 * 所有槽方法均在 worker 线程执行，通过信号回传结果给主线程 ReplayEngine。
 */

#include "ReplayWorker.h"
#include "Communication/Communication_NATS.h"

#include <QByteArray>
#include <QRegularExpression>

ReplayWorker::ReplayWorker(QObject *parent)
    : QObject(parent)
{
    // 注册跨线程信号/槽需要排队传递的自定义类型
    qRegisterMetaType<DataFileInfo>("DataFileInfo");
    qRegisterMetaType<WindowResult>("WindowResult");
}

ReplayWorker::~ReplayWorker()
{
    close();
}

void ReplayWorker::setEntityIdMapping(const QMap<QString, QString> &mapping)
{
    QMutexLocker locker(&m_mappingMutex);
    m_mapping = mapping;
}

QMap<QString, QString> ReplayWorker::mappingSnapshot() const
{
    QMutexLocker locker(&m_mappingMutex);
    return m_mapping;   // 返回拷贝，释放锁后安全地在窗口处理循环中使用
}

void ReplayWorker::openFile(const QString &filePath, quint64 epoch)
{
    // 更新代次号，使队列中更早入队的旧窗口任务在随后执行时被识别为过期
    m_epoch.storeRelease(epoch);

    // 清理旧的读取器
    if (m_reader) {
        delete m_reader;
        m_reader = nullptr;
    }

    m_reader = new DataFileReader(this);
    if (!m_reader->openFile(filePath)) {
        delete m_reader;
        m_reader = nullptr;
        emit logMessage("WARN", QString("无法打开数据文件: %1").arg(filePath));
        emit fileOpened(false, DataFileInfo(), QString("初始化失败：无法打开数据文件"));
        return;
    }

    const DataFileInfo info = m_reader->fileInfo();

    // ---- 读取并发送首条 Init 记录（若存在） ----
    // 数据文件的第一条记录通常为初始化信息（含 "CMD":"Init"），
    // 在 initialize() 阶段立即发送，其余数据在 start() 后按仿真步长发送。
    DataRecord initRecord = m_reader->readFirstRecord();
    if (!initRecord.fullLine.isEmpty()
        && initRecord.jsonPayload.contains(QStringLiteral("\"CMD\":\"Init\""))) {

        applyEntityIdMapping(mappingSnapshot(), initRecord.jsonPayload);

        const QStringList publishTopics = Communication_NATS::getInstance().publishTopics();
        const QByteArray payload = initRecord.jsonPayload.toUtf8();
        for (const QString &topic : publishTopics) {
            Communication_NATS::getInstance().sendMsgData(
                (void *)payload.data(), payload.size(), topic);
        }

        emit logMessage("INFO", QString("初始化消息已发送 (simTime: %1)")
                        .arg(initRecord.simTimestamp.toString("HH:mm:ss.zzz")));
    } else {
        // 第一条记录不是 Init 消息 → 重置游标，保留数据不丢失
        m_reader->reset();
        if (!initRecord.fullLine.isEmpty()) {
            emit logMessage("INFO", "首条数据非 Init 消息，将在开始回放时发送");
        }
    }

    emit fileOpened(true, info, QString());
}

void ReplayWorker::processWindow(const QDateTime &windowStart, int stepMs, quint64 epoch)
{
    WindowResult result;
    result.epoch = epoch;
    result.windowStart = windowStart;

    // 代次不匹配（停止/重初始化前的过期任务）或 reader 不可用 → 返回无效结果
    if (epoch != m_epoch.loadAcquire() || !m_reader || !m_reader->isOpen()) {
        result.valid = false;
        emit windowProcessed(result);
        return;
    }

    // 从读取器按窗口取数据（文件已按时间有序，保持原始行序，不额外排序）
    QList<DataRecord> records;
    if (!m_reader->atEnd()) {
        records = m_reader->readWindow(windowStart, stepMs);
    }

    // 实体ID映射替换：对每条数据的 payload 执行4种正则模式匹配。
    // 先取一次映射快照（加锁拷贝），窗口内所有记录共用，避免逐条加锁。
    const QMap<QString, QString> mapping = mappingSnapshot();
    for (DataRecord &record : records) {
        applyEntityIdMapping(mapping, record.jsonPayload);
    }

    // 发布到 NATS（发布主题从配置文件读取，逐条发到所有发布主题）
    const QStringList publishTopics = Communication_NATS::getInstance().publishTopics();
    for (const DataRecord &record : records) {
        const QByteArray payload = record.jsonPayload.toUtf8();
        for (const QString &topic : publishTopics) {
            Communication_NATS::getInstance().sendMsgData(
                (void *)payload.data(), payload.size(), topic);
        }
    }

    result.valid = true;
    result.recordCount = records.size();
    result.atEnd = m_reader->atEnd();
    result.lastSimTime = records.isEmpty() ? QDateTime() : records.last().simTimestamp;

    emit windowProcessed(result);
}

void ReplayWorker::reset()
{
    if (m_reader) {
        m_reader->reset();
    }
}

void ReplayWorker::close()
{
    if (m_reader) {
        delete m_reader;
        m_reader = nullptr;
    }
}

void ReplayWorker::applyEntityIdMapping(const QMap<QString, QString> &mapping, QString &payload)
{
    if (mapping.isEmpty())
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
            auto mapIt = mapping.constFind(entityId);
            if (mapIt != mapping.constEnd() && !mapIt.value().isEmpty()) {
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
