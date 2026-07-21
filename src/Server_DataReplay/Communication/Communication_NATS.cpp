/**
 * @file Communication_NATS.cpp
 * @brief NATS 通信类的实现文件
 *
 * 实现基于 NATS 消息中间件的网络通信功能：
 * - 单例模式管理全局 NATS 通信实例
 * - 从 XML 配置文件读取服务器 IP 和订阅主题
 * - 消息的发布（sendMsgData）与接收回调处理
 * - 断线重连机制（指数退避策略）
 * - NATS 消息桥接到内部通信（Communication_Interior）
 */

#include "Communication_NATS.h"
#include <QDebug>
#include <QFile>
#include "Communication_Interior.h"

Communication_NATS& Communication_NATS::getInstance()
{
    // 局部静态变量实现懒汉式单例（C++11 起线程安全）
    static Communication_NATS instance;
    return instance;
}

Communication_NATS::Communication_NATS()
{
    // 初始化成员变量
    m_pInteriorCommunication = nullptr;
    m_NATSClient = new NATSClient();
    // 创建单次触发的重连定时器
    m_pReconnectTimer = new QTimer(this);
    m_pReconnectTimer->setSingleShot(true);
    connect(m_pReconnectTimer, &QTimer::timeout, this, &Communication_NATS::attemptReconnect);
    m_nReconnectAttempts = 0;

    // 读取配置（initNATSConnect 由外部调用者触发，构造函数中仅读取配置不发起连接）
    readIPAndTopicConfig();
}

Communication_NATS::~Communication_NATS()
{
    // 停止重连定时器
    if (m_pReconnectTimer->isActive()) {
        m_pReconnectTimer->stop();
    }
    // 销毁NATS客户端
    if (m_NATSClient) {
        m_NATSClient->destroy();
        delete m_NATSClient;
        m_NATSClient = nullptr;
    }
}

int Communication_NATS::sendMsgData(void *pBuf, int nSize, QString strTopicID, QString strDestaddr, unsigned short usDestPort)
{
    // 参数校验：缓冲区、数据长度、主题、客户端均需有效
    if (!pBuf || nSize <= 0 || strTopicID.isEmpty() || !m_NATSClient) {
        return -1;
    }

    // 将临时QString转为std::string，避免c_str()悬空指针
    const std::string topic = strTopicID.toStdString();

    // 通过NATS客户端发布消息（高频场景不等待Flush确认）
    bool success = m_NATSClient->publish(topic.c_str(),
                                        static_cast<const char*>(pBuf),
                                        nSize);
    return success ? nSize : -1;
}

void Communication_NATS::initNATSConnect()
{
    // 已经成功初始化过（含连接+订阅），避免重复初始化导致连接泄漏
    if (m_bNATSInitialized) {
        qDebug() << "NATS already initialized, skipping";
        return;
    }

    // NATS客户端未初始化时直接返回
    if (!m_NATSClient) {
        qDebug() << "NATSClient not initialized";
        return;
    }

    // 停止可能正在运行的重连定时器（全新开始连接流程）
    if (m_pReconnectTimer->isActive()) {
        m_pReconnectTimer->stop();
    }

    // 构建NATS服务器URL，默认使用 localhost:4222
    const std::string natsUrl = "nats://" +
        (m_NatsServerIP.isEmpty() ? "localhost" : m_NatsServerIP.toStdString()) + ":4222";

    // 初始化NATS客户端并设置消息回调
    m_NATSClient->initialize(this, handleFuncPointerToPlatform);

    // 设置NATS内置自动重连参数（连接成功后断线将自动重连，无需外部定时器轮询）
    m_NATSClient->setReconnectParams(-1 /* 无限重连 */, 2000 /* 2秒间隔 */);

    // 尝试连接NATS服务器
    if (!m_NATSClient->connect(natsUrl.c_str())) {
        qDebug() << "Failed to connect to NATS server at:" << natsUrl.c_str();
        // 连接失败，启动重连流程
        m_nReconnectAttempts = 0;
        attemptReconnect();
        return;
    }

    // 连接成功，重设计数
    m_nReconnectAttempts = 0;
    qDebug() << "NATS connected to:" << natsUrl.c_str();

    // 订阅配置文件中所有主题
    for (const QString& topic : m_strListTopic) {
        if (!topic.isEmpty()) {
            const std::string topicStr = topic.toStdString();
            if (!m_NATSClient->subscribe(topicStr.c_str())) {
                qDebug() << "Failed to subscribe to topic:" << topic;
            } else {
                qDebug() << "Successfully subscribed to topic:" << topic;
            }
        }
    }

    // 标记初始化完成，防止后续重复初始化
    m_bNATSInitialized = true;
    emit natsConnected(true);
}

void Communication_NATS::readIPAndTopicConfig()
{
    // 从配置文件路径加载NATS配置
    QString natsConfig = NATSCONFIG;

    QFile file(natsConfig);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to load file:" << natsConfig;
        return;
    }

    QXmlStreamReader xml(&file);

    // 解析XML配置文件，提取NATS服务器IP和订阅主题列表
    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        // 读取AttrIP节点中的IP属性
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("AttrIP")) {
                QXmlStreamAttributes attributes = xml.attributes();
                if (attributes.hasAttribute("IP")) {
                    m_NatsServerIP = attributes.value("IP").toString();
                    qDebug() << "NATS Server IP:" << m_NatsServerIP;
                }
            }
            // 读取AttrTOPIC节点：区分订阅主题和发布主题
            else if (xml.name() == QLatin1String("AttrTOPIC")) {
                QXmlStreamAttributes attributes = xml.attributes();
                if (attributes.hasAttribute("SUBSCRIBETOPIC")) {
                    QString topic = attributes.value("SUBSCRIBETOPIC").toString();
                    if (!topic.isEmpty()) {
                        m_strListTopic.append(topic);
                    }
                }
                if (attributes.hasAttribute("PUBLISHTOPIC")) {
                    QString topic = attributes.value("PUBLISHTOPIC").toString();
                    if (!topic.isEmpty()) {
                        m_strListPublishTopic.append(topic);
                    }
                }
            }
        }
    }

    if (xml.hasError()) {
        qDebug() << "XML parse error:" << xml.errorString();
    }

    file.close();
}

void Communication_NATS::handleFuncPointerToPlatform(void *pPointer, const char *subject, void *msg, unsigned int unlength)
{
    // 静态回调：将消息转发给对应的 Communication_NATS 实例处理
    if (pPointer) {
        Communication_NATS* pThis = static_cast<Communication_NATS*>(pPointer);
        pThis->onMsgHandleToPlatform(subject, msg, unlength);
    }
}

void Communication_NATS::onMsgHandleToPlatform(const char *subject, void *msg, unsigned int unlength)
{
    // 将接收到的NATS消息通过内部通信分发给平台内部订阅者
    if (subject && msg && unlength > 0) {
        QString topic = QString::fromUtf8(subject);
        QByteArray data(static_cast<const char*>(msg), unlength);

        // 通过内部通信对象将消息分发给平台内部的订阅者
        if (m_pInteriorCommunication) {
            m_pInteriorCommunication->sendMsgData(msg, unlength, topic);
        }

        // 发射信号供UI层订阅更新
        emit messageReceived(topic, data);
    }
}

void Communication_NATS::setInteriorCommunication(Communication_Interior* pInterior)
{
    m_pInteriorCommunication = pInterior;
}

void Communication_NATS::attemptReconnect()
{
    // 已达最大重连次数时停止重连
    if (m_nReconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        qDebug() << "NATS 初始连接重试已达最大次数，停止重连";
        return;
    }

    m_nReconnectAttempts++;
    qDebug() << "NATS 初始连接尝试第" << m_nReconnectAttempts << "次...";

    // 销毁旧连接状态并重新初始化
    // 注意：destroy() 不调用 nats_Close()，避免NATS库被错误关闭
    m_NATSClient->destroy();
    m_NATSClient->initialize(this, handleFuncPointerToPlatform);

    // 重新连接NATS服务器
    const std::string natsUrl = "nats://" +
        (m_NatsServerIP.isEmpty() ? "localhost" : m_NatsServerIP.toStdString()) + ":4222";

    // 重连成功时重新订阅所有主题
    if (m_NATSClient->connect(natsUrl.c_str())) {
        qDebug() << "NATS 初始连接成功！";
        m_nReconnectAttempts = 0;
        for (const QString& topic : m_strListTopic) {
            if (!topic.isEmpty()) {
                m_NATSClient->subscribe(topic.toStdString().c_str());
            }
        }
        // 标记初始化完成，通知 UI 层 NATS 已就绪
        m_bNATSInitialized = true;
        emit natsConnected(true);
        return;
    }

    // 使用指数退避策略计算下次重连间隔
    int base = RECONNECT_BASE_INTERVAL_MS * (1 << qMin(m_nReconnectAttempts, 10));
    int interval = qMin(base, RECONNECT_MAX_INTERVAL_MS);
    qDebug() << "NATS 连接失败，" << interval / 1000.0 << "秒后重试";
    m_pReconnectTimer->start(interval);
}
