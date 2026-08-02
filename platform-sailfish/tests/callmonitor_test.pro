TEMPLATE = app
TARGET = callmonitor_test

QT += core dbus
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../common
INCLUDEPATH += $$PWD/../helper

SOURCES += callmonitor_test.cpp \
    ../helper/callmonitor.cpp
HEADERS += ../helper/callmonitor.h \
    ../common/wire.h \
    ../../libpebble3d/include/libpebble3d-platform.h
