#ifndef LOGSERVICE_H
#define LOGSERVICE_H

#include <QObject>
#include <QFile>
#include <QString>
#include <QMutex>
#include <QTextStream>
#include "Server_DataReplay_global.h"

/**
 * @brief 日志服务（单例）
 *
 * 提供日志收集、信号发射（供前端显示）和文件写入功能。
 * 日志格式: [yyyy-MM-dd HH:mm:ss] [LEVEL] 消息内容
 */
class SERVER_DATAREPLAY_EXPORT LogService : public QObject
{
    Q_OBJECT

public:
    /** @brief 获取单例实例 */
    static LogService &instance();

    /**
     * @brief 写入日志
     * @param level   日志级别（INFO, WARN, ERROR, DEBUG）
     * @param message 日志消息
     */
    void log(const QString &level, const QString &message);

    /** @brief 设置日志文件目录 */
    void setLogFile(const QString &dirPath);

    /** @brief 获取当前日志文件路径 */
    QString logFilePath() const;

signals:
    /** @brief 新日志信号（供前端 QTextEdit 显示） */
    void newLog(const QString &formattedMessage);

private:
    LogService();
    ~LogService();

    LogService(const LogService &) = delete;
    LogService &operator=(const LogService &) = delete;

    /** @brief 格式化日志消息 */
    QString formatMessage(const QString &level, const QString &message);

    /** @brief 写入日志文件 */
    void writeToFile(const QString &formatted);

    QFile       *m_logFile = nullptr;
    QTextStream *m_logStream = nullptr;  //!< 复用 QTextStream，避免每次写日志重新构造
    QString      m_logDir;
    mutable QMutex m_mutex;               //!< mutable 允许 const 方法中加锁
};

#endif // LOGSERVICE_H
