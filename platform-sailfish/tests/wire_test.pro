TEMPLATE = app
TARGET = wire_test

QT -= core gui
CONFIG += console c++11
CONFIG -= app_bundle
QMAKE_CXXFLAGS += -Wall -Wextra -Werror

INCLUDEPATH += $$PWD/../common

SOURCES += wire_test.cpp
HEADERS += ../common/wire.h
