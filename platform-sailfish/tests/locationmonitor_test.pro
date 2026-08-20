TEMPLATE = app
TARGET = locationmonitor_test

QT += core positioning
QT -= gui
CONFIG += console c++11
DEFINES += LP3_LOCATION_TEST

INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../helper

SOURCES += locationmonitor_test.cpp \
    ../helper/locationmonitor.cpp
HEADERS += ../helper/locationmonitor.h \
    ../../libpebble3d/include/libpebble3d-platform.h
