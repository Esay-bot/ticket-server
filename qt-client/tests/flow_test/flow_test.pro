# M5 全流程测试: 预约/取消/我的预约 + 断线重连(会 kill 并重启真实服务端)
QT += core gui widgets network testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = flow_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/tickettablemodel.cpp \
           ../../src/reservetablemodel.cpp \
           ../../src/tcpclient.cpp \
           ../../src/protocolcodec.cpp \
           ../../src/mainwindow.cpp \
           ../../src/logindialog.cpp

HEADERS += ../../src/tickettablemodel.h \
           ../../src/reservetablemodel.h \
           ../../src/jsonutil.h \
           ../../src/appconfig.h \
           ../../src/tcpclient.h \
           ../../src/protocolcodec.h \
           ../../src/mainwindow.h \
           ../../src/logindialog.h
