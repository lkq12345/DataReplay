/**
 * @file NATSClient.h
 * @brief NATS 客户端封装类的头文件
 *
 * 本文件定义了 NATSClient 类，对 NATS C 库（nats.c）进行面向对象的封装，
 * 提供连接管理、主题订阅、消息发布、资源销毁以及断线重连参数设置等功能。
 * 同时定义了重连相关的全局常量（最大间隔、基础间隔、最大尝试次数）。
 */

#ifndef NATSCLIENT_H
#define NATSCLIENT_H

#include "NATS/nats/nats.h"
#include "publicDefineAndStruct.h"
#include <QMutex>

/**
 * @brief NATS 客户端封装类
 *
 * 对 NATS C 库（nats.c）进行面向对象封装，提供以下核心功能：
 * - initialize：初始化客户端并注册消息回调
 * - connect：连接到 NATS 服务器
 * - subscribe：订阅指定主题
 * - publish：向指定主题发布消息
 * - destroy：销毁连接并释放资源
 * - setReconnectParams：设置 NATS 内置自动重连参数
 *
 * 内部维护连接状态机（Uninitialized -> Initialized -> Connected -> Destroyed），
 * 并通过 QMutex 保证多线程环境下的操作安全。
 */
class NATSClient
{
public:
    NATSClient();
    virtual ~NATSClient();

    /**
     * @brief 初始化 NATS 客户端，设置消息回调处理对象和函数
     * @param handler      消息处理对象指针，回调时透传给回调函数
     * @param linkNatsMsg  NATS 消息回调函数指针
     */
    void initialize(void* handler, registerNATSMsg linkNatsMsg);

    /**
     * @brief 连接到 NATS 服务器
     * @param url NATS 服务器 URL，格式如 "nats://localhost:4222"
     * @return true 连接成功，false 连接失败
     */
    bool connect(const char* url);

    /**
     * @brief 订阅指定主题
     * @param topic 主题名称
     * @return true 订阅成功，false 订阅失败
     */
    bool subscribe(const char* topic);

    /** @brief 销毁 NATS 连接，释放所有相关资源 */
    void destroy();

    /**
     * @brief 发布消息到指定主题
     * @param topic      目标主题
     * @param buf        消息数据缓冲区
     * @param nLength    消息数据长度
     * @param bSyncFlush 是否同步等待 Flush 确认（高吞吐场景建议设为 false）
     * @return true 发布成功，false 发布失败
     */
    virtual bool publish(const char* topic, const char* buf, int nLength, bool bSyncFlush = false);

    /**
     * @brief 设置 NATS 内置自动重连参数（需在 connect 前调用）
     * @param maxReconnect    最大重连次数（-1 表示无限重连）
     * @param reconnectWaitMs 重连间隔时间（毫秒）
     */
    void setReconnectParams(int maxReconnect = -1, int reconnectWaitMs = 2000);

    /**
     * @brief NATS 消息回调函数（静态）
     * @param nc      NATS 连接对象
     * @param sub     订阅对象
     * @param msg     收到的消息
     * @param closure 用户自定义数据（指向 Communication_NATS 实例）
     */
    static void onMsgFromNATS(natsConnection* nc, natsSubscription* sub, natsMsg* msg, void* closure);

private:
    natsConnection* m_pNatsConn = nullptr;      //!< NATS 连接对象指针
    natsOptions*    m_pNatsOpts = nullptr;      //!< NATS 配置选项指针
    registerNATSMsg m_linkNatsMsg = nullptr;    //!< NATS 消息回调函数指针
    void*           m_handler = nullptr;        //!< 消息处理对象指针
    QMutex          m_mutex;                    //!< 操作互斥锁，保证多线程安全

    /** @brief 客户端内部状态枚举 */
    enum class State {
        Uninitialized,  //!< 未初始化
        Initialized,    //!< initialize() 已完成
        Connected,      //!< connect() 成功
        Destroyed       //!< destroy() 已完成
    };
    State m_state = State::Uninitialized;       //!< 当前状态
};

/** @brief 最大重连间隔（毫秒）*/
const int RECONNECT_MAX_INTERVAL_MS = 30000;
/** @brief 基础重连间隔（毫秒）*/
const int RECONNECT_BASE_INTERVAL_MS = 1000;
/** @brief 最大重连尝试次数 */
const int MAX_RECONNECT_ATTEMPTS = 20;

#endif // NATSCLIENT_H
