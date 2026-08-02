TEMPLATE = app
TARGET = mainvolumemonitor_test

QT += core dbus
QT -= gui
CONFIG += console c++11
DEFINES += LP3_MAINVOLUME_TEST

INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../helper

SOURCES += mainvolumemonitor_test.cpp \
    ../helper/mainvolumemonitor.cpp
HEADERS += ../helper/mainvolumemonitor.h \
    ../../libpebble3d/include/libpebble3d-platform.h
