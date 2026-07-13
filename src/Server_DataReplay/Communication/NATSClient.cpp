/**
 * @file NATSClient.cpp
 * @brief NATS 客户端封装类的实现文件
 *
 * 对 NATS C 库（nats.c）进行面向对象封装的具体实现。
 * 提供连接管理、主题订阅、消息发布、资源销毁以及断线重连参数设置等功能。
 * 所有公开操作均通过 QMutex 保证线程安全。
 * 内部维护状态机，防止在未初始化或已销毁状态下执行非法操作。
 */

#include "NATSClient.h"
#include <QDebug>

NATSClient::NATSClient()
{
    // 初始化NATS库（内部使用引用计数，可安全多次调用；-1表示使用默认锁自旋计数）
    nats_Open(-1);
}

NATSClient::~NATSClient()
{
    destroy();
    // 在实例最终销毁时关闭NATS库（nats_Close 有引用计数，不会重复关闭）
    nats_Close();
}

void NATSClient::initialize(void *handler, registerNATSMsg linkNatsMsg)
{
    QMutexLocker locker(&m_mutex);

    // 保存回调处理对象和函数指针
    m_handler = handler;
    m_linkNatsMsg = linkNatsMsg;

    // 如果已有旧的options则先销毁（支持重新初始化）
    if (m_pNatsOpts) {
        natsOptions_Destroy(m_pNatsOpts);
        m_pNatsOpts = nullptr;
    }

    // 创建NATS配置选项对象
    natsStatus s = natsOptions_Create(&m_pNatsOpts);
    if (s != NATS_OK) {
        qDebug() << "Failed to create NATS options:" << natsStatus_GetText(s);
        m_state = State::Uninitialized;
        return;
    }

    m_state = State::Initialized;
}

void NATSClient::setReconnectParams(int maxReconnect, int reconnectWaitMs)
{
    QMutexLocker locker(&m_mutex);

    if (!m_pNatsOpts) {
        qDebug() << "NATS options not created, call initialize() first";
        return;
    }

    // 利用NATS客户端内置的自动重连机制，不再依赖外部定时器轮询
    natsOptions_SetMaxReconnect(m_pNatsOpts, maxReconnect);
    natsOptions_SetReconnectWait(m_pNatsOpts, reconnectWaitMs);
    // 增加随机扰动，避免多个客户端同时重连造成惊群效应
    natsOptions_SetReconnectJitter(m_pNatsOpts, 100, 500);
}

/** @brief 静态消息回调：NATS有消息到达时由库内部I/O线程触发 */
void NATSClient::onMsgFromNATS(natsConnection* nc, natsSubscription* sub, natsMsg* msg, void* closure)
{
    // 消除未使用参数编译警告
    (void)nc;
    (void)sub;

    // 通过 closure 指针恢复 NATSClient 实例，调用业务层回调
    NATSClient* pThis = static_cast<NATSClient*>(closure);
    if (pThis && pThis->m_linkNatsMsg && pThis->m_handler) {
        // natsMsg_GetData 返回 const char*，先转 const void* 再 const_cast
        const char* subject = natsMsg_GetSubject(msg);
        const void* data    = natsMsg_GetData(msg);
        int         length  = natsMsg_GetDataLength(msg);

        // 将消息转发给业务层注册的回调函数
        pThis->m_linkNatsMsg(pThis->m_handler, subject, const_cast<void*>(data), length);
    }

    // 消息对象由NATS库分配，必须手动销毁释放内存
    natsMsg_Destroy(msg);
}

bool NATSClient::connect(const char *url)
{
    QMutexLocker locker(&m_mutex);

    // 确保已经调用过 initialize()
    if (m_state != State::Initialized) {
        qDebug() << "NATS not initialized, call initialize() first";
        return false;
    }

    // 设置 NATS 服务器 URL
    natsStatus s = natsOptions_SetURL(m_pNatsOpts, url);
    if (s != NATS_OK) {
        qDebug() << "Failed to set NATS URL:" << natsStatus_GetText(s);
        return false;
    }

    // 创建 NATS 连接
    s = natsConnection_Connect(&m_pNatsConn, m_pNatsOpts);
    if (s != NATS_OK) {
        qDebug() << "Failed to connect to NATS server:" << natsStatus_GetText(s);
        return false;
    }

    m_state = State::Connected;
    return true;
}

bool NATSClient::subscribe(const char *topic)
{
    QMutexLocker locker(&m_mutex);

    // 确保已连接且主题不为空
    if (m_state != State::Connected || topic == nullptr) {
        qDebug() << "Invalid connection or topic for subscription";
        return false;
    }

    // 如果已经订阅过该主题，先取消旧订阅（避免重复订阅泄漏资源）
    QString topicKey = QString::fromUtf8(topic);
    auto it = m_subscriptions.find(topicKey);
    if (it != m_subscriptions.end()) {
        natsSubscription_Destroy(it.value());
        m_subscriptions.erase(it);
    }

    natsSubscription *sub = nullptr;
    // 订阅主题，使用静态函数 onMsgFromNATS 作为消息回调
    natsStatus s = natsConnection_Subscribe(&sub, m_pNatsConn, topic,
                                           onMsgFromNATS, this);

    if (s != NATS_OK) {
        qDebug() << "Failed to subscribe to topic:" << natsStatus_GetText(s);
        return false;
    }

    // 保存订阅对象到映射表，便于后续取消订阅和资源管理
    m_subscriptions[topicKey] = sub;
    return true;
}

bool NATSClient::publish(const char *topic, const char *buf, int nLength, bool bSyncFlush)
{
    QMutexLocker locker(&m_mutex);

    // 参数校验：确保已连接、主题和数据均有效
    if (m_state != State::Connected || topic == nullptr || buf == nullptr || nLength <= 0) {
        return false;
    }

    // 发布消息到指定主题
    natsStatus s = natsConnection_Publish(m_pNatsConn, topic, buf, nLength);
    if (s != NATS_OK) {
        qDebug() << "Failed to publish message:" << natsStatus_GetText(s);
        return false;
    }

    // Flush是同步操作，会阻塞等待服务器确认。高频场景不应每次调用，
    // 仅在需要同步确认的场合（如关闭前、关键消息）启用 bSyncFlush。
    if (bSyncFlush) {
        s = natsConnection_Flush(m_pNatsConn);
        if (s != NATS_OK) {
            qDebug() << "Failed to flush NATS connection:" << natsStatus_GetText(s);
            return false;
        }
    }

    return true;
}

bool NATSClient::unsubscribe(const char *topic)
{
    QMutexLocker locker(&m_mutex);

    if (topic == nullptr) {
        return false;
    }

    QString topicKey = QString::fromUtf8(topic);
    auto it = m_subscriptions.find(topicKey);
    if (it == m_subscriptions.end()) {
        qDebug() << "Topic not subscribed:" << topic;
        return false;
    }

    // 销毁订阅对象（natsSubscription_Destroy 内部会自动取消订阅）
    natsSubscription_Destroy(it.value());
    m_subscriptions.erase(it);
    qDebug() << "Unsubscribed from topic:" << topic;
    return true;
}

void NATSClient::unsubscribeAll()
{
    QMutexLocker locker(&m_mutex);

    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        natsSubscription_Destroy(it.value());
    }
    m_subscriptions.clear();
    qDebug() << "All subscriptions have been cancelled";
}

void NATSClient::destroy()
{
    QMutexLocker locker(&m_mutex);

    // 先销毁所有订阅对象（自动取消订阅），避免连接关闭后遗留悬空指针
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        natsSubscription_Destroy(it.value());
    }
    m_subscriptions.clear();

    // 关闭并销毁NATS连接
    if (m_pNatsConn != nullptr) {
        natsConnection_Close(m_pNatsConn);
        natsConnection_Destroy(m_pNatsConn);
        m_pNatsConn = nullptr;
    }

    // 销毁NATS配置选项
    if (m_pNatsOpts != nullptr) {
        natsOptions_Destroy(m_pNatsOpts);
        m_pNatsOpts = nullptr;
    }

    // 清空回调指针
    m_linkNatsMsg = nullptr;
    m_handler = nullptr;
    m_state = State::Destroyed;

    // 注意：这里不调用 nats_Close()，它只应在析构函数中调用一次。
    // 这样在重连场景中，destroy() + initialize() 不会导致NATS库关闭。
}
