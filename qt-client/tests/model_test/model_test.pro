# TicketTableModel 单元测试 + 真实服务端查票集成测试
QT += core gui widgets network testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = model_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/tickettablemodel.cpp \
           ../../src/tcpclient.cpp \
           ../../src/protocolcodec.cpp \
           ../../src/mainwindow.cpp \
           ../../src/logindialog.cpp

HEADERS += ../../src/tickettablemodel.h \
           ../../src/jsonutil.h \
           ../../src/tcpclient.h \
           ../../src/protocolcodec.h \
           ../../src/mainwindow.h \
           ../../src/logindialog.h
