TARGET = rockpool

include(../version.pri)

QT += qml quick dbus webkit quick-private webkit-private

CONFIG += c++11
CONFIG += sailfishapp

HEADERS += \
    notificationsourcemodel.h \
    servicecontrol.h \
    pebble.h \
    pebbles.h \
    applicationsmodel.h \
    applicationsfiltermodel.h \
    appstoreclient.h \
    screenshotmodel.h

SOURCES += main.cpp \
    notificationsourcemodel.cpp \
    servicecontrol.cpp \
    pebble.cpp \
    pebbles.cpp \
    applicationsmodel.cpp \
    applicationsfiltermodel.cpp \
    appstoreclient.cpp \
    screenshotmodel.cpp

RESOURCES += rockwork.qrc

QML_FILES += $$files(qml/*.qml,true)
QML_FILES += $$files(qml/pages/*.qml,true)
QML_FILES += $$files(qml/cover/*.qml,true)

CONF_FILES +=  rockpool.png \
               rockpool.desktop

#show all the files in QtCreator
OTHER_FILES += $${QML_FILES} \
               $${CONF_FILES}

#specify where the qml files are installed to
qml.path = /usr/share/rockpool/qml
qml.files += $${QML_FILES}
INSTALLS+=qml

#and the app icon
icon.path = /usr/share/icons/hicolor/86x86/apps/
icon.files = rockpool.png
INSTALLS+=icon

# Default rules for deployment.
INSTALLS+=target icon
target.path = /usr/bin

DISTFILES += \
    qml/pages/LoadingPage.qml

