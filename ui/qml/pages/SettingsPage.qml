import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    property string connectedProfile: pebble ? pebble.profileWhenConnected : ""
    property string disconnectedProfile: pebble ? pebble.profileWhenDisconnected : ""
    property bool settingsReady: pebble && pebble.settingsPageReady
    property bool accountAuthenticated: pebble ? pebble.accountAuthenticated : false
    property var cannedResponses: pebble ? pebble.cannedResponses : {}
    property bool cannedResponsesReady: pebble && pebble.cannedResponsesReady
    property var notificationsMap: pebble ? pebble.notificationsFilter:{}
    property bool timelineWindowDirty
    property bool loadingTimelineWindow

    function loadTimelineWindow() {
        if (!root.pebble || !root.pebble.timelineWindowReady
                || root.timelineWindowDirty) {
            return
        }
        root.loadingTimelineWindow = true
        timelineWindowStartField.text = root.pebble.timelineWindowStart
        timelineWindowFadeField.text = root.pebble.timelineWindowFade
        timelineWindowEndField.text = root.pebble.timelineWindowEnd
        root.loadingTimelineWindow = false
    }

    Component.onCompleted: {
        if (root.pebble) {
            root.pebble.refreshSettingsPage()
            root.pebble.refreshCannedResponses()
            root.pebble.refreshTimelineWindow()
        }
    }

    Connections {
        target: root.pebble
        onTimelineWindowChanged: root.loadTimelineWindow()
        onTimelineWindowReadyChanged: root.loadTimelineWindow()
    }

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
                enabled: root.settingsReady
                menu: ContextMenu {
                        MenuItem {
                            text: qsTr("Metric")
                            onClicked: root.pebble.imperialUnits = false
                        }
                        MenuItem {
                            text: qsTr("Imperial")
                            onClicked: root.pebble.imperialUnits = true
                        }
                    }
                currentIndex: root.pebble && root.pebble.imperialUnits ? 1 : 0
            }
            Button {
                width: parent.width
                text: qsTr("Language")
                onClicked: pageStack.push(Qt.resolvedUrl("LanguagePage.qml"), {pebble: pebble})
            }

            SectionHeader {
                text: qsTr("Timeline")
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Sync calendar to timeline")
                enabled: root.settingsReady
                automaticCheck: false
                checked: root.pebble ? root.pebble.calendarSyncEnabled : false
                onClicked: root.pebble.calendarSyncEnabled =
                           !root.pebble.calendarSyncEnabled
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Sync Apps from Cloud")
                enabled: root.settingsReady
                automaticCheck: false
                checked: root.pebble ? root.pebble.syncAppsFromCloud : false
                onClicked: root.pebble.syncAppsFromCloud =
                           !root.pebble.syncAppsFromCloud
            }
            Button {
                width: parent.width
                text: qsTr("Reset Timeline")
                enabled: pebble && pebble.connected
                onClicked: pebble.resetTimeline()
            }
            TextField {
                id: timelineWindowStartField

                width: parent.width
                enabled: root.pebble && root.pebble.timelineWindowReady
                label: qsTr("Timeline Window Start (days ago)")
                placeholderText: label
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: IntValidator { bottom: -365; top: -1 }
                onTextChanged: if (activeFocus && !root.loadingTimelineWindow) {
                    root.timelineWindowDirty = true
                }
            }
            TextField {
                id: timelineWindowEndField

                width: parent.width
                enabled: root.pebble && root.pebble.timelineWindowReady
                label: qsTr("Timeline Window End (days ahead)")
                placeholderText: label
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: IntValidator { bottom: -365; top: 365 }
                onTextChanged: if (activeFocus && !root.loadingTimelineWindow) {
                    root.timelineWindowDirty = true
                }
            }
            TextField {
                id: timelineWindowFadeField

                width: parent.width
                enabled: root.pebble && root.pebble.timelineWindowReady
                label: qsTr("Notification re-delivery expiration (seconds)")
                placeholderText: label
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: IntValidator { bottom: -2592000; top: 2592000 }
                onTextChanged: if (activeFocus && !root.loadingTimelineWindow) {
                    root.timelineWindowDirty = true
                }
            }
            Button {
                width: parent.width
                text: qsTr("Set Timeline Window")
                onClicked: {
                    root.pebble.setTimelineWindow(
                                Number(timelineWindowStartField.text),
                                Number(timelineWindowFadeField.text),
                                Number(timelineWindowEndField.text))
                    root.timelineWindowDirty = false
                }
                enabled: root.pebble && root.pebble.timelineWindowReady
                         && root.timelineWindowDirty
                         && timelineWindowStartField.acceptableInput
                         && timelineWindowFadeField.acceptableInput
                         && timelineWindowEndField.acceptableInput
                         && Number(timelineWindowStartField.text)
                            <= Number(timelineWindowEndField.text)
                         && timelineWindowStartField.text.length > 0
                         && timelineWindowFadeField.text.length > 0
                         && timelineWindowEndField.text.length > 0
            }

            SectionHeader {
                text: qsTr("Active Timeline WebSync account")
            }
            Label {
                width: parent.width
                visible: pebble && accountAuthenticated
                text: visible ? pebble.accountName : ""
            }
            Label {
                width: parent.width
                visible: pebble && accountAuthenticated
                text: visible ? pebble.accountEmail : ""
            }
            Button {
                width: parent.width
                enabled: pebble && !pebble.accountTokenPending
                text: accountAuthenticated ? qsTr("Logout") : qsTr("Login")
                onClicked: if(accountAuthenticated) {
                               pebble.setOAuthToken("");
                           } else {
                               pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), {
                                              url: "https://boot.rebble.io",
                                              pebble: pebble,
                                              oauthBootFlow: true
                                          })
                           }
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: pebble && pebble.accountTokenPending
                size: BusyIndicatorSize.Small
                visible: running
            }
            Label {
                width: parent.width
                visible: pebble && pebble.accountTokenError.length > 0
                text: visible ? pebble.accountTokenError : ""
                color: Theme.errorColor
                wrapMode: Text.Wrap
            }

            SectionHeader {
                text: qsTr("Automatic Profile")
            }
            ComboBox {
                width: parent.width
                label: qsTr("Connected")
                enabled: root.settingsReady
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("no change")
                        down: root.connectedProfile === ""
                        onClicked: root.pebble.profileWhenConnected = ""
                    }
                    Repeater {
                        model: rockPool.sysProfiles
                        delegate: MenuItem {
                            text: modelData
                            down: modelData === root.connectedProfile
                            onClicked: root.pebble.profileWhenConnected = modelData
                        }
                    }
                }
                value: root.connectedProfile === "" ? qsTr("no change") : root.connectedProfile
            }
            ComboBox {
                width: parent.width
                label: qsTr("Disconnected")
                enabled: root.settingsReady
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("no change")
                        down: root.disconnectedProfile === ""
                        onClicked: root.pebble.profileWhenDisconnected = ""
                    }
                    Repeater {
                        model: rockPool.sysProfiles
                        delegate: MenuItem {
                            text: modelData
                            down: modelData === root.disconnectedProfile
                            onClicked: root.pebble.profileWhenDisconnected = modelData
                        }
                    }
                }
                value: root.disconnectedProfile === "" ? qsTr("no change") : root.disconnectedProfile
            }
            SectionHeader {
                text: qsTr("Canned Messages")
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: !root.cannedResponsesReady
                visible: running
            }
            Repeater {
                model: root.cannedResponsesReady ? Object.keys(cannedResponses) : []
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
                                           list: cannedResponses[modelData].slice(0)})
                    }
                }
            }
        }
        Component.onCompleted: rockPool.getProfiles()
    }
}
