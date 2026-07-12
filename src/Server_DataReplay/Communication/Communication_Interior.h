/**
 * @file Communication_Interior.h
 * @brief 进程内部通信机制的头文件
 *
 * 本文件定义了 Communication_Interior 类，实现进程内消息的注册、注销与分发功能。
 * 通过回调函数队列的方式，支持同一主题下的多订阅者模式。
 * 继承自 QObject 与 Service_Communication_Factory 接口。
 */

#ifndef COMMUNICATION_INTERIOR_H
#define COMMUNICATION_INTERIOR_H

#include <QObject>
#include <QQueue>
#include "Service_Communication_Factory.h"

/** @brief 平台消息结构体，保存回调函数及其所属对象指针 */
struct platformMsgStruct
{
    registerInteriorMsg     recvPLATMsgFunc;        //!< 回调函数指针
    void*                   pClassPointer;          //!< 回调函数所属类实例指针
};

/** @brief 回调消息队列：按注册顺序存储同一主题下的多个回调 */
typedef  QQueue<platformMsgStruct>              InteriorMsge_Queue;
/** @brief 内部通信管理映射表：Key为主题ID，Value为对应的回调消息队列 */
typedef  QMap<QString,InteriorMsge_Queue>       ComInterior_Map;

/** @brief 进程内部通信类
 *
 *  继承自 QObject 与 Service_Communication_Factory，
 *  实现进程内消息的订阅与分发机制。
 *  发送者调用 sendMsgData 后，根据主题ID查找注册的回调队列，
 *  逐一调用回调函数进行消息分发。
 */
class Communication_Interior:public QObject,public Service_Communication_Factory
{
    Q_OBJECT
public:
    Communication_Interior();
    ~Communication_Interior();

    /**
     * @brief 发送内部消息，通过回调队列分发给所有订阅者
     * @param pBuf        数据缓冲区指针
     * @param nSize       数据长度
     * @param strTopicID  消息主题标识
     * @param strDestaddr 目标地址（内部通信中未使用，保留以兼容接口）
     * @param usDestPort  目标端口（内部通信中未使用，保留以兼容接口）
     * @return int 0 表示成功，-1 表示失败
     */
    int sendMsgData(void* pBuf,int nSize,QString strTopicID="",QString strDestaddr="",unsigned short usDestPort=0);
    /**
     * @brief 注册指定主题的回调函数
     * @param strTopicID      待订阅的消息主题
     * @param lpVoid          接收者对象指针，用于注销时匹配
     * @param recvPLATMsgFunc 回调函数指针，收到消息时被调用
     */
    void registerRecvFunc(QString strTopicID,void* lpVoid,registerInteriorMsg recvPLATMsgFunc);
    /**
     * @brief 注销指定主题的回调函数
     * @param strTopicID 待取消订阅的消息主题
     * @param lpVoid     接收者对象指针，与注册时传入的指针匹配
     */
    void unRegisterRecvFunc(QString strTopicID,void* lpVoid);
private:
    ComInterior_Map     m_mapPLATMsgManage;         //!< 内部消息回调管理映射表，Key=主题ID，Value=回调队列
    QByteArray          m_byteData;                 //!< 待发送的消息数据缓冲区
};

#endif // COMMUNICATION_INTERIOR_H
