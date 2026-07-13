/**
 * @file main.cpp
 * @brief 应用程序入口
 *
 * 数据回放客户端软件主入口。
 * 创建 QApplication 实例，显示主窗口 DataReplayWidget，进入事件循环。
 */

#include "DataReplayWidget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    DataReplayWidget w;
    w.show();
    return a.exec();
}
