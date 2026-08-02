TARGET = rockpool

QT += qml quick dbus

CONFIG += c++11
CONFIG += sailfishapp

PKGCONFIG += sailfishwebengine qt5embedwidget

HEADERS += \
    notificationsourcemodel.h \
    servicecontrol.h \
    rockpoolaccount.h \
    rockpooloperation.h \
    pebble.h \
    pebbles.h \
    applicationsmodel.h \
    applicationsfiltermodel.h \
    appstoreclient.h \
    screenshotmodel.h

SOURCES += main.cpp \
    notificationsourcemodel.cpp \
    servicecontrol.cpp \
    rockpoolaccount.cpp \
    rockpooloperation.cpp \
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

JSM_FILES += $$files(jsm/*.manifest,true)
JSM_FILES += $$files(jsm/*.js,true)

SAILJAIL_FILES = $$files(Rockpool.permission,true)

#show all the files in QtCreator
OTHER_FILES += $${QML_FILES} \
               $${JSM_FILES} \
               $${CONF_FILES} \
               $${SAILJAIL_FILES}

#specify where the qml files are installed to
qml.path = /usr/share/rockpool/qml
qml.files += $${QML_FILES}

# Default rules for deployment.
target.path = /usr/bin

# gecko js modules
jsm.path = /usr/share/rockpool/jsm
jsm.files += $${JSM_FILES}

SAILFISHAPP_ICONS += 86x86 108x108 128x128 256x256

sailjail.path = /etc/sailjail/permissions
sailjail.files += $${SAILJAIL_FILES}

DISTFILES += JSM_FILES \
    icons/86x86/rockpool.png \
    icons/108x108/rockpool.png \
    icons/128x128/rockpool.png \
    icons/256x256/rockpool.png
INSTALLS += jsm sailjail

CONFIG(debug, debug|release) {
    DEFINES += 'ROCKPOOL_DATA_PATH=\\"/opt/sdk/rockpool/usr/share/rockpool/\\"'
}
# Translations
lupdate_only {
    SOURCES += $${QML_FILES}
}
TRANSLATIONS += $$files(translations/*.ts,true)
CONFIG += sailfishapp_i18n
load(sailfishapp_i18n)

# Building an RPM must not update checked-in translation sources.  Keep the
# Sailfish translation install rule, but compile the existing catalogs without
# its install-time lupdate step.
qm.commands = mkdir -p translations && \
    [ \"$${OUT_PWD}\" != \"$${_PRO_FILE_PWD_}\" -a $$HAVE_TRANSLATIONS -eq 1 ] && \
    cp -af $${TRANSLATIONS_IN} $${OUT_PWD}/translations || :
qm.commands += ; [ $$HAVE_TRANSLATIONS -eq 1 ] && \
    lrelease $${TRANSLATE_UNFINISHED} $${TRANSLATIONS_OUT} || :
