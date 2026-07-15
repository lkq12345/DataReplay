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
    // 延迟到 initialize() 调用 setLogFile() 时再创建日志文件，
    // 避免在构造阶段用错误的路径创建孤儿日志文件
}

LogService::~LogService()
{
    delete m_logStream;
    m_logStream = nullptr;
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

    // 关闭旧文件和流
    delete m_logStream;
    m_logStream = nullptr;
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
    QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    QString filePath = m_logDir + "/" + dateStr + ".log";

    m_logFile = new QFile(filePath);
    if (!m_logFile->open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "LogService: Cannot open log file:" << filePath;
        delete m_logFile;
        m_logFile = nullptr;
        return;
    }

    // 创建复用的 QTextStream（UTF-8 编码）
    m_logStream = new QTextStream(m_logFile);
}

QString LogService::logFilePath() const
{
    QMutexLocker locker(&m_mutex);
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
    if (!m_logStream || !m_logFile || !m_logFile->isOpen()) {
        return;
    }

    // 复用成员 QTextStream，追加写入后立即 flush 防止丢失
    (*m_logStream) << formatted << "\n";
    m_logStream->flush();
}
