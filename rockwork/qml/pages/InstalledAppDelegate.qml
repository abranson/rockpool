import QtQuick 2.2
import Sailfish.Silica 1.0

ListItem {
    id: root

    property string uuid: ""
    property string name: ""
    property string iconSource: ""
    property string vendor: ""
    property bool hasSettings: false
    property bool isSystemApp: false

    signal launchApp
    signal deleteApp
    signal configureApp

    contentHeight: Theme.itemSizeMedium
    width: parent.width
    //height: contentHeight

    menu: ContextMenu {
        closeOnActivation: true
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
    }
}
