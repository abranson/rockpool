TEMPLATE = app
TARGET = stop_handshake_test

QT -= core gui
CONFIG += console c++11
CONFIG -= app_bundle
DEFINES += _GNU_SOURCE
QMAKE_CXXFLAGS += -Wall -Wextra -Werror

INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../common

SOURCES += stop_handshake_test.cpp
HEADERS += ../common/wire.h \
    ../../libpebble3d/include/libpebble3d-launcher-wire.h \
    ../../libpebble3d/include/libpebble3d-platform.h

LIBS += -ldl -pthread
