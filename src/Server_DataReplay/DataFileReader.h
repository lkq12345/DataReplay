/**
 * @file DataFileReader.h
 * @brief 大文件游标式顺序读取器的头文件
 *
 * 定义 DataFileReader 类和 DataRecord 结构体。
 * 不加载全文到内存，通过文件游标 + 时间窗口实现增量读取。
 */

#ifndef DATAFILEREADER_H
#define DATAFILEREADER_H

#include <QObject>
#include <QFile>
#include <QDateTime>
#include <QList>
#include "Server_DataReplay_global.h"

/**
 * @brief 数据文件预扫描信息
 *
 * 存储数据文件的时间范围，供回放引擎做进度计算和结束判断。
 */
struct DataFileInfo {
    QDateTime minTime;          //!< 数据文件中的最早仿真时间（读取首行获得）
    QDateTime maxTime;          //!< 数据文件中的最晚仿真时间（读取尾部获得）
};

/**
 * @brief 单条数据记录
 */
struct DataRecord {
    QDateTime simTimestamp;   // 仿真时间（从 JSON 中提取的 simTime）
    QString    fullLine;      // 原始完整行
    QString    jsonPayload;   // JSON 部分（不含外层时间戳，用于 NATS 发送）
};

/**
 * @brief 大文件游标式顺序读取器
 *
 * 核心设计：不加载全文到内存，维护 QFile + qint64 游标位置，
 * 通过 readWindow() 按仿真时间窗口读取数据，保持文件原始行序。
 */
class SERVER_DATAREPLAY_EXPORT DataFileReader : public QObject
{
    Q_OBJECT

public:
    explicit DataFileReader(QObject *parent = nullptr);
    ~DataFileReader();

    /** @brief 打开数据文件，并预扫描文件信息 */
    bool openFile(const QString &filePath);

    /** @brief 关闭文件 */
    void close();

    /** @brief 文件是否已打开 */
    bool isOpen() const;

    /** @brief 获取预扫描的文件信息 */
    DataFileInfo fileInfo() const;

    /**
     * @brief 获取指定时间窗口 [windowStart, windowStart + stepMs) 内的数据
     * @param windowStart 窗口起始仿真时间
     * @param stepMs      仿真步长（毫秒）
     * @return 窗口内的数据记录列表（按文件原始行序排列）
     *
     * 内部维护游标位置，每次调用从上次位置继续读取。
     * 不会重复读取已处理的数据。
     */
    QList<DataRecord> readWindow(const QDateTime &windowStart, int stepMs);

    /**
     * @brief 读取文件第一条有效数据记录
     * @return 第一条数据记录（若文件为空或无有效行则返回空 DataRecord）
     *
     * 读取后游标前进到该行之后，后续 readWindow() 从第二条记录开始。
     * 调用方通过检查 jsonPayload 是否含 "CMD":"Init" 判断是否为初始化消息。
     */
    DataRecord readFirstRecord();

    /** @brief 重置到文件开头 */
    void reset();

    /** @brief 文件是否已到达末尾 */
    bool atEnd() const;

signals:
    /** @brief 读取进度变化信号，发射当前已读取的百分比 */
    void progressChanged(double percent);

private:
    /** @brief 解析一行数据，提取 JSON 部分 */
    bool parseLine(const QByteArray &rawLine, QByteArray &jsonPart);

    /** @brief 从 JSON 中提取 simTime（优先使用 formatted 字段） */
    QDateTime extractSimTime(const QByteArray &jsonData);

    /** @brief 预扫描文件信息 */
    void scanFileInfo();

    QFile      *m_file = nullptr;
    qint64      m_cursorPos = 0;        //!< 当前读取游标位置
    QDateTime   m_lastReadTimestamp;     //!< 最后读取的仿真时间（用于进度计算）
    DataFileInfo m_fileInfo;
    bool        m_infoScanned = false;
    bool        m_atEnd = false;
};

#endif // DATAFILEREADER_H
