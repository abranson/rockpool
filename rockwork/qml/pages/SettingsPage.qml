import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    property string oauth: (pebble) ? pebble.oauthToken : ""

    SilicaFlickable {
        anchors.fill: parent
        anchors.margins: Theme.horizontalPageMargin
        contentHeight: content.height

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("General")
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

            SectionHeader {
                text: qsTr("Timeline")
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Sync calendar to timeline")
                checked: root.pebble.calendarSyncEnabled
                onClicked: {
                    root.pebble.calendarSyncEnabled = checked
                }
            }
            TextField {
                width: parent.width
                label: qsTr("Timeline Window Start (days ago)")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                text: pebble.timelineWindowStart
            }
            TextField {
                width: parent.width
                label: qsTr("Timeline Window End (days ahead)")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                text: pebble.timelineWindowEnd
            }
            TextField {
                width: parent.width
                label: qsTr("Notification re-delivery expiration (seconds)")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                text: pebble.timelineWindowFade
            }
            Button {
                width: parent.width
                text: qsTr("Set Timeline Window")
                onClicked: pebble.setTimelineWindow()
            }

            SectionHeader {
                text: qsTr("Active Timeline WebSync account")
            }
            Label {
                width: parent.width
                visible: (pebble && oauth)
                text: visible ? pebble.accountName : ""
            }
            Label {
                width: parent.width
                visible: (pebble && oauth)
                text: visible ? pebble.accountEmail : ""
            }
            Button {
                width: parent.width
                text: oauth ? qsTr("Logout") : qsTr("Login")
                onClicked: if(oauth) {
                               pebble.setOAuthToken("")
                           } else {
                               pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), {
                                              url: "https://auth-client.getpebble.com/en_US/",
                                              pebble: pebble
                                          })
                           }
            }

            SectionHeader {
                text: qsTr("Automatic Profile")
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
