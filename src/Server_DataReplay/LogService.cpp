#include "LogService.h"

#include <QDateTime>
#include <QDir>
#include <QDebug>
#include <QTextStream>

LogService &LogService::instance()
{
    static LogService s_instance;
    return s_instance;
}

LogService::LogService()
    : QObject(nullptr)
{
    // 默认日志目录：当前工作目录下的 logs/
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
    QMutexLocker locker(&m_mutex);

    QString formatted = formatMessage(level, message);

    // 发射信号（供前端显示）
    emit newLog(formatted);

    // 写入日志文件
    writeToFile(formatted);

    // 同时输出到调试控制台
    qDebug().noquote() << formatted;
}

void LogService::setLogFile(const QString &dirPath)
{
    m_logDir = dirPath;

    // 关闭旧文件
    if (m_logFile) {
        if (m_logFile->isOpen()) {
            m_logFile->close();
        }
        delete m_logFile;
        m_logFile = nullptr;
    }

    // 创建日志目录
    QDir dir(m_logDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    // 创建日志文件（以日期命名）
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
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    return QString("[%1] [%2] %3").arg(timestamp, level, message);
}

void LogService::writeToFile(const QString &formatted)
{
    if (!m_logFile || !m_logFile->isOpen()) {
        return;
    }

    QTextStream stream(m_logFile);
    stream << formatted << "\n";
    stream.flush();
}
