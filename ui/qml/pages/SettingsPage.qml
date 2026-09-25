import QtQuick 2.6
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

    property var pendingLanguage

    function showLanguageRemorse() {
        if (status !== PageStatus.Active || !pendingLanguage) {
            return
        }
        var selection = pendingLanguage
        pendingLanguage = null
        languageRemorse.execute(qsTr("Changing watch language to %1").arg(selection.name), function() {
            if (root.status === PageStatus.Active && root.pebble === selection.watch
                    && root.pebble.connected && root.pebble.languageVersion !== selection.version) {
                root.pebble.loadLanguagePack(selection.file)
            }
        })
    }

    onStatusChanged: if (status === PageStatus.Active) showLanguageRemorse()

    RemorsePopup {
        id: languageRemorse
    }

    Timer {
        id: languageRemorseTimer

        interval: 0
        onTriggered: root.showLanguageRemorse()
    }

    Component.onCompleted: {
        if (root.pebble) {
            root.pebble.refreshSettingsPage()
            root.pebble.refreshCannedResponses()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Reset Timeline")
                enabled: root.pebble && root.pebble.connected
                onClicked: root.pebble.resetTimeline()
            }
            MenuItem {
                text: qsTr("Login")
                visible: !root.accountAuthenticated
                enabled: root.pebble && !root.pebble.accountTokenPending
                onClicked: pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), {
                                             url: "https://boot.rebble.io",
                                             pebble: root.pebble,
                                             oauthBootFlow: true
                                         })
            }
        }

        VerticalScrollDecorator {}

        Column {
            id: content

            width: parent.width

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
            WatchLanguageSelector {
                pebble: root.pebble
                onLanguageSelected: {
                    root.pendingLanguage = {watch: root.pebble, file: file, name: name, version: version}
                    // A full-screen picker returns here before showing remorse.
                    // For an inline menu, defer until its click handler has finished.
                    languageRemorseTimer.restart()
                }
            }

            BackgroundItem {
                id: quietTimeItem

                width: parent.width
                enabled: root.pebble !== null
                onClicked: pageStack.push(Qt.resolvedUrl("QuietTimePage.qml"), {
                                             pebble: root.pebble
                                         })

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x - quietTimeArrow.width - Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Quiet Time")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }
                Icon {
                    id: quietTimeArrow

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-right"
                    highlighted: quietTimeItem.highlighted
                }
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
            BackgroundItem {
                id: timelineWindowItem

                width: parent.width
                enabled: root.pebble !== null
                onClicked: pageStack.push(Qt.resolvedUrl("TimelineSettingsDialog.qml"), {
                                             pebble: root.pebble
                                         })

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x - timelineArrow.width - Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Timeline window")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }
                Icon {
                    id: timelineArrow

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-right"
                    highlighted: timelineWindowItem.highlighted
                }
            }

            SectionHeader {
                text: qsTr("Rebble account")
            }
            ListItem {
                id: accountItem

                width: parent.width
                contentHeight: accountDetails.height + 2 * Theme.paddingMedium
                enabled: root.accountAuthenticated && root.pebble && !root.pebble.accountTokenPending
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Logout")
                        onClicked: root.pebble.setOAuthToken("")
                    }
                }

                Column {
                    id: accountDetails

                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    y: Theme.paddingMedium
                    spacing: Theme.paddingSmall

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: root.accountAuthenticated ? root.pebble.accountName : qsTr("Not signed in")
                        color: accountItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        visible: root.accountAuthenticated
                        text: visible ? root.pebble.accountEmail : ""
                        color: accountItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                    }
                }
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: pebble && pebble.accountTokenPending
                size: BusyIndicatorSize.Small
                visible: running
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
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
                    id: responseItem

                    width: content.width

                    Row {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.itemSizeSmall
                        spacing: Theme.paddingMedium
                        Image {
                            id: responseIcon

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
                            width: parent.width - responseIcon.width - parent.spacing
                            truncationMode: TruncationMode.Fade
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData in notificationsMap)?notificationsMap[modelData]["name"]:modelData
                            color: responseItem.highlighted ? Theme.highlightColor : Theme.primaryColor
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
