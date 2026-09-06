TEMPLATE = app
TARGET = pebblebondremover_test

QT += core dbus
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../common
INCLUDEPATH += $$PWD/../helper

SOURCES += pebblebondremover_test.cpp \
    ../helper/pebblebondremover.cpp
HEADERS += ../common/wire.h \
    ../helper/pebblebondremover.h \
    ../../libpebble3d/include/libpebble3d-platform.h
