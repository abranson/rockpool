import QtQuick 2.6
import Sailfish.Silica 1.0

ListItem {
    id: root

    property string uuid: ""
    property string name: ""
    property string iconSource: ""
    property string vendor: ""
    property string version: ""
    property string candidate: ""
    property string installedStoreSource
    property string candidateStoreSource
    property bool switchAvailable
    readonly property bool crossStore: installedStoreSource.length > 0 && candidateStoreSource.length > 0
                                       && installedStoreSource !== candidateStoreSource
    readonly property bool hasUpgrade: candidate.length > 0 && !switchAvailable && candidate !== version
                                       && installedStoreSource.length > 0 && candidateStoreSource.length > 0

    function versionActionText() {
        var sourceName = candidateStoreSource === "core" ? "Core Devices" : "Rebble"
        if (switchAvailable && crossStore)
            return qsTr("Switch build to %1").arg(sourceName)
        if (hasUpgrade && crossStore)
            return qsTr("Upgrade from %1").arg(sourceName)
        if (hasUpgrade)
            return qsTr("Upgrade") + " " + version + " >> " + candidate
        return qsTr("Version") + " " + version
    }
    property bool hasSettings: false
    property bool isSystemApp: false
    property bool isLastApp: true
    property bool appMutationsAllowed: true
    property bool watchConnected: true
    property bool offlineSettingsAvailable: false

    signal launchApp
    signal deleteApp
    signal configureApp
    signal reviewPermissions
    signal moveApp(int dir)
    signal openStore
    signal upgrade

    contentHeight: Theme.itemSizeMedium
    width: parent.width

    menu: ContextMenu {
        closeOnActivation: true
        MenuItem {
            text: root.versionActionText()
            onClicked: root.upgrade()
            enabled: root.hasUpgrade || (root.switchAvailable && root.crossStore)
            visible: !isSystemApp
        }

        MenuItem {
            text: qsTr("Open in store")
            visible: !root.isSystemApp && root.installedStoreSource.length > 0
            onClicked: root.openStore()
        }
        MenuItem {
            text: qsTr("Launch")
            enabled: root.watchConnected
            onClicked: root.launchApp()
        }
        MenuItem {
            text: qsTr("Settings")
            visible: root.hasSettings || root.offlineSettingsAvailable
            enabled: root.watchConnected || root.offlineSettingsAvailable
            onClicked: root.configureApp()
        }
        MenuItem {
            text: qsTr("Permissions")
            visible: !root.isSystemApp
            onClicked: root.reviewPermissions()
        }
        MenuItem {
            text: qsTr("Delete")
            visible: !root.isSystemApp
            enabled: root.appMutationsAllowed
            onClicked: {
                root.remorseAction(qsTr("Really Delete?"), function () {
                    root.deleteApp()
                })
            }
        }
        MenuItem {
            text: qsTr("Move Up")
            visible: index > 1
            enabled: root.appMutationsAllowed
            onClicked: root.moveApp(-1)
        }
        MenuItem {
            text: qsTr("Move Down")
            visible: index>0 && !root.isLastApp
            enabled: root.appMutationsAllowed
            onClicked: root.moveApp(1)
        }
    }

    Row {
        anchors.fill: parent
        spacing: Theme.paddingSmall

        SystemAppIcon {
            height: Theme.iconSizeMedium
            width: height
            isSystemApp: root.isSystemApp
            uuid: root.uuid
            iconSource: root.iconSource
            anchors.verticalCenter: parent.verticalCenter
        }

        Column {
            spacing: Theme.paddingSmall
            Label {
                text: root.name
            }

            Label {
                text: root.vendor
                font.pixelSize: Theme.fontSizeSmall
            }
        }
        Image {
            source: "image://theme/icon-s-update"
            visible: root.hasUpgrade
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    onClicked: showMenu()
}
