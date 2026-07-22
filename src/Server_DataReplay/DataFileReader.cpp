/**
 * @file DataFileReader.cpp
 * @brief 大文件游标式顺序读取器的实现
 *
 * 实现基于文件游标的数据读取器，核心设计原则：
 * - 不将文件全文加载到内存，仅维护 QFile + qint64 游标位置
 * - 通过 readWindow() 按仿真时间窗口增量读取，避免重复扫描
 * - 按文件原始行序输出数据，保证发送顺序与源文件一致
 *
 * 单行数据格式（外层时间戳 + JSON）：
 *   2026-07-12 09:00:00.001 {"data":{"entity":"1001","simTime":{...},"cmd":"status",...}}
 */

#include "DataFileReader.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QTextStream>

DataFileReader::DataFileReader(QObject *parent)
    : QObject(parent)
{
}

DataFileReader::~DataFileReader()
{
    close();
}

bool DataFileReader::openFile(const QString &filePath)
{
    // 若已有打开的文件，先关闭
    if (m_file) {
        close();
    }

    m_file = new QFile(filePath);

    if (!m_file->open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "DataFileReader: Cannot open file:" << filePath;
        delete m_file;
        m_file = nullptr;
        return false;
    }

    // 重置内部状态
    m_cursorPos = 0;
    m_atEnd = false;
    m_infoScanned = false;

    // 预扫描：获取起止时间
    scanFileInfo();

    qDebug() << "DataFileReader: Opened" << filePath
             << "size:" << m_file->size();

    return true;
}

void DataFileReader::close()
{
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    m_cursorPos = 0;
    m_atEnd = false;
    m_infoScanned = false;
}

bool DataFileReader::isOpen() const
{
    return m_file != nullptr && m_file->isOpen();
}

DataFileInfo DataFileReader::fileInfo() const
{
    return m_fileInfo;
}

bool DataFileReader::atEnd() const
{
    return m_atEnd;
}

void DataFileReader::reset()
{
    // 重置到文件开头，配合 ReplayEngine::stop() 使用
    if (m_file) {
        m_file->seek(0);
        m_cursorPos = 0;
        m_atEnd = false;
    }
}

DataRecord DataFileReader::readFirstRecord()
{
    DataRecord record;

    if (!m_file || !m_file->isOpen()) {
        return record;
    }

    // 确保从文件开头读取
    m_file->seek(0);
    m_cursorPos = 0;
    m_atEnd = false;

    // 跳过空行，读取第一条有效数据行
    while (!m_file->atEnd()) {
        QByteArray rawLine = m_file->readLine();

        if (rawLine.isEmpty()) {
            continue;
        }

        // 去除行末尾的 \r \n
        while (rawLine.endsWith('\n') || rawLine.endsWith('\r')) {
            rawLine.chop(1);
        }

        if (rawLine.isEmpty()) {
            continue;
        }

        // 解析行
        QDateTime outerTimestamp;
        QByteArray jsonPart;
        if (!parseLine(rawLine, outerTimestamp, jsonPart)) {
            continue;
        }

        // 提取仿真时间
        QDateTime simTime = extractSimTime(jsonPart);
        if (!simTime.isValid()) {
            simTime = outerTimestamp;
        }

        // 填充记录
        record.simTimestamp = simTime;
        record.fullLine      = QString::fromUtf8(rawLine);
        record.jsonPayload   = QString::fromUtf8(jsonPart);

        // 游标前进到该行之后
        m_cursorPos = m_file->pos();
        m_lastReadTimestamp = simTime;
        return record;
    }

    // 文件为空或无有效行
    m_atEnd = true;
    return record;  // 返回空的 DataRecord
}

QList<DataRecord> DataFileReader::readWindow(const QDateTime &windowStart, int stepMs)
{
    QList<DataRecord> results;

    if (!m_file || !m_file->isOpen() || m_atEnd) {
        return results;
    }

    // 计算窗口结束时间：[windowStart, windowStart + stepMs)
    QDateTime windowEnd = windowStart.addMSecs(stepMs);

    // 从上次停止的游标位置继续读取（避免重复扫描）
    m_file->seek(m_cursorPos);

    while (!m_file->atEnd()) {
        // 记录当前行起始位置，用于回退游标
        qint64 lineStartPos = m_file->pos();
        QByteArray rawLine = m_file->readLine();

        if (rawLine.isEmpty()) {
            continue;
        }

        // 去除行末尾的 \r \n
        while (rawLine.endsWith('\n') || rawLine.endsWith('\r')) {
            rawLine.chop(1);
        }

        if (rawLine.isEmpty()) {
            continue;
        }

        // 解析行：分离外层时间戳和 JSON 部分
        QDateTime outerTimestamp;
        QByteArray jsonPart;
        if (!parseLine(rawLine, outerTimestamp, jsonPart)) {
            continue;
        }

        // 从 JSON 内部提取仿真时间（优先用 simTime.formatted）
        QDateTime simTime = extractSimTime(jsonPart);
        if (!simTime.isValid()) {
            // JSON 内无 simTime 时，回退到外层时间戳
            simTime = outerTimestamp;
        }

        // ---- 窗口判断（半开区间 [windowStart, windowEnd)） ----

        // 情况一：数据时间早于窗口 → 跳过，游标前进
        if (simTime < windowStart) {
            m_cursorPos = m_file->pos();
            m_lastReadTimestamp = simTime;
            continue;
        }

        // 情况二：数据时间超出窗口 → 停止本次读取，游标回退到此行开头
        if (simTime >= windowEnd) {
            m_cursorPos = lineStartPos;
            break;
        }

        // 情况三：在窗口范围内 → 加入结果集，游标前进
        DataRecord record;
        record.simTimestamp = simTime;
        record.fullLine = QString::fromUtf8(rawLine);
        record.jsonPayload = QString::fromUtf8(jsonPart);  // 仅 JSON 部分，不含外层时间戳
        results.append(record);

        m_cursorPos = m_file->pos();
        m_lastReadTimestamp = simTime;
    }

    // 标记文件是否已读完
    if (m_file->atEnd()) {
        m_atEnd = true;
    }

    // 发射单文件进度信号（基于时间范围的线性估算）
    if (m_fileInfo.maxTime.isValid() && m_fileInfo.minTime.isValid()) {
        qint64 totalMs = m_fileInfo.minTime.msecsTo(m_fileInfo.maxTime);
        if (totalMs > 0 && m_lastReadTimestamp.isValid()) {
            qint64 elapsedMs = m_fileInfo.minTime.msecsTo(m_lastReadTimestamp);
            double percent = qBound(0.0, (double)elapsedMs / totalMs * 100.0, 100.0);
            emit progressChanged(percent);
        }
    }

    return results;
}

bool DataFileReader::parseLine(const QByteArray &rawLine, QDateTime &outerTimestamp, QByteArray &jsonPart)
{
    // 行格式: "YYYY-MM-DD HH:MM:SS.zzz <JSON_DATA>"
    // 外层时间戳固定 23 字符，直接用 left(23) 截取，避免被 JSON 内部日期混淆
    QString lineStr = QString::fromUtf8(rawLine).trimmed();

    if (lineStr.length() < 23) {
        return false;
    }

    static const int TIMESTAMP_LEN = 23; // "YYYY-MM-DD HH:MM:SS.zzz"

    // 取前 23 字符作为外层时间戳
    QString tsStr = lineStr.left(TIMESTAMP_LEN);

    outerTimestamp = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss.zzz");
    if (!outerTimestamp.isValid()) {
        // 兼容无毫秒的 19 字符格式 "YYYY-MM-DD HH:MM:SS"
        tsStr = lineStr.left(19);
        outerTimestamp = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss");
        if (!outerTimestamp.isValid()) {
            return false;
        }
        jsonPart = lineStr.mid(19).trimmed().toUtf8();
    } else {
        jsonPart = lineStr.mid(TIMESTAMP_LEN).trimmed().toUtf8();
    }

    return !jsonPart.isEmpty();
}

QDateTime DataFileReader::extractSimTime(const QByteArray &jsonData)
{
    // JSON 格式: {"data":{"simTime":{"formatted":"...","epochMillis":...},...}}
    //
    // 优先使用 formatted 字符串（本地时间，与想定 XML 解析一致），
    // epochMillis 是 UTC 时间戳，在非 UTC 时区下会引入偏移。

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);

    if (error.error != QJsonParseError::NoError) {
        return QDateTime();
    }

    if (!doc.isObject()) {
        return QDateTime();
    }

    QJsonObject rootObj = doc.object();
    QJsonObject dataObj = rootObj.value("data").toObject();

    if (dataObj.isEmpty()) {
        return QDateTime();
    }

    QJsonValue simTimeVal = dataObj.value("simTime");
    if (simTimeVal.isUndefined()) {
        return QDateTime();
    }

    // 情况一：simTime 是对象 → 优先取 formatted，备选取 epochMillis
    QJsonObject simTimeObj = simTimeVal.toObject();
    if (!simTimeObj.isEmpty()) {
        if (simTimeObj.contains("formatted")) {
            QString formatted = simTimeObj.value("formatted").toString();
            QDateTime dt = QDateTime::fromString(formatted, "yyyy-MM-dd HH:mm:ss.zzz");
            if (!dt.isValid()) {
                dt = QDateTime::fromString(formatted, "yyyy-MM-dd HH:mm:ss");
            }
            return dt;
        }

        // 备选：epochMillis（UTC 毫秒时间戳）
        if (simTimeObj.contains("epochMillis")) {
            qint64 epochMs = (qint64)simTimeObj.value("epochMillis").toDouble();
            return QDateTime::fromMSecsSinceEpoch(epochMs);
        }
    }

    // 情况二：simTime 本身就是一个数字（epochMillis）
    if (simTimeVal.isDouble()) {
        return QDateTime::fromMSecsSinceEpoch((qint64)simTimeVal.toDouble());
    }

    return QDateTime();
}

void DataFileReader::scanFileInfo()
{
    if (!m_file || !m_file->isOpen() || m_infoScanned) {
        return;
    }

    qint64 fileSize = m_file->size();
    m_infoScanned = true;

    // ---- 读取第一行获取起始仿真时间 ----
    m_file->seek(0);
    QByteArray firstLine = m_file->readLine();
    if (firstLine.isEmpty()) {
        return;
    }

    QDateTime outerTs;
    QByteArray jsonPart;
    if (parseLine(firstLine, outerTs, jsonPart)) {
        m_fileInfo.minTime = extractSimTime(jsonPart);
        if (!m_fileInfo.minTime.isValid()) {
            m_fileInfo.minTime = outerTs;   // 回退到外层时间戳
        }
    }

    // ---- 从文件尾部读取最后一行获取结束仿真时间 ----
    const int bufferSize = 4096;
    if (fileSize > bufferSize) {
        // 大文件：只读尾部 4KB 缓冲区
        m_file->seek(fileSize - bufferSize);
        QByteArray tailBuffer = m_file->read(bufferSize);
        QString tailStr = QString::fromUtf8(tailBuffer);
        QStringList lines = tailStr.split('\n', QString::SkipEmptyParts);
        if (!lines.isEmpty()) {
            QString lastLine = lines.last().trimmed();
            QByteArray lastLineBytes = lastLine.toUtf8();
            if (parseLine(lastLineBytes, outerTs, jsonPart)) {
                m_fileInfo.maxTime = extractSimTime(jsonPart);
                if (!m_fileInfo.maxTime.isValid()) {
                    m_fileInfo.maxTime = outerTs;
                }
            }
        }
    } else {
        // 小文件：全量读取，取最后一行
        m_file->seek(0);
        QByteArray allData = m_file->readAll();
        QStringList lines = QString::fromUtf8(allData).split('\n', QString::SkipEmptyParts);
        if (!lines.isEmpty()) {
            QString lastLine = lines.last().trimmed();
            QByteArray lastLineBytes = lastLine.toUtf8();
            if (parseLine(lastLineBytes, outerTs, jsonPart)) {
                m_fileInfo.maxTime = extractSimTime(jsonPart);
                if (!m_fileInfo.maxTime.isValid()) {
                    m_fileInfo.maxTime = outerTs;
                }
            }
        }
    }

    // 重置到文件开头，准备正式读取
    m_file->seek(0);
    m_cursorPos = 0;

    qDebug() << "DataFileReader scan:" << m_file->fileName()
             << "time range:" << m_fileInfo.minTime << "-" << m_fileInfo.maxTime;
}
