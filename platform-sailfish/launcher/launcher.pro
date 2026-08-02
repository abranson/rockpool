TEMPLATE = app
TARGET = libpebble3d-platform-sailfish-launcher

QT -= core gui
CONFIG += console
DEFINES += _GNU_SOURCE
QMAKE_CFLAGS += -std=c11 -fPIE -Wall -Wextra -Werror
QMAKE_LFLAGS += -pie -Wl,-z,relro -Wl,-z,now

INCLUDEPATH += $$PWD/../../libpebble3d/include

SOURCES += main.c
HEADERS += ../../libpebble3d/include/libpebble3d-launcher-wire.h

isEmpty(LP3_LIBEXECDIR): LP3_LIBEXECDIR = /usr/libexec
DEFINES += LP3_LIBEXECDIR=\\\"$$LP3_LIBEXECDIR\\\"
!isEmpty(LP3_BUILD_ID): DEFINES += LP3_PLATFORM_BUILD_ID=\\\"$$LP3_BUILD_ID\\\"
target.path = $$LP3_LIBEXECDIR/libpebble3d
INSTALLS += target
