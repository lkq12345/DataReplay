#ifndef REPLAYENGINE_H
#define REPLAYENGINE_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QMap>
#include "Server_DataReplay_global.h"

class Scenario;
class DataFileReader;
struct DataRecord;

/**
 * @brief 回放引擎
 *
 * 实现回放状态机管理、定时器步进、倍速控制、跨文件数据合并与 NATS 发送。
 *
 * 状态机：
 *   Idle → initialize() → Ready → start() → Playing → pause() → Paused
 *   Paused → resume() → Playing
 *   Playing/Paused → stop() → Stopped
 *   Stopped → initialize() → Ready
 */
class SERVER_DATAREPLAY_EXPORT ReplayEngine : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 回放状态枚举
     */
    enum State {
        Idle,       //!< 未初始化（初始状态）
        Ready,      //!< 已就绪（想定已加载、NATS 已连接）
        Playing,    //!< 回放中
        Paused,     //!< 已暂停
        Stopped     //!< 已停止
    };
    Q_ENUM(State)

    explicit ReplayEngine(QObject *parent = nullptr);
    ~ReplayEngine();

    /**
     * @brief 初始化回放引擎（创建 DataFileReader，连接 NATS）
     * @param scenario      想定信息
     * @param selectedFiles 指定的数据文件列表，为空则使用想定下所有文件
     */
    bool initialize(const Scenario *scenario, const QStringList &selectedFiles = QStringList());

    /** @brief 开始回放 */
    bool start();

    /** @brief 暂停回放 */
    bool pause();

    /** @brief 继续回放 */
    bool resume();

    /** @brief 停止回放（重置到已就绪状态） */
    bool stop();

    /** @brief 设置实体ID映射表（回放发送前将原始ID替换为映射值） */
    void setEntityIdMapping(const QMap<QString, QString> &mapping);

    /** @brief 设置倍速（1~100，运行中实时生效） */
    void setSpeed(int speed);

    /** @brief 获取当前倍速 */
    int speed() const;

    /** @brief 获取当前状态 */
    State state() const;

    /** @brief 获取当前仿真时间 */
    QDateTime currentSimTime() const;

    /** @brief 获取整体进度百分比（0~100） */
    double overallProgress() const;

    /** @brief 获取状态名称 */
    static QString stateName(State state);

signals:
    void stateChanged(ReplayEngine::State newState);
    void simTimeChanged(const QDateTime &simTime);
    void progressChanged(double percent);
    void replayFinished();
    void errorOccurred(const QString &error);
    void dataSent(int count);
    void logMessage(const QString &level, const QString &message);

private slots:
    /** @brief 定时器 tick，执行一个仿真步长的数据读取和发送 */
    void tick();

private:
    /** @brief 更新定时器间隔（根据倍速和仿真步长） */
    void updateTimerInterval();

    State           m_state = Idle;
    const Scenario *m_scenario = nullptr;
    QList<DataFileReader *> m_readers;
    QTimer         *m_timer = nullptr;
    QDateTime       m_currentSimTime;
    QDateTime       m_windowStart;     //!< 当前窗口起始时间（数据已读到此位置）
    QDateTime       m_maxDataSimTime;  //!< 已发送数据中最大的 simTime
    QDateTime       m_dataEndTime;     //!< 所有数据文件的最大 simTime（用于进度和结束判断）
    int             m_speed = 1;
    bool            m_allReadersAtEnd = false;
    QMap<QString, QString> m_entityIdMapping;   //!< 实体ID映射表（原始ID → 映射ID）
};

#endif // REPLAYENGINE_H
