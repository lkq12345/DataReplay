#ifndef REPLAYENGINE_H
#define REPLAYENGINE_H

#include <QObject>
#include <QTimer>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QThread>
#include "Server_DataReplay_global.h"
#include "ReplayWorker.h"   // ReplayWorker、WindowResult

class Scenario;
class QEventLoop;

/**
 * @brief 回放引擎
 *
 * 实现回放状态机管理、定时器步进、倍速控制。
 *
 * 耗时的数据读取、实体ID替换与 NATS 发布已下沉到 ReplayWorker（专用工作线程），
 * 本类（主线程）仅负责状态机、定时器、进度计算与 UI 信号发射，避免大窗口卡界面。
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
     * @brief 初始化回放引擎（创建工作线程，异步打开数据文件并同步等待结果）
     * @param scenario      想定信息
     * @param selectedFile  指定的数据文件路径，为空则回退到想定的第一个数据文件
     */
    bool initialize(const Scenario *scenario, const QString &selectedFile = QString());

    /** @brief 开始回放 */
    bool start();

    /** @brief 暂停回放 */
    bool pause();

    /** @brief 继续回放 */
    bool resume();

    /** @brief 停止回放（重置到已停止状态） */
    bool stop();

    /** @brief 设置实体ID映射表（直接调用 worker 线程安全接口，回放发送前将原始ID替换为映射值） */
    void setEntityIdMapping(const QMap<QString, QString> &mapping);

    /** @brief 设置倍速（1~100，运行中实时生效） */
    void setSpeed(int speed);

    /** @brief 获取当前倍速 */
    int speed() const;

    /** @brief 获取当前状态 */
    State state() const;

    /** @brief 获取整体进度百分比（0~100） */
    double overallProgress() const;

signals:
    void stateChanged(ReplayEngine::State newState);
    void simTimeChanged(const QDateTime &simTime);
    void progressChanged(double percent);
    void replayFinished();
    void errorOccurred(const QString &error);
    void logMessage(const QString &level, const QString &message);

private slots:
    /** @brief 定时器 tick：前置判断 + 异步派发一个窗口处理任务到工作线程 */
    void tick();

    /** @brief 工作线程窗口处理完成回调（主线程）：更新进度、判断结束、推进仿真时间 */
    void onWindowProcessed(const WindowResult &result);

    /** @brief 工作线程文件打开结果回调（主线程）：记录结果并退出 initialize() 的等待循环 */
    void onFileOpened(bool ok, const DataFileInfo &info, const QString &error);

private:
    /** @brief 更新定时器间隔（根据倍速和仿真步长） */
    void updateTimerInterval();

    /** @brief 结束回放并发射完成信号（统一的结束处理入口） */
    void finishReplay(const QDateTime &stopSimTime, const QString &reason);

    State           m_state = Idle;
    const Scenario *m_scenario = nullptr;

    // ---- 工作线程（承载 ReplayWorker） ----
    QThread       *m_workerThread = nullptr;   //!< 专用工作线程
    ReplayWorker  *m_worker = nullptr;         //!< 工作对象（moveToThread 到 m_workerThread）

    QTimer         *m_timer = nullptr;
    QDateTime       m_currentSimTime;
    QDateTime       m_windowStart;     //!< 当前窗口起始时间（数据已读到此位置）
    QDateTime       m_maxDataSimTime;  //!< 已发送数据中最大的 simTime
    QDateTime       m_dataStartTime;   //!< 数据文件的最小 simTime（用于初始化和进度计算）
    QDateTime       m_dataEndTime;     //!< 数据文件的最大 simTime（用于进度和结束判断）
    int             m_speed = 1;
    bool            m_processing = false;  //!< 工作线程是否正在处理某个窗口（背压，避免并发读文件）

    // ---- initialize() 期间等待 worker fileOpened 的临时状态 ----
    bool            m_openOk = false;      //!< 文件打开是否成功
    DataFileInfo    m_openInfo;            //!< 文件打开成功时的时间范围
    QString         m_openError;           //!< 文件打开失败时的错误信息
    QEventLoop     *m_openLoop = nullptr;  //!< 指向 initialize() 中的局部事件循环
};

#endif // REPLAYENGINE_H
