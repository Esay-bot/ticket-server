# 登录/注册对话框自动化测试(HTTP 形态, offscreen 运行)
# 前置: C++ 服务端 127.0.0.1:6000 已启动; 测试自行拉起 FastAPI 服务层(8901)
# 运行: QT_QPA_PLATFORM=offscreen ./dialog_test
QT += core gui widgets network testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = dialog_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/apiclient.cpp \
           ../../src/logindialog.cpp

HEADERS += ../../src/apiclient.h \
           ../../src/logindialog.h \
           ../../src/appconfig.h
