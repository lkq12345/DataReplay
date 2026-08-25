QT -= gui

TEMPLATE = lib
DEFINES += SERVER_DATAREPLAY_LIBRARY

CONFIG += c++11
INCLUDEPATH += $$PWD/../../include

# DLL 与 APP 输出到同一目录
DESTDIR = $$PWD/../../bin

# ==================== 编译中间文件输出到 temp 目录 ====================
# .o / moc / uic 等中间产物统一放到项目根 temp/ 下（已被 .gitignore 忽略），
# 不在源码目录生成 debug/、release/ 子目录。
CONFIG(debug, debug|release) {
    OBJECTS_DIR = $$PWD/../../temp/obj/debug/$$TARGET
    MOC_DIR     = $$PWD/../../temp/moc/debug/$$TARGET
    UI_DIR      = $$PWD/../../temp/ui/debug/$$TARGET
    RCC_DIR     = $$PWD/../../temp/rcc/debug/$$TARGET
} else {
    OBJECTS_DIR = $$PWD/../../temp/obj/release/$$TARGET
    MOC_DIR     = $$PWD/../../temp/moc/release/$$TARGET
    UI_DIR      = $$PWD/../../temp/ui/release/$$TARGET
    RCC_DIR     = $$PWD/../../temp/rcc/release/$$TARGET
}

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    Communication/Communication_Interior.cpp \
    Communication/Communication_NATS.cpp \
    Communication/NATSClient.cpp \
    Server_DataReplay.cpp \
    ScenarioMgr.cpp \
    DataFileReader.cpp \
    ReplayEngine.cpp \
    ReplayWorker.cpp \
    LogService.cpp

HEADERS += \
    Communication/Communication_Interior.h \
    Communication/Communication_NATS.h \
    Communication/NATSClient.h \
    Server_DataReplay_global.h \
    Server_DataReplay.h \
    ScenarioMgr.h \
    DataFileReader.h \
    ReplayEngine.h \
    ReplayWorker.h \
    LogService.h


# NATS 客户端库
LIBS += -L$$PWD/../../bin -lnats

# Default rules for deployment.
unix {
    target.path = /usr/lib
}
!isEmpty(target.path): INSTALLS += target
