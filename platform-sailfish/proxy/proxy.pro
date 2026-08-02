TEMPLATE = lib
TARGET = pebble3d-platform-sailfish

QT -= core gui
# Providers are discovered as regular, exact-name .so files.  Do not let the
# normal shared-library versioning rule turn that file into a symlink to an
# ABI-versioned payload: the host intentionally rejects provider symlinks.
CONFIG += c++11 hide_symbols unversioned_libname
DEFINES += _GNU_SOURCE
QMAKE_CXXFLAGS += -fvisibility=hidden
QMAKE_LFLAGS += -pthread -Wl,-z,relro -Wl,-z,now
LIBS += -pthread

# This provider and the development RPM are produced by the same source
# package, so compile against the canonical ABI header in the source tree.
INCLUDEPATH += $$PWD/../../libpebble3d/include
INCLUDEPATH += $$PWD/../common

SOURCES += sailfish_proxy.cpp
HEADERS += ../common/wire.h \
    ../../libpebble3d/include/libpebble3d-launcher-wire.h

isEmpty(LP3_LIBDIR): LP3_LIBDIR = /usr/lib
!isEmpty(LP3_BUILD_ID): DEFINES += LP3_PLATFORM_BUILD_ID=\\\"$$LP3_BUILD_ID\\\"
target.path = $$LP3_LIBDIR/libpebble3d/platforms
INSTALLS += target
