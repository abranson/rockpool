import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import RockWork 1.0

ListItem {
    id: root

    property string uuid: ""
    property string name: ""
    property string iconSource: ""
    property string vendor: ""
    property bool hasSettings: false
    property alias hasGrip: grip.visible
    property bool isSystemApp: false

    signal deleteApp();
    signal configureApp();

    leadingActions: ListItemActions {
        actions: [
            Action {
                visible: !root.isSystemApp
                iconName: "delete"
                onTriggered: {
                    root.deleteApp();
                }
            }
        ]
    }

    trailingActions: ListItemActions {
        actions: [
            Action {
                visible: root.hasSettings
                iconName: "settings"
                onTriggered: {
                    print("settings triggered")
                    root.configureApp();
                }
            }
        ]
    }

    RowLayout {
        anchors {
            fill: parent
            margins: units.gu(1)
        }
        spacing: units.gu(1)

        SystemAppIcon {
            Layout.fillHeight: true
            Layout.preferredWidth: height
            isSystemApp: root.isSystemApp
            uuid: root.uuid
            iconSource: root.iconSource
        }

        ColumnLayout {
            Layout.fillWidth: true
            Label {
                text: root.name
                Layout.fillWidth: true
            }

            Label {
                text: root.vendor
                Layout.fillWidth: true
                fontSize: "small"
            }
        }

        Item {
            id: grip
            Layout.fillHeight: true
            Layout.preferredWidth: height
            opacity: (root.contentMoving || root.swiped || root.dragging) ? 0 : 1
            Behavior on opacity { UbuntuNumberAnimation {} }
            Icon {
                width: units.gu(3)
                height: width
                anchors.centerIn: parent
                name: "grip-large"
            }
        }
    }
}
