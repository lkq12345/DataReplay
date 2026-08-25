QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

DESTDIR = $$PWD/../../bin/

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# 添加公共头文件路径
INCLUDEPATH += $$PWD/../../include

# 添加 Server 库的头文件路径（用于引用 ScenarioMgr, ReplayEngine 等）
INCLUDEPATH += $$PWD/../Server_DataReplay

SOURCES += \
    main.cpp \
    DataReplayWidget.cpp \
    ScenarioFilterProxyModel.cpp \
    EntityFilterProxyModel.cpp \
    ImportDialog.cpp \
    DescriptionDialog.cpp

HEADERS += \
    DataReplayWidget.h \
    ScenarioFilterProxyModel.h \
    EntityFilterProxyModel.h \
    ImportDialog.h \
    DescriptionDialog.h

FORMS += \
    DataReplayWidget.ui

# 链接 Server_DataReplay 库
LIBS += -L$$PWD/../../bin -lServer_DataReplay


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
