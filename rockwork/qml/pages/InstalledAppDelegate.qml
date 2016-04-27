import QtQuick 2.2
import Sailfish.Silica 1.0

ListItem {
    id: root

    property string uuid: ""
    property string name: ""
    property string iconSource: ""
    property string vendor: ""
    property string version: ""
    property string candidate: ""
    property bool hasSettings: false
    property bool isSystemApp: false
    property bool isLastApp: true

    signal launchApp
    signal deleteApp
    signal configureApp
    signal moveApp(int dir)
    signal upgrade

    contentHeight: Theme.itemSizeMedium
    width: parent.width

    menu: ContextMenu {
        closeOnActivation: true
        MenuItem {
            text: (candidate ? qsTr("Upgrade") + " " + version +  " >> " + candidate : qsTr("Version") + " " + version)
            onClicked: root.upgrade()
            enabled: candidate && candidate != version
            visible: !isSystemApp
        }

        MenuItem {
            text: qsTr("Launch")
            onClicked: root.launchApp()
        }
        MenuItem {
            text: qsTr("Settings")
            visible: root.hasSettings
            onClicked: root.configureApp()
        }
        MenuItem {
            text: qsTr("Delete")
            visible: !root.isSystemApp
            onClicked: {
                root.remorseAction(qsTr("Really Delete?"), function () {
                    root.deleteApp()
                })
            }
        }
        MenuItem {
            text: qsTr("Move Up")
            visible: index > 1
            onClicked: root.moveApp(-1)
        }
        MenuItem {
            text: qsTr("Move Down")
            visible: index>0 && !root.isLastApp
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
            visible: candidate && candidate != version
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    onClicked: showMenu()
}
