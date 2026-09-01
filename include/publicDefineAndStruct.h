/**
 * @file publicDefineAndStruct.h
 * @brief 公共宏定义、枚举类型与结构体声明文件
 *
 * 定义了整个仿真推演系统全局使用的宏常量、通信相关枚举类型及回调函数指针类型。
 * 包括：NATS配置路径、主题ID常量、通信方式枚举、通信结果枚举以及消息回调函数指针。
 */

#ifndef PUBLICDEFINEANDSTRUCT_H
#define PUBLICDEFINEANDSTRUCT_H
#include <QString>
#include <QMap>
#include <QDataStream>

/** @brief NATS配置文件路径 */
#define NATSCONFIG                  "../config/NATS/NatsConfig.xml"      //NATS配置文件
/** @brief A发送给B的主题 */
#define SEND_A_TO_B                    "SendToB"                           //A发送给B的主题
/** @brief B发送给A的主题 */
#define SEND_B_TO_A                    "SendToA"                           //B发送给A的主题

/** @brief 发送消息主题 */
#define SEND_DATA                   1001                           //发送消息主题
/** @brief 飞机实体数据主题 */
#define SEND_PLANE_DATA             1004                           //飞机实体数据主题
/** @brief 汽车实体数据主题 */
#define SEND_CAR_DATA               1005                           //汽车实体数据主题

/** @brief 通信方式枚举 */
enum CommunicationType
{
    Type_Interior = 0,      // 内部通信
    Type_NATS = 1,          // NATS通信
    Type_Http = 2           // HTTP通信
};

/** @brief 通信结果枚举 */
enum CommunicationResult
{
    Result_AddCommunication_Err_Exist = 0x01,       // 添加失败：通信已存在
    Result_AddCommunication_Err_Pointer = 0x02,     // 添加失败：指针不存在
    Result_AddCommunication_Success = 0x03,         // 添加成功
    Result_Message_Send_Success = 0x04,             // 消息发送成功
    Result_Message_Send_Err = 0x05,                 // 消息发送失败
    Result_Register_Success = 0x06,                 // 注册成功
    Result_Register_Err = 0x07                      // 注册失败
};

/** @brief 内部消息回调函数指针类型（函数指针，主题，数据，数据大小） */
typedef void(*registerInteriorMsg)(void*,const char *,void*,unsigned int);  //内部(函数指针，主题，数据，大小)
/** @brief NATS消息回调函数指针类型（函数指针，主题，数据，数据大小） */
typedef void(*registerNATSMsg)(void*,const char *,void*,unsigned int);  //NATS(函数指针，主题，数据，大小)

#endif // PUBLICDEFINEANDSTRUCT_H
