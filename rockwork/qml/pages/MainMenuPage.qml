import QtQuick 2.2
import Sailfish.Silica 1.0
import QtGraphicalEffects 1.0

Page {
    id: root

    property var pebble: null
    allowedOrientations: Orientation.All

    function connectionStatusText() {
        switch (root.pebble ? root.pebble.connectionState : 0) {
        case 1: return qsTr("Connecting…")
        case 2: return qsTr("Negotiating…")
        case 3: return qsTr("Connected")
        case 4: return qsTr("Connection failed")
        default: return qsTr("Disconnected")
        }
    }

    //Creating the menu list this way to allow the text field to be translatable (http://askubuntu.com/a/476331)
    ListModel {
        id: mainMenuModel
        dynamicRoles: true
    }

    SilicaFlickable {
        PullDownMenu {
            MenuItem {
                text: qsTr("Health history")
                visible: root.pebble && !root.pebble.connected
                onClicked: pageStack.push(
                               Qt.resolvedUrl("HealthHistoryPage.qml"),
                               { pebble: root.pebble })
            }
            MenuItem {
                // With a single watch, rockpool.qml pushes this page straight onto a cleared
                // stack, so the watch list is otherwise unreachable — and with it, pairing and
                // forgetting.
                text: qsTr("Watch manager")
                onClicked: {
                    pageStack.push(Qt.resolvedUrl("PebblesPage.qml"))
                }
            }
            MenuItem {
                text: qsTr("About")
                onClicked: {
                    pageStack.push(Qt.resolvedUrl("InfoPage.qml"))
                }
            }
            MenuItem {
                text: qsTr("Developer tools")
                onClicked: {
                    pageStack.push(Qt.resolvedUrl("DeveloperToolsPage.qml"), {
                                       pebble: root.pebble
                                   })
                }
            }
        }
        anchors.fill: parent
        Column {
            anchors.fill: parent
            PageHeader {
                id: hdr
                title: pebble.name
                description: modelModel.getOrFallback(root.pebble.model).modelName
            }
            Grid {
                id: watchMenu

                property real cellWidth: (width - spacing * (columns - 1)) / columns

                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                columns: parent.width > parent.height ? 2 : 1
                spacing: Theme.paddingLarge

                Row {
                    spacing: Theme.paddingLarge
                    height: watchImage.height
                    width: watchMenu.cellWidth

                    Item {
                        width: watchImage.width
                        height: watchImage.height
                        MouseArea {
                            width: watchImage.width
                            height: watchImage.height

                            property var watchModel: modelModel.getOrFallback(root.pebble.model)

                            Image {
                                id: watchImage
                                fillMode: Image.PreserveAspectFit
                                anchors.centerIn: parent
                                source: parent.watchModel.image
                                height: (sourceSize.height ? sourceSize.height : 350)
                                width: (sourceSize.width ? sourceSize.width : 251)
                            }

                            Image {
                                id: image
                                anchors.centerIn: parent
                                source: "file://" + root.pebble.screenshots.latestScreenshot
                                fillMode: Image.PreserveAspectFit
                                width: parent.watchModel.screenWidth
                                height: parent.watchModel.screenHeight
                                visible: false
                            }
                            Component.onCompleted: {
                                if (root.pebble.connected
                                        && !root.pebble.screenshots.latestScreenshot) {
                                    root.pebble.requestScreenshot()
                                }
                            }
                            OpacityMask {
                                anchors.centerIn: parent
                                width: maskRect.width
                                height: maskRect.height
                                source: image
                                maskSource: maskRect
                                cached: true
                            }
                            Rectangle {
                                id: maskRect
                                width: image.width
                                height: image.height
                                anchors.centerIn: parent
                                color: "transparent"
                                visible: false
                                property bool isRound: parent.watchModel.shape === "round"
                                Rectangle {
                                    color: "blue"
                                    anchors.centerIn: parent
                                    height: image.height
                                    width: parent.isRound ? height : parent.width
                                    radius: parent.isRound ? height / 2 : 0
                                }
                            }

                            onClicked: pageStack.push(Qt.resolvedUrl("ScreenshotsPage.qml"), {
                                                          pebble: root.pebble
                                                      })
                        }
                    }
                    Column {
                        spacing: Theme.paddingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - watchImage.width - parent.spacing

                        Image {
                            height: Theme.iconSizeSmall
                            width: height
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-lock-"
                                    + (root.pebble.connected ? "transfer" : "warning")
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.connectionStatusText()
                        }
                        Label {
                            width: parent.width
                            text: root.pebble.softwareVersion
                                  ? qsTr("Firmware %1").arg(root.pebble.softwareVersion)
                                  : ""
                            visible: text.length > 0
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                        }
                        Image {
                            source: "image://theme/icon-lock-application-update"
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.pebble.connected && root.pebble.firmwareUpgradeAvailable && !root.pebble.upgradingFirmware
                        }
                        Label {
                            text: qsTr("Update Available")
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.pebble.connected && root.pebble.firmwareUpgradeAvailable && !root.pebble.upgradingFirmware
                        }

                        Image {
                            id: upgradeIcon
                            height: Theme.iconSizeMedium
                            width: height
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-m-sync"

                            RotationAnimation on rotation {
                                duration: 2000
                                loops: Animation.Infinite
                                from: 0
                                to: 360
                                running: upgradeIcon.visible
                            }
                            visible: root.pebble.connected && root.pebble.upgradingFirmware
                        }
                        Label {
                            text: qsTr("Upgrading...")
                            font.pixelSize: Theme.fontSizeLarge
                            anchors.horizontalCenter: parent.horizontalCenter
                            visible: root.pebble.connected && root.pebble.upgradingFirmware
                        }
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-s-developer"
                            visible: root.pebble.developerSettingsReady
                                     && root.pebble.devConnServerRunning
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: qsTr("Running")
                            visible: root.pebble.developerSettingsReady
                                     && root.pebble.devConnServerRunning
                        }
                    }
                }

                Column {
                    width: watchMenu.cellWidth
                    spacing: menuRepeater.count > 0 ? 0 : Theme.paddingSmall

                    Label {
                        text: qsTr("Your Pebble smartwatch is disconnected. Please make sure it is powered on and within range.")
                        width: parent.width
                        anchors.horizontalCenter: parent.horizontalCenter
                        wrapMode: Text.WordWrap
                        visible: root.pebble && (root.pebble.connectionState === 0
                                                 || root.pebble.connectionState === 4)
                        font.pixelSize: Theme.fontSizeLarge
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Label {
                        text: qsTr("Your Pebble smartwatch is in factory mode and needs to be initialized.")
                        width: parent.width
                        anchors.horizontalCenter: parent.horizontalCenter
                        wrapMode: Text.WordWrap
                        visible: root.pebble && root.pebble.connected && root.pebble.recovery && !root.pebble.upgradingFirmware
                        font.pixelSize: Theme.fontSizeLarge
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Button {
                        text: qsTr("Initialize Pebble")
                        onClicked: root.pebble.performFirmwareUpgrade()
                        visible: root.pebble && root.pebble.connected && root.pebble.recovery && !root.pebble.upgradingFirmware
                        anchors.horizontalCenter: parent.horizontalCenter
                    }


                    Repeater {
                        id: menuRepeater
                        model: root.pebble && root.pebble.connected && !root.pebble.recovery && !root.pebble.upgradingFirmware ? mainMenuModel : null
                        delegate: ListItem {
                            contentHeight: Theme.iconSizeMedium + Theme.paddingSmall*2

                            Row {
                                height: Theme.iconSizeMedium
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: Theme.paddingMedium
                                anchors.rightMargin: Theme.paddingMedium
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.paddingMedium

                                Image {
                                    id: menuIcon

                                    width: Theme.iconSizeMedium
                                    height: width
                                    source: "image://theme/" + model.icon
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    width: parent.width - menuIcon.width - parent.spacing
                                    text: model.text
                                    anchors.verticalCenter: parent.verticalCenter
                                    truncationMode: TruncationMode.Fade
                                }
                            }

                            onClicked: {
                                var options = {}
                                options["pebble"] = root.pebble
                                var modelItem = mainMenuModel.get(index)
                                options["showWatchApps"] = modelItem.showWatchApps
                                options["showWatchFaces"] = modelItem.showWatchFaces
                                pageStack.push(Qt.resolvedUrl(model.page), options)
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: pebble
        onOpenURL: {
            if (url) {
                pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), {
                                   uuid: uuid,
                                   url: url,
                                   pebble: pebble
                               })
            }
        }
    }
    Connections {
        target: root.pebble
        onConnectedChanged: {
            if (root.pebble.connected && !root.pebble.screenshots.latestScreenshot) {
                root.pebble.requestScreenshot()
            }
        }
        onFirmwareUpgradeAvailableChanged: {
            populateMainMenu()
        }
    }

    Component.onCompleted: {
        populateMainMenu();
        if (root.pebble) {
            root.pebble.refreshDeveloperSettings()
        }
    }

    function populateMainMenu() {
        mainMenuModel.clear()

        mainMenuModel.append({
                                 icon: "icon-m-alarm",
                                 text: qsTr("Notifications"),
                                 page: "NotificationsPage.qml"
                             })
        mainMenuModel.append({
                                 icon: "icon-m-favorite",
                                 text: qsTr("Health history"),
                                 page: "HealthHistoryPage.qml"
                             })
        mainMenuModel.append({
                                 icon: "icon-m-toy",
                                 text: qsTr("Watch Apps"),
                                 page: "InstalledAppsPage.qml",
                                 showWatchApps: true
                             })
        mainMenuModel.append({
                                 icon: "icon-m-watch",
                                 text: qsTr("Watchfaces"),
                                 page: "InstalledAppsPage.qml",
                                 showWatchFaces: true
                             })
        mainMenuModel.append({
                                 icon: "icon-m-developer-mode",
                                 text: qsTr("Settings"),
                                 page: "SettingsPage.qml",
                                 showWatchFaces: true
                             })

        mainMenuModel.append({
                                 icon: "icon-m-up",
                                 text: qsTr("Firmware"),
                                 page: "FirmwareUpgradePage.qml"
                             })
    }

    PebbleModels {
        id: modelModel
    }
}
