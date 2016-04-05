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
               rockpool.desktop \
               $$files(translations/*.ts,true)

#show all the files in QtCreator
OTHER_FILES += $${QML_FILES} \
               $${CONF_FILES}

#specify where the qml files are installed to
qml.path = /usr/share/rockpool/qml
qml.files += $${QML_FILES}

#and the app icon
icon.path = /usr/share/icons/hicolor/86x86/apps/
icon.files = rockpool.png

# Default rules for deployment.
target.path = /usr/bin

DISTFILES += \
    qml/pages/LoadingPage.qml

# Translations
lupdate_only {
    SOURCES += QML_FILES
}
CONFIG += sailfishapp_i18n
TRANSLATIONS += $$files(translations/*.ts,true)
