/**
 * @file LogService.cpp
 * @brief 日志服务的实现文件
 *
 * 实现单例日志服务：
 * - 格式化日志输出（带时间戳和级别前缀）
 * - 通过 Qt 信号将日志推送到前端 QTextEdit 实时显示
 * - 以追加模式写入按日期命名的日志文件
 * - 所有写入操作通过 QMutex 保证线程安全
 */

#include "LogService.h"

#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QTextStream>

LogService &LogService::instance()
{
    // C++11 起局部静态变量初始化线程安全，无需双重检查锁定
    static LogService s_instance;
    return s_instance;
}

LogService::LogService()
    : QObject(nullptr)
{
    // 默认日志目录：可执行文件同级或工作目录下的 logs/
    setLogFile(QDir::currentPath() + "/logs");
}

LogService::~LogService()
{
    if (m_logFile) {
        if (m_logFile->isOpen()) {
            m_logFile->close();
        }
        delete m_logFile;
        m_logFile = nullptr;
    }
}

void LogService::log(const QString &level, const QString &message)
{
    // 加锁保护：emit 信号和文件写入均需线程安全
    QMutexLocker locker(&m_mutex);

    QString formatted = formatMessage(level, message);

    // ① 发射信号 → 前端 DataReplayWidget::appendLog() 实时显示
    emit newLog(formatted);

    // ② 写入磁盘日志文件（追加模式，按日期滚动）
    writeToFile(formatted);

    // ③ 同步输出到调试控制台（Qt Creator / 命令行可见）
    qDebug().noquote() << formatted;
}

void LogService::setLogFile(const QString &dirPath)
{
    m_logDir = dirPath;

    // 关闭旧文件（如有），避免同时持有多个文件句柄
    if (m_logFile) {
        if (m_logFile->isOpen()) {
            m_logFile->close();
        }
        delete m_logFile;
        m_logFile = nullptr;
    }

    // 确保日志目录存在（递归创建）
    QDir dir(m_logDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 以当前日期创建日志文件，每天自动滚动
    // 格式：logs/2026-07-13.log
    QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString filePath = m_logDir + "/" + dateStr + ".log";

    m_logFile = new QFile(filePath);
    if (!m_logFile->open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "LogService: Cannot open log file:" << filePath;
        m_fileError = true;
        delete m_logFile;
        m_logFile = nullptr;
    } else {
        m_fileError = false;
    }
}

QString LogService::logFilePath() const
{
    if (m_logFile) {
        return m_logFile->fileName();
    }
    return QString();
}

QString LogService::formatMessage(const QString &level, const QString &message)
{
    // 统一格式：[yyyy-MM-dd HH:mm:ss] [LEVEL] 消息内容
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    return QString("[%1] [%2] %3").arg(timestamp, level, message);
}

void LogService::writeToFile(const QString &formatted)
{
    if (!m_logFile || !m_logFile->isOpen()) {
        return;
    }

    // QTextStream 自动处理编码（UTF-8），追加写入后立即 flush 防止丢失
    QTextStream stream(m_logFile);
    stream << formatted << "\n";
    stream.flush();
}
