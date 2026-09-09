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
    src/protocolcodec.cpp \
    src/tcpclient.cpp \
    src/logindialog.cpp \
    src/tickettablemodel.cpp \
    src/reservetablemodel.cpp

HEADERS += \
    src/mainwindow.h \
    src/protocolcodec.h \
    src/tcpclient.h \
    src/logindialog.h \
    src/tickettablemodel.h \
    src/reservetablemodel.h \
    src/jsonutil.h \
    src/appconfig.h
