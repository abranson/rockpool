import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null

    SilicaFlickable {
        anchors.fill: parent
        anchors.margins: Theme.horizontalPageMargin

        Column {
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: qsTr("Settings")
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Sync calendar to timeline")
                checked: root.pebble.calendarSyncEnabled
                onClicked: {
                    root.pebble.calendarSyncEnabled = checked
                }
            }

            ComboBox {
                width: parent.width
                label: qsTr("Distance Units")
                menu: ContextMenu {
                        MenuItem {
                            text: qsTr("Metric")
                        }
                        MenuItem {
                            text: qsTr("Imperial")
                        }
                    }
                onCurrentIndexChanged: {
                    root.pebble.imperialUnits = (currentIndex===1)
                }
                currentIndex: (root.pebble.imperialUnits) ? 1 : 0
            }

            Label {
                text: qsTr("Automatic Profile")
                font.family: Theme.fontFamilyHeading
                color: Theme.highlightColor
                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingMedium
            }
            ComboBox {
                width: parent.width
                label: qsTr("Connected")
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("no change")
                    }
                    Repeater {
                        model: rockPool.sysProfiles
                        delegate: MenuItem {
                            text: modelData
                            down: modelData === root.pebble.profileWhenConnected || (root.pebble.profileWhenConnected === "" && index === 0)
                        }
                    }
                }
                value: root.pebble.profileWhenConnected === "" ? qsTr("no change") : root.pebble.profileWhenConnected
                onCurrentIndexChanged: {
                    root.pebble.profileWhenConnected = currentIndex == 0 ? "" : currentItem.text
                }
            }
            ComboBox {
                width: parent.width
                label: qsTr("Disconnected")
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("no change")
                    }
                    Repeater {
                        model: rockPool.sysProfiles
                        delegate: MenuItem {
                            text: modelData
                            down: modelData === root.pebble.profileWhenDisconnected || (root.pebble.profileWhenConnected === "" && index == 0)
                        }
                    }
                }
                value: root.pebble.profileWhenDisconnected === "" ? qsTr("no change") : root.pebble.profileWhenDisconnected
                onCurrentIndexChanged: {
                    root.pebble.profileWhenDisconnected = currentIndex == 0 ? "" : currentItem.text
                }
            }
        }
        Component.onCompleted: rockPool.getProfiles()
    }
}
