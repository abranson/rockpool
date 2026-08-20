TEMPLATE = app
TARGET = libpebble3d-platform-sailfish-host

QT += core dbus positioning
QT -= gui
PKGCONFIG += dbus-1 mlite5
CONFIG += console c++11 link_pkgconfig
DEFINES += _GNU_SOURCE
QMAKE_CXXFLAGS += -fPIE
QMAKE_LFLAGS += -pie -pthread -Wl,-z,relro -Wl,-z,now
LIBS += -pthread

# This helper and the development RPM are produced by the same source package,
# so compile against the canonical ABI header in the source tree.
INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../common

SOURCES += main.cpp \
    callmonitor.cpp \
    locationmonitor.cpp \
    mainvolumemonitor.cpp \
    notificationmonitor.cpp
HEADERS += ../common/wire.h \
    ../../libpebble3d/include/libpebble3d-launcher-wire.h \
    callmonitor.h \
    locationmonitor.h \
    mainvolumemonitor.h \
    notificationmonitor.h

isEmpty(LP3_LIBEXECDIR): LP3_LIBEXECDIR = /usr/libexec
!isEmpty(LP3_BUILD_ID): DEFINES += LP3_PLATFORM_BUILD_ID=\\\"$$LP3_BUILD_ID\\\"
target.path = $$LP3_LIBEXECDIR/libpebble3d
INSTALLS += target
