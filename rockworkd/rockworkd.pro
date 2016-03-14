QT += core bluetooth dbus network contacts qml location
QT -= gui

include(../version.pri)

TARGET = rockpoold

CONFIG += c++11
CONFIG += console
CONFIG += link_pkgconfig

INCLUDEPATH += $$[QT_HOST_PREFIX]/include/quazip/
LIBS += -lquazip

PKGCONFIG += qt5-boostable libmkcal-qt5 libkcalcoren-qt5 dbus-1 mpris-qt5
INCLUDEPATH += /usr/include/mkcal-qt5 /usr/include/kcalcoren-qt5

SOURCES += main.cpp \
    libpebble/watchconnection.cpp \
    libpebble/pebble.cpp \
    libpebble/watchdatareader.cpp \
    libpebble/watchdatawriter.cpp \
    libpebble/notificationendpoint.cpp \
    libpebble/musicendpoint.cpp \
    libpebble/phonecallendpoint.cpp \
    libpebble/musicmetadata.cpp \
    libpebble/jskit/jskitmanager.cpp \
    libpebble/jskit/jskitconsole.cpp \
    libpebble/jskit/jskitgeolocation.cpp \
    libpebble/jskit/jskitlocalstorage.cpp \
    libpebble/jskit/jskitpebble.cpp \
    libpebble/jskit/jskitxmlhttprequest.cpp \
    libpebble/jskit/jskittimer.cpp \
    libpebble/jskit/jskitperformance.cpp \
#   libpebble/jskit/jskitwebsocket.cpp \
    libpebble/appinfo.cpp \
    libpebble/appmanager.cpp \
    libpebble/appmsgmanager.cpp \
    libpebble/uploadmanager.cpp \
    libpebble/bluez/bluezclient.cpp \
    libpebble/bluez/bluez_agentmanager1.cpp \
    libpebble/bluez/bluez_adapter1.cpp \
    libpebble/bluez/bluez_device1.cpp \
    libpebble/bluez/freedesktop_objectmanager.cpp \
    libpebble/bluez/freedesktop_properties.cpp \
    core.cpp \
    pebblemanager.cpp \
    dbusinterface.cpp \
# Platform integration part
    platformintegration/sailfish/sailfishplatform.cpp \
#   platformintegration/sailfish/callchannelobserver.cpp \
    platformintegration/sailfish/voicecallmanager.cpp \
    platformintegration/sailfish/voicecallhandler.cpp \
    libpebble/blobdb.cpp \
    libpebble/timelineitem.cpp \
    libpebble/notification.cpp \
    platformintegration/sailfish/organizeradapter.cpp \
    libpebble/calendarevent.cpp \
    libpebble/appmetadata.cpp \
    libpebble/appdownloader.cpp \
    libpebble/screenshotendpoint.cpp \
    libpebble/firmwaredownloader.cpp \
    libpebble/bundle.cpp \
    libpebble/watchlogendpoint.cpp \
    libpebble/ziphelper.cpp \
    libpebble/healthparams.cpp \
    libpebble/dataloggingendpoint.cpp \
    platformintegration/sailfish/musiccontroller.cpp \
    platformintegration/sailfish/notificationmonitor.cpp \
    platformintegration/sailfish/notifications.cpp

HEADERS += \
    libpebble/watchconnection.h \
    libpebble/pebble.h \
    libpebble/watchdatareader.h \
    libpebble/watchdatawriter.h \
    libpebble/notificationendpoint.h \
    libpebble/musicendpoint.h \
    libpebble/musicmetadata.h \
    libpebble/phonecallendpoint.h \
    libpebble/platforminterface.h \
    libpebble/jskit/jskitmanager.h \
    libpebble/jskit/jskitconsole.h \
    libpebble/jskit/jskitgeolocation.h \
    libpebble/jskit/jskitlocalstorage.h \
    libpebble/jskit/jskitpebble.h \
    libpebble/jskit/jskitxmlhttprequest.h \
    libpebble/jskit/jskittimer.h \
    libpebble/jskit/jskitperformance.h \
#   libpebble/jskit/jskitwebsocket.h \
    libpebble/appinfo.h \
    libpebble/appmanager.h \
    libpebble/appmsgmanager.h \
    libpebble/uploadmanager.h \
    libpebble/bluez/bluezclient.h \
    libpebble/bluez/bluez_agentmanager1.h \
    libpebble/bluez/bluez_adapter1.h \
    libpebble/bluez/bluez_device1.h \
    libpebble/bluez/freedesktop_objectmanager.h \
    libpebble/bluez/freedesktop_properties.h \
    core.h \
    pebblemanager.h \
    dbusinterface.h \
# Platform integration part
    platformintegration/sailfish/sailfishplatform.h \
    platformintegration/sailfish/voicecallmanager.h \
    platformintegration/sailfish/voicecallhandler.h \
    platformintegration/sailfish/organizeradapter.h \
    platformintegration/sailfish/musiccontroller.h \
    platformintegration/sailfish/notificationmonitor.h \
    libpebble/blobdb.h \
    libpebble/timelineitem.h \
    libpebble/notification.h \
    libpebble/calendarevent.h \
    libpebble/appmetadata.h \
    libpebble/appdownloader.h \
    libpebble/enums.h \
    libpebble/screenshotendpoint.h \
    libpebble/firmwaredownloader.h \
    libpebble/bundle.h \
    libpebble/watchlogendpoint.h \
    libpebble/ziphelper.h \
    libpebble/healthparams.h \
    libpebble/dataloggingendpoint.h \
    platformintegration/sailfish/notifications.h

testing: {
    SOURCES += platformintegration/testing/testingplatform.cpp
    HEADERS += platformintegration/testing/testingplatform.h
    RESOURCES += platformintegration/testing/testui.qrc
    DEFINES += ENABLE_TESTING
    QT += qml quick
}

INSTALLS += target systemd

systemd.files = $${TARGET}.service
systemd.path = /usr/lib/systemd/user

# Default rules for deployment.
target.path = /usr/bin

RESOURCES += \
    libpebble/jskit/jsfiles.qrc
