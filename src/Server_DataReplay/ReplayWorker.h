/**
 * @file ReplayWorker.h
 * @brief 回放工作线程对象声明
 *
 * ReplayWorker 运行在专用的 QThread 中，承担回放过程中最耗时的部分：
 *   - 打开数据文件 + 预扫描时间范围 + 读取并发送首条 Init 记录
 *   - 按时间窗口读取数据（DataFileReader::readWindow）
 *   - 实体ID映射替换（正则扫描 + QMap 查找）
 *   - NATS 消息发布
 *
 * 这样做的目的：把"读文件 + 数据处理 + 网络发布"从 GUI 线程剥离，
 * 避免大窗口（单步长内数据量很大）时主界面卡死，满足"界面响应流畅"的需求。
 *
 * ReplayEngine 主线程仅负责状态机、定时器、进度计算和 UI 信号发射，
 * 通过跨线程信号/槽（QueuedConnection）与 ReplayWorker 协作。
 *
 * 线程安全说明：
 *   - 所有公开槽方法均在 worker 线程（事件循环）中执行；
 *   - m_epoch 为原子代次号，供主线程在 stop()/initialize() 时同步失效
 *     队列中尚未执行的旧窗口任务，避免"停止后仍发送旧数据"。
 */

#ifndef REPLAYWORKER_H
#define REPLAYWORKER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QMap>
#include <QHash>
#include <QAtomicInteger>
#include <QMetaType>
#include <QMutex>

#include "DataFileReader.h"   // DataFileInfo、DataFileReader、DataRecord
#include "publicDefineAndStruct.h"   // EntityState

/**
 * @brief 单个时间窗口的处理结果（由 worker 线程回传给主线程）
 */
struct WindowResult {
    quint64    epoch = 0;         //!< 代次号，用于丢弃停止/重初始化前的过期结果
    bool       valid = false;     //!< 本次处理是否有效（reader 未打开等异常时为 false）
    QDateTime  windowStart;       //!< 本窗口起始仿真时间（回显，主线程据此推进）
    int        recordCount = 0;   //!< 本窗口实际发送的记录数
    bool       atEnd = false;     //!< 读取后文件是否已到末尾
    QDateTime  lastSimTime;       //!< 本窗口最后一条记录的仿真时间（用于推进 m_maxDataSimTime）
};

Q_DECLARE_METATYPE(WindowResult)

/**
 * @brief 回放工作线程对象
 *
 * 通过 moveToThread 移入专用 QThread 后，其槽方法均在 worker 线程执行。
 */
class ReplayWorker : public QObject
{
    Q_OBJECT

public:
    explicit ReplayWorker(QObject *parent = nullptr);
    ~ReplayWorker();

    // ---- 代次管理（供主线程跨线程直接调用，内部为原子操作，线程安全） ----

    /** @brief 递增代次号，使队列中尚未执行的旧窗口任务失效 */
    void bumpEpoch() { m_epoch.fetchAndAddRelaxed(1); }

    /** @brief 读取当前代次号 */
    quint64 epoch() const { return m_epoch.loadAcquire(); }

    /**
     * @brief 当前跟踪/显示的属性名列表（唯一配置点）
     *
     * 后期要增减显示的属性（如加 speed/heading），只需修改 m_trackedAttributes，
     * XML 解析、数据解析、表格列、增量更新均自动适配。
     */
    QStringList trackedAttributes() const { return m_trackedAttributes; }

    /**
     * @brief 设置实体ID映射表（线程安全，供主线程直接调用）
     *
     * 通过 QMutex 保护，主线程与 worker 线程可并发安全访问。
     * 主线程在 initialize() 前调用，立即（同步）生效。
     */
    void setEntityIdMapping(const QMap<QString, QString> &mapping);

public slots:
    /** @brief 打开数据文件：预扫描 + 读取并发送首条 Init 记录（worker 线程执行） */
    void openFile(const QString &filePath, quint64 epoch);

    /** @brief 处理一个时间窗口：读窗口 + 映射替换 + NATS 发布（worker 线程执行） */
    void processWindow(const QDateTime &windowStart, int stepMs, quint64 epoch);

    /** @brief 重置读取游标到文件开头（worker 线程执行） */
    void reset();

    /** @brief 关闭并释放数据文件（worker 线程执行） */
    void close();

signals:
    /** @brief 文件打开结果（携带预扫描到的时间范围） */
    void fileOpened(bool ok, const DataFileInfo &info, const QString &error);

    /** @brief 单个窗口处理完成，回传处理结果 */
    void windowProcessed(const WindowResult &result);

    /** @brief 实体状态更新（每窗口一次，只含位置/属性发生变化的实体，worker 线程发射） */
    void entityStatesUpdated(const QList<EntityState> &changed);

    /** @brief worker 内部日志（转发给 ReplayEngine，最终进入 LogService） */
    void logMessage(const QString &level, const QString &message);

private:
    /** @brief 获取映射表快照（加锁拷贝一次，供窗口处理循环使用，避免逐条加锁） */
    QMap<QString, QString> mappingSnapshot() const;

    /** @brief 对 payload 中所有匹配的实体ID模式执行映射替换（使用传入的映射快照） */
    void applyEntityIdMapping(const QMap<QString, QString> &mapping, QString &payload);

    /** @brief 解析单条数据的实体状态（entity + 跟踪属性），更新状态表并收集变化 */
    void parseEntityState(const QString &jsonPayload, QList<EntityState> &changedList);

    DataFileReader *m_reader = nullptr;       //!< 数据文件读取器（worker 线程创建与使用）
    mutable QMutex   m_mappingMutex;          //!< 保护 m_mapping 的互斥锁（主线程/worker 线程共享）
    QMap<QString, QString> m_mapping;         //!< 实体ID映射表（原始ID → 映射ID）
    QAtomicInteger<quint64> m_epoch;          //!< 原子代次号（0 初始）

    // ---- 实体状态（worker 线程内维护） ----
    QStringList m_trackedAttributes = { "x", "y", "z" };   //!< 跟踪/显示的属性名（唯一配置点）
    QHash<QString, EntityState> m_entityStates;            //!< 实体ID → 运行时状态
};

#endif // REPLAYWORKER_H
