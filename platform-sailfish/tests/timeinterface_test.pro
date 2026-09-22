TEMPLATE = app
TARGET = timeinterface_test

QT += core dbus
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/../helper
SOURCES += timeinterface_test.cpp
HEADERS += ../helper/timeinterface.h
