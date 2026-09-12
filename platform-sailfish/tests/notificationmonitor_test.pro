TEMPLATE = app
TARGET = notificationmonitor_test

QT += core dbus
QT += gui
PKGCONFIG += dbus-1
CONFIG += console c++11 link_pkgconfig
DEFINES += LP3_NOTIFICATIONMONITOR_TEST

INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../common
INCLUDEPATH += $$PWD/../helper

SOURCES += notificationmonitor_test.cpp
HEADERS += ../common/wire.h \
    ../helper/notificationmonitor.h \
    ../../libpebble3d/include/libpebble3d-platform.h
