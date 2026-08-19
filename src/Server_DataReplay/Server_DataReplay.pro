QT -= gui

TEMPLATE = lib
DEFINES += SERVER_DATAREPLAY_LIBRARY

CONFIG += c++11
INCLUDEPATH += $$PWD/../../include

# DLL 与 APP 输出到同一目录
DESTDIR = $$PWD/../../bin

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
