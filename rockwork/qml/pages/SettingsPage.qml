import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    property string connectedProfile: pebble ? pebble.profileWhenConnected : ""
    property string disconnectedProfile: pebble ? pebble.profileWhenDisconnected : ""
    property string oauth: (pebble) ? pebble.oauthToken : ""
    property var cannedResponses: pebble ? pebble.cannedResponses : {}
    property var notificationsMap: pebble ? pebble.notificationsFilter:{}
    property bool timelineWindowChanged: false

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
            TextSwitch {
                width: parent.width
                text: qsTr("Sync Apps from Cloud")
                checked: root.pebble.syncAppsFromCloud
                onClicked: {
                    root.pebble.syncAppsFromCloud = checked
                }
            }
            Button {
                width: parent.width
                text: qsTr("Reset Timeline")
                onClicked: pebble.resetTimeline()
            }
            TextField {
                width: parent.width
                label: qsTr("Timeline Window Start (days ago)")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                text: pebble.timelineWindowStart
                onTextChanged: timelineWindowChanged=true
            }
            TextField {
                width: parent.width
                label: qsTr("Timeline Window End (days ahead)")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                text: pebble.timelineWindowEnd
                onTextChanged: timelineWindowChanged=true
            }
            TextField {
                width: parent.width
                label: qsTr("Notification re-delivery expiration (seconds)")
                placeholderText: label
                inputMethodHints: Qt.ImhDigitsOnly
                text: pebble.timelineWindowFade
                onTextChanged: timelineWindowChanged=true
            }
            Button {
                width: parent.width
                text: qsTr("Set Timeline Window")
                onClicked: {pebble.setTimelineWindow();timelineWindowChanged=false}
                enabled: timelineWindowChanged
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
                               pebble.setOAuthToken("");
                               oauth = "";
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
                            down: modelData === root.connectedProfile || (root.connectedProfile === "" && index === 0)
                        }
                    }
                }
                value: root.connectedProfile === "" ? qsTr("no change") : root.connectedProfile
                onCurrentIndexChanged: {
                    root.connectedProfile = currentIndex == 0 ? "" : currentItem.text
                    root.pebble.profileWhenConnected = root.connectedProfile
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
                            down: modelData === root.disconnectedProfile || (root.disconnectedProfile === "" && index == 0)
                        }
                    }
                }
                value: root.disconnectedProfile === "" ? qsTr("no change") : root.disconnectedProfile
                onCurrentIndexChanged: {
                    root.disconnectedProfile = currentIndex == 0 ? "" : currentItem.text;
                    root.pebble.profileWhenDisconnected = root.disconnectedProfile;
                }
            }
            SectionHeader {
                text: qsTr("Canned Messages")
            }
            Repeater {
                model: Object.keys(cannedResponses)
                delegate: BackgroundItem {
                    Row {
                        height: Theme.itemSizeSmall
                        spacing: Theme.paddingMedium
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            height: Theme.iconSizeSmall
                            width: height
                            source: {
                                var icon = (modelData in notificationsMap)?notificationsMap[modelData]["icon"]:"icon-lock-chat";
                                if (icon.indexOf("image://") === 0 || icon.indexOf("file://")  === 0)
                                    return icon;
                                else if (icon.indexOf("/") === 0)
                                    return "file://" + icon
                                else
                                    return "image://theme/"+icon

                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData in notificationsMap)?notificationsMap[modelData]["name"]:modelData
                            color: highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                    onClicked: {
                        console.log(modelData,cannedResponses[modelData]);
                        pageStack.push(Qt.resolvedUrl("ResponsesPage.qml"), {
                                           pebble: root.pebble,
                                           source: modelData,
                                           title: (modelData in notificationsMap)?notificationsMap[modelData]["name"]:modelData,
                                           list: cannedResponses[modelData]})
                    }
                }
            }
        }
        Component.onCompleted: rockPool.getProfiles()
    }
}
