TEMPLATE = app
TARGET = servicecontrol_async_test

QT += core dbus testlib
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/..

SOURCES += servicecontrol_async_test.cpp \
    ../servicecontrol.cpp

HEADERS += ../servicecontrol.h
