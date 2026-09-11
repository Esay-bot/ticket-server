# 票务预约系统 Qt 图形客户端
# 编译(WSL/Linux): mkdir -p build && cd build && qmake ../ticket-client.pro && make -j$(nproc)
QT += core gui network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET   = ticket-client
TEMPLATE = app
CONFIG  += c++14

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/apiclient.cpp \
    src/chatwidget.cpp \
    src/tracepanel.cpp \
    src/logindialog.cpp \
    src/tickettablemodel.cpp \
    src/reservetablemodel.cpp

HEADERS += \
    src/mainwindow.h \
    src/apiclient.h \
    src/chatwidget.h \
    src/tracepanel.h \
    src/logindialog.h \
    src/tickettablemodel.h \
    src/reservetablemodel.h \
    src/jsonutil.h \
    src/appconfig.h

# 直连 TCP 形态(V1-M2 起主程序不再使用, 保留: 协议层实现 + codec/net 测试)
SOURCES += src/protocolcodec.cpp src/tcpclient.cpp
HEADERS += src/protocolcodec.h src/tcpclient.h

RESOURCES += \
    res/ticket-client.qrc
