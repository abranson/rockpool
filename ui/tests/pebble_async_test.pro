TEMPLATE = app
TARGET = pebble_async_test

QT += core dbus testlib
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/..

SOURCES += pebble_async_test.cpp \
    ../pebble.cpp \
    ../rockpoolaccount.cpp \
    ../rockpooloperation.cpp \
    ../notificationsourcemodel.cpp \
    ../applicationsmodel.cpp \
    ../screenshotmodel.cpp

HEADERS += ../pebble.h \
    ../rockpoolaccount.h \
    ../rockpooloperation.h \
    ../notificationsourcemodel.h \
    ../applicationsmodel.h \
    ../screenshotmodel.h
