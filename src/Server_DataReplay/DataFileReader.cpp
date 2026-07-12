#include "DataFileReader.h"

#include <QFileInfo>
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

    QFileInfo fi(filePath);
    m_fileInfo.filePath = fi.absoluteFilePath();
    m_fileInfo.fileSize = fi.size();

    m_cursorPos = 0;
    m_atEnd = false;
    m_infoScanned = false;

    // 预扫描文件信息
    scanFileInfo();

    qDebug() << "DataFileReader: Opened" << filePath
             << "size:" << m_fileInfo.fileSize
             << "records:" << m_fileInfo.recordCount;

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
    if (m_file) {
        m_file->seek(0);
        m_cursorPos = 0;
        m_atEnd = false;
    }
}

QList<DataRecord> DataFileReader::readWindow(const QDateTime &windowStart, int stepMs)
{
    QList<DataRecord> results;

    if (!m_file || !m_file->isOpen() || m_atEnd) {
        return results;
    }

    QDateTime windowEnd = windowStart.addMSecs(stepMs);

    // 从游标当前位置开始读取
    m_file->seek(m_cursorPos);

    while (!m_file->atEnd()) {
        qint64 lineStartPos = m_file->pos();
        QByteArray rawLine = m_file->readLine();

        if (rawLine.isEmpty()) {
            continue;
        }

        // 去除末尾换行符
        while (rawLine.endsWith('\n') || rawLine.endsWith('\r')) {
            rawLine.chop(1);
        }

        if (rawLine.isEmpty()) {
            continue;
        }

        // 解析行数据
        QDateTime outerTimestamp;
        QByteArray jsonPart;
        if (!parseLine(rawLine, outerTimestamp, jsonPart)) {
            continue;
        }

        // 从 JSON 中提取仿真时间
        QDateTime simTime = extractSimTime(jsonPart);
        if (!simTime.isValid()) {
            // 如果无法提取 simTime，尝试使用外层时间戳
            simTime = outerTimestamp;
        }

        // 窗口匹配：检查 simTime 是否在 [windowStart, windowEnd) 范围内
        if (simTime < windowStart) {
            // 当前数据时间早于窗口起始，跳过并更新游标
            m_cursorPos = m_file->pos();
            m_lastReadTimestamp = simTime;
            continue;
        }

        if (simTime >= windowEnd) {
            // 当前数据已超出窗口范围，停止读取
            // 游标保持当前位置（以便下次从当前位置开始）
            // 但需要把游标回退到这一行的起始位置
            m_cursorPos = lineStartPos;
            break;
        }

        // 在窗口范围内，加入结果
        DataRecord record;
        record.simTimestamp = simTime;
        record.fullLine = QString::fromUtf8(rawLine);
        record.jsonPayload = QString::fromUtf8(jsonPart);
        results.append(record);

        m_cursorPos = m_file->pos();
        m_lastReadTimestamp = simTime;
    }

    // 检查是否到达文件末尾
    if (m_file->atEnd()) {
        m_atEnd = true;
    }

    // 发射进度信号
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
    // 格式: "YYYY-MM-DD HH:MM:SS.zzz JSON_DATA"
    // 外层时间戳固定长度 23 字符: "2026-07-12 09:00:00.000"
    // 直接用 left(23) 截取时间戳，避免被 JSON 内部的日期混扰
    QString lineStr = QString::fromUtf8(rawLine).trimmed();

    if (lineStr.length() < 23) {
        return false;
    }

    static const int TIMESTAMP_LEN = 23; // "YYYY-MM-DD HH:MM:SS.zzz"

    // 取前 23 个字符作为时间戳，跳过后面的空格定位 JSON
    QString tsStr = lineStr.left(TIMESTAMP_LEN);

    outerTimestamp = QDateTime::fromString(tsStr, "yyyy-MM-dd HH:mm:ss.zzz");
    if (!outerTimestamp.isValid()) {
        // 尝试无毫秒格式 "YYYY-MM-DD HH:MM:SS" (19字符)
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
    // JSON 格式: {"data":{"simTime":{"formatted":"...","epochMillis":...},"entity":"...",...}}
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

    // 优先使用 formatted 字符串（不涉及时区，与想定 XML 解析一致）
    // epochMillis 是 UTC 时间戳，在非 UTC 时区下会引入偏移
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

        // 备选：使用 epochMillis（UTC 时间戳）
        if (simTimeObj.contains("epochMillis")) {
            qint64 epochMs = (qint64)simTimeObj.value("epochMillis").toDouble();
            return QDateTime::fromMSecsSinceEpoch(epochMs);
        }
    }

    // 如果 simTime 本身就是个数字（epoch millis）
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

    qint64 fileSize = m_fileInfo.fileSize;
    m_fileInfo.recordCount = 0;
    m_infoScanned = true;

    // 读取第一行获取起始时间
    m_file->seek(0);
    QByteArray firstLine = m_file->readLine();
    if (firstLine.isEmpty()) {
        return;
    }

    m_fileInfo.recordCount++;

    QDateTime outerTs;
    QByteArray jsonPart;
    if (parseLine(firstLine, outerTs, jsonPart)) {
        m_fileInfo.minTime = extractSimTime(jsonPart);
        if (!m_fileInfo.minTime.isValid()) {
            m_fileInfo.minTime = outerTs;
        }
    }

    // 从文件尾倒序读取最后一行获取结束时间
    const int bufferSize = 4096;
    if (fileSize > bufferSize) {
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
        // 小文件：读取所有行，取最后一行
        m_file->seek(0);
        QByteArray allData = m_file->readAll();
        QStringList lines = QString::fromUtf8(allData).split('\n', QString::SkipEmptyParts);
        m_fileInfo.recordCount = lines.size();
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

    // 估算行数
    if (fileSize > 0 && m_fileInfo.recordCount > 0) {
        // 用第一行的长度估算总行数
        int firstLineLen = firstLine.length();
        if (firstLineLen > 0) {
            m_fileInfo.recordCount = (quint64)(fileSize / firstLineLen);
        }
    }

    // 重置到文件开头
    m_file->seek(0);
    m_cursorPos = 0;

    qDebug() << "DataFileReader scan:" << m_fileInfo.filePath
             << "records ~" << m_fileInfo.recordCount
             << "time range:" << m_fileInfo.minTime << "-" << m_fileInfo.maxTime;
}
