TEMPLATE = app
TARGET = uiactivation_test
QT += core dbus testlib
QT -= gui
CONFIG += console c++11
HEADERS += ../uiactivation.h
SOURCES += uiactivation_test.cpp
