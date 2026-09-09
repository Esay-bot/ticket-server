# TcpClient 集成测试: 需要本机 127.0.0.1:6000 有真实服务端在运行
# 前置: cd ../../.. && ./server/ser
QT -= gui
QT += core network
CONFIG += console c++14
TEMPLATE = app
TARGET   = net_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/tcpclient.cpp \
           ../../src/protocolcodec.cpp

HEADERS += ../../src/tcpclient.h \
           ../../src/protocolcodec.h
