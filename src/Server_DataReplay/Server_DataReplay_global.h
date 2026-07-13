/**
 * @file Server_DataReplay_global.h
 * @brief DLL 导出/导入宏定义
 *
 * 根据当前编译目标（DLL 自身 or 使用者）自动切换 __declspec(dllexport) / __declspec(dllimport)。
 * SERVER_DATAREPLAY_LIBRARY 宏在 Server_DataReplay.pro 中通过 DEFINES 定义。
 */

#ifndef SERVER_DATAREPLAY_GLOBAL_H
#define SERVER_DATAREPLAY_GLOBAL_H

#include <QtCore/qglobal.h>

#if defined(SERVER_DATAREPLAY_LIBRARY)
#  define SERVER_DATAREPLAY_EXPORT Q_DECL_EXPORT   // 编译 DLL 时导出符号
#else
#  define SERVER_DATAREPLAY_EXPORT Q_DECL_IMPORT   // 链接 DLL 时导入符号
#endif

#endif // SERVER_DATAREPLAY_GLOBAL_H
