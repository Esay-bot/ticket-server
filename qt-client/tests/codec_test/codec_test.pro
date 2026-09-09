# ProtocolCodec 单元测试(控制台程序, 不依赖 GUI)
# 运行: mkdir -p build && cd build && qmake ../codec_test.pro && make -j && ./codec_test
QT -= gui
QT += core
CONFIG += console c++14
TEMPLATE = app
TARGET   = codec_test

INCLUDEPATH += ../../src

SOURCES += main.cpp \
           ../../src/protocolcodec.cpp

HEADERS += ../../src/protocolcodec.h
