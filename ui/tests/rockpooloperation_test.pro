TEMPLATE = app
TARGET = rockpooloperation_test

QT += core dbus testlib
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/..

SOURCES += rockpooloperation_test.cpp \
    ../rockpooloperation.cpp

HEADERS += ../rockpooloperation.h
