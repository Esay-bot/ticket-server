# 登录/注册对话框自动化测试(offscreen 运行, 需真实服务端 127.0.0.1:6000)
# 运行: QT_QPA_PLATFORM=offscreen ./dialog_test
QT += core gui widgets network testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = dialog_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/tcpclient.cpp \
           ../../src/protocolcodec.cpp \
           ../../src/logindialog.cpp

HEADERS += ../../src/tcpclient.h \
           ../../src/protocolcodec.h \
           ../../src/logindialog.h
