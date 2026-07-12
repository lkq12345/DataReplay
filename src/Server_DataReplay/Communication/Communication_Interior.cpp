/**
 * @file Communication_Interior.cpp
 * @brief 进程内部通信机制的实现文件
 *
 * 实现消息的发送（分发）、回调注册与注销功能。
 * 发送消息时，根据主题ID查找已注册的回调队列，依次调用每个回调函数进行分发。
 * 采用深拷贝策略确保所有回调函数获取到独立的数据副本。
 */

#include "Communication_Interior.h"

Communication_Interior::Communication_Interior()
{

}

Communication_Interior::~Communication_Interior()
{

}

int Communication_Interior::sendMsgData(void *pBuf, int nSize, QString strTopicID, QString strDestaddr, unsigned short usDestPort)
{
    // 主题非空时执行消息分发
    if(!strTopicID.isEmpty())
    {
        // 深拷贝数据，确保所有回调拿到稳定的数据副本
        m_byteData = QByteArray(static_cast<const char*>(pBuf), nSize);

        // 获取该主题对应的回调队列，依次调用每个回调函数
        QQueue<platformMsgStruct> queueRegisterInterior = m_mapPLATMsgManage[strTopicID];
        foreach (platformMsgStruct _regInterMsg,queueRegisterInterior) {
            _regInterMsg.recvPLATMsgFunc(_regInterMsg.pClassPointer,
                strTopicID.toStdString().c_str(),
                m_byteData.data(), m_byteData.size());
        }
    }
    return nSize;
}

void Communication_Interior::registerRecvFunc(QString strTopicID, void *lpVoid, registerInteriorMsg recvPLATMsgFunc)
{
    // 构造平台消息结构体，保存回调函数及其所属对象指针
    platformMsgStruct _platformMsgStruct;
    _platformMsgStruct.pClassPointer = lpVoid;
    _platformMsgStruct.recvPLATMsgFunc = recvPLATMsgFunc;
    // 如果该主题还没有回调队列，则新建并插入；否则追加到已有队列末尾
    if(!m_mapPLATMsgManage.contains(strTopicID))
    {
        InteriorMsge_Queue _InteriorMsgeQueue;
        _InteriorMsgeQueue.enqueue(_platformMsgStruct);
        m_mapPLATMsgManage.insert(strTopicID,_InteriorMsgeQueue);
    }else{
        m_mapPLATMsgManage[strTopicID].enqueue(_platformMsgStruct);
    }
}

void Communication_Interior::unRegisterRecvFunc(QString strTopicID, void *lpVoid)
{
    // 主题不存在时直接返回
    if(!m_mapPLATMsgManage.contains(strTopicID))
    {
        return;
    }
    // 获取该主题回调队列的引用（非拷贝），查找并移除与 lpVoid 匹配的回调
    QQueue<platformMsgStruct>& queueRegisterInterior = m_mapPLATMsgManage[strTopicID];
    int nPos = -1;
    // 遍历查找与 lpVoid 匹配的条目
    for (int i = 0; i < queueRegisterInterior.size(); ++i) {
        if (queueRegisterInterior.at(i).pClassPointer == lpVoid) {
            nPos = i;
            break;
        }
    }
    // 找到后从队列中移除
    if (nPos != -1) {
        queueRegisterInterior.removeAt(nPos);
    }
}
