TEMPLATE = subdirs

SUBDIRS += \
    Server_DataReplay \
    APP_DataReplay

# 指定构建顺序：先编译 Server 库，再编译 APP
APP_DataReplay.depends = Server_DataReplay
