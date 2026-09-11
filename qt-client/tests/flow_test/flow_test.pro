# V1-M2 数据链路测试(HTTP): 登录/表格填充/退出登录/断链提示
# 前置: C++ 服务端 127.0.0.1:6000 已启动; 测试自行拉起 FastAPI 服务层(8906)
# 运行: QT_QPA_PLATFORM=offscreen ./flow_test
QT += core gui widgets network testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = flow_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/apiclient.cpp \
           ../../src/chatwidget.cpp \
           ../../src/tracepanel.cpp \
           ../../src/logindialog.cpp \
           ../../src/mainwindow.cpp \
           ../../src/tickettablemodel.cpp \
           ../../src/reservetablemodel.cpp

HEADERS += ../../src/apiclient.h \
           ../../src/chatwidget.h \
           ../../src/tracepanel.h \
           ../../src/logindialog.h \
           ../../src/mainwindow.h \
           ../../src/tickettablemodel.h \
           ../../src/reservetablemodel.h \
           ../../src/jsonutil.h \
           ../../src/appconfig.h
