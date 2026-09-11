# TicketTableModel / ReserveTableModel 单元测试(不需要任何服务)
QT += core gui widgets testlib
CONFIG += c++14
TEMPLATE = app
TARGET   = model_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/tickettablemodel.cpp \
           ../../src/reservetablemodel.cpp

HEADERS += ../../src/tickettablemodel.h \
           ../../src/reservetablemodel.h \
           ../../src/jsonutil.h
