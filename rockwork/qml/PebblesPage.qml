import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3

Page {
    title: i18n.tr("Manage Pebble Watches")

    head {
        actions: [
            Action {
                iconName: "settings"
                onTriggered: {
                    onClicked: Qt.openUrlExternally("settings://system/bluetooth")
                }
            }
        ]
    }

    ListView {
        anchors.fill: parent
        model: pebbles
        delegate: ListItem {
            RowLayout {
                anchors.fill: parent
                anchors.margins: units.gu(1)

                ColumnLayout {
                    Layout.fillHeight: true
                    Layout.fillWidth: true

                    Label {
                        text: model.name
                    }

                    Label {
                        text: model.connected ? i18n.tr("Connected") : i18n.tr("Disconnected")
                        fontSize: "small"
                    }
                }
            }

            onClicked: {
                var p = pebbles.get(index);
                print("opening pebble:", p.name, p.hardwarePlatform)
                pageStack.push(Qt.resolvedUrl("MainMenuPage.qml"), {pebble: pebbles.get(index)})
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: parent.width - units.gu(4)
        spacing: units.gu(4)
        visible: pebbles.count === 0

        Label {
            text: i18n.tr("No Pebble smartwatches configured yet. Please connect your Pebble smartwatch using System Settings.")
            fontSize: "large"
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Button {
            text: i18n.tr("Open System Settings")
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: Qt.openUrlExternally("settings://system/bluetooth")
        }
    }
}
