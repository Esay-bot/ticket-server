# V1-M3 验收测试: 聊天页签 + 确认卡片(渲染单测无 Key 可跑; 真实场景需 DEEPSEEK_API_KEY)
# 前置: C++ 服务端 127.0.0.1:6000 已启动; 测试自行拉起 FastAPI 服务层(8908)
# 运行: QT_QPA_PLATFORM=offscreen ./chat_test
QT += core gui widgets network testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = chat_test

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
