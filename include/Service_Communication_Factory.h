/**
 * @file Service_Communication_Factory.h
 * @brief 通信工厂抽象基类定义文件
 *
 * 定义了通信工厂的抽象基类，提供消息发送的统一接口。
 * 所有具体通信方式（内部通信、NATS、HTTP等）需继承此类并实现 sendMsgData 方法。
 */

#ifndef SERVICE_COMMUNICATION_FACTORY_H
#define SERVICE_COMMUNICATION_FACTORY_H


#include "publicDefineAndStruct.h"

/**
 * @brief 通信工厂抽象基类，定义消息发送的统一接口
 *
 * 所有具体通信方式（内部通信、NATS、HTTP等）均需继承此类并实现 sendMsgData 方法
 */
class Service_Communication_Factory
{

public:
    /** @brief 构造函数 */
    Service_Communication_Factory(){};
    /** @brief 虚析构函数 */
    virtual ~Service_Communication_Factory(){};

    /**
      * @brief 发送消息数据（纯虚函数）
      * @param pBuf 数据缓冲区指针
      * @param nSize 数据长度
      * @param strTopicID 消息主题ID
      * @param strDestaddr 目标IP地址
      * @param usDestPort 目标端口号
      * @return 成功返回非负值，失败返回 -1
     */
    virtual int sendMsgData(void* pBuf,int nSize,QString strTopicID="",QString strDestaddr="",unsigned short usDestPort=0)=0;
};
#endif // SERVICE_COMMUNICATION_FACTORY_H
