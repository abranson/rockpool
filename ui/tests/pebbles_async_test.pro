TEMPLATE = app
TARGET = pebbles_async_test

QT += core dbus testlib
QT -= gui
CONFIG += console c++11

INCLUDEPATH += $$PWD/..

SOURCES += pebbles_async_test.cpp \
    ../pebbles.cpp \
    ../pebble.cpp \
    ../rockpoolaccount.cpp \
    ../rockpooloperation.cpp \
    ../notificationsourcemodel.cpp \
    ../applicationsmodel.cpp \
    ../applicationsfiltermodel.cpp \
    ../screenshotmodel.cpp

HEADERS += ../pebbles.h \
    ../pebble.h \
    ../rockpoolaccount.h \
    ../rockpooloperation.h \
    ../notificationsourcemodel.h \
    ../applicationsmodel.h \
    ../applicationsfiltermodel.h \
    ../screenshotmodel.h
