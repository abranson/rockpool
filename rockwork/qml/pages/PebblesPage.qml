import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    SilicaListView {
        anchors.fill: parent
        model: pebbles
        header: PageHeader {
            title: qsTr("RockPool")
            description: qsTr("Manage Pebble Watches")
        }
        PullDownMenu {
            MenuItem {
                text: qsTr("Bluetooth Settings")
                onClicked: rockPool.startBT()
            }
            MenuItem {
                text: qsTr("Restart service")
                onClicked: rockPool.restartService()
            }
        }

        delegate: ListItem {
            contentHeight: Theme.fontSizeMedium*2
            Row {
                anchors.fill: parent
                anchors.margins: Theme.horizontalPageMargins

                Column {
                    Label {
                        text: model.name
                    }

                    Label {
                        text: model.connected ? qsTr("Connected") : qsTr("Disconnected")
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }

            onClicked: {
                var p = pebbles.get(index);
                print("opening pebble:", p.name, p.hardwarePlatform)
                rockPool.curPebble=index;
                pageStack.push(Qt.resolvedUrl("MainMenuPage.qml"), {pebble: pebbles.get(index)})
            }
        }
    }

    ViewPlaceholder {
        anchors.fill: parent
        enabled: pebbles.count === 0

        Label {
            text: qsTr("No Pebble smartwatches configured yet. Please connect your Pebble smartwatch using System Settings.")
            font.pixelSize: Theme.fontSizeLarge
            width: parent.width-(Theme.paddingSmall*2)
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Button {
            text: qsTr("Open Bluetooth Settings")
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            onClicked: rockPool.startBT()
        }
    }
}
