/**
 * @file Communication_NATS.h
 * @brief NATS 通信类的头文件
 *
 * 本文件定义了 Communication_NATS 类，提供基于 NATS 消息中间件的网络通信能力。
 * 支持消息发送、主题订阅、XML 配置加载、断线重连以及 NATS 消息桥接到进程内部通信等功能。
 * 采用单例模式，确保全局只有一个 NATS 通信实例。
 */

#ifndef COMMUNICATION_NATS_H
#define COMMUNICATION_NATS_H

#include "NATS/nats/nats.h"
#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QXmlStreamReader>
#include "Server_DataReplay_global.h"
#include "Service_Communication_Factory.h"
#include "NATSClient.h"

/**
 * @brief NATS 通信类
 *
 * 继承自 QObject 与 Service_Communication_Factory，
 * 提供基于 NATS 消息中间件的消息收发功能。
 * 支持从 XML 配置文件读取服务器地址和订阅主题，
 * 具备断线重连机制，并能将 NATS 消息桥接到进程内部通信系统。
 */
class SERVER_DATAREPLAY_EXPORT Communication_NATS : public QObject, public Service_Communication_Factory
{
    Q_OBJECT
public:
    /** @brief 获取单例实例（线程安全） */
    static Communication_NATS& getInstance();
    ~Communication_NATS();

    /** @brief 禁用拷贝构造 */
    Communication_NATS(const Communication_NATS&) = delete;
    /** @brief 禁用拷贝赋值 */
    Communication_NATS& operator=(const Communication_NATS&) = delete;

    /**
     * @brief 通过 NATS 发送消息
     * @param pBuf        数据缓冲区指针
     * @param nSize       数据长度
     * @param strTopicID  消息主题
     * @param strDestaddr 目标 IP 地址
     * @param usDestPort  目标端口
     * @return int 发送结果，0 表示成功
     */
    int sendMsgData(void* pBuf, int nSize, QString strTopicID = "", QString strDestaddr = "", unsigned short usDestPort = 0);
    /** @brief 初始化 NATS 连接，连接到配置文件中指定的 NATS 服务器 */
    void initNATSConnect();
    /** @brief 从 XML 配置文件读取 NATS 服务器 IP 地址和需要订阅的主题列表 */
    void readIPAndTopicConfig();
    /**
     * @brief 静态回调函数，将 NATS 接收到的消息桥接到平台内部通信
     * @param pPointer  指向 Communication_NATS 类实例的指针
     * @param subject   消息主题
     * @param msg       消息数据指针
     * @param unlength  消息数据长度
     */
    static void handleFuncPointerToPlatform(void* pPointer, const char* subject, void* msg, unsigned int unlength);
    /**
     * @brief 处理接收到的 NATS 消息，转发到内部通信系统
     * @param subject  消息主题
     * @param msg      消息数据指针
     * @param unlength 消息数据长度
     */
    void onMsgHandleToPlatform(const char* subject, void* msg, unsigned int unlength);

    /** @brief 查询 NATS 是否已成功连接并完成订阅 */
    bool isConnected() const { return m_bNATSInitialized; }

    /** @brief 获取发布主题列表（从配置文件读取） */
    QStringList publishTopics() const { return m_strListPublishTopic; }

signals:
    /**
     * @brief NATS 连接状态变化信号
     * @param connected true 表示已成功连接并完成主题订阅
     */
    void natsConnected(bool connected);

    /**
     * @brief NATS 消息到达信号（在 NATS I/O 线程发射，参数为值拷贝，跨线程安全）
     * @param topic 消息主题
     * @param data  消息数据
     */
    void messageReceived(const QString &topic, const QByteArray &data);

private:
    Communication_NATS();

private slots:
    /** @brief 定时重连 NATS 服务器（重连定时器槽函数） */
    void attemptReconnect();

private:
    NATSClient*          m_NATSClient;                  //!< NATS 客户端对象，封装底层 NATS 连接
    QString              m_NatsServerIP;                //!< NATS 服务器 IP 地址，从 XML 配置读取
    QStringList          m_strListTopic;                //!< 需要订阅的 NATS 主题列表，从 XML 配置读取
    QStringList          m_strListPublishTopic;         //!< 需要发布的 NATS 主题列表，从 XML 配置读取
    // 重连机制相关成员（仅用于初始连接失败的重试，连接成功后由 NATS 内置重连接管）
    QTimer*              m_pReconnectTimer;             //!< 重连定时器
    int                  m_nReconnectAttempts;          //!< 当前已尝试的重连次数
    bool                 m_bNATSInitialized = false;    //!< 是否已完成 NATS 初始化连接与订阅
};

#endif // COMMUNICATION_NATS_H
