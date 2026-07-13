/**
 * @file Server_DataReplay.h
 * @brief 服务端动态库入口类
 *
 * 本文件定义了 Server_DataReplay 库的入口类。
 * 所有业务逻辑（想定管理、回放引擎、NATS 通信、日志等）均在此 DLL 中实现，
 * 供 APP_DataReplay 前端程序调用。
 */

#ifndef SERVER_DATAREPLAY_H
#define SERVER_DATAREPLAY_H

#include "Server_DataReplay_global.h"

/**
 * @brief 服务端动态库入口类
 *
 * 作为 DLL 的导出入口，构造函数中可执行库级别的初始化操作。
 * 实际业务由 ScenarioMgr、ReplayEngine、Communication_NATS 等独立模块承载。
 */
class SERVER_DATAREPLAY_EXPORT Server_DataReplay
{
public:
    Server_DataReplay();
};

#endif // SERVER_DATAREPLAY_H
