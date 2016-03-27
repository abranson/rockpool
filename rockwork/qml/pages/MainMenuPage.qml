import QtQuick 2.2
import Sailfish.Silica 1.0
import QtGraphicalEffects 1.0

Page {
    id: root
    property var pebble: null
    allowedOrientations: Orientation.All

    //Creating the menu list this way to allow the text field to be translatable (http://askubuntu.com/a/476331)
    ListModel {
        id: mainMenuModel
        dynamicRoles: true
    }

    SilicaFlickable {
        PullDownMenu {
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
            }
            Grid {
                id: watchMenu
                width: parent.width
                columns: parent.width > parent.height ? 2 : 1
                spacing: Theme.paddingSmall
                Row {
                    anchors.margins: Theme.paddingSmall
                    spacing: Theme.paddingSmall
                    height: watchImage.height
                    width: parent.width/parent.columns - Theme.paddingSmall
                    Item {
                        width: watchImage.width
                        height: watchImage.height
                        Image {
                            id: watchImage
                            fillMode: Image.PreserveAspectFit
                            anchors.centerIn: parent
                            source: modelModel.get(root.pebble.model).image
                            height: (sourceSize.height ? sourceSize.height : 350)
                            width: (sourceSize.width ? sourceSize.width : 251)
                        }

                        Image {
                            id: image
                            anchors.centerIn: parent
                            source: "file://" + root.pebble.screenshots.latestScreenshot
                            fillMode: Image.PreserveAspectFit
                            visible: false
                        }
                        Component.onCompleted: {
                            if (!root.pebble.screenshots.latestScreenshot) {
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
                            property bool isRound: modelModel.get(root.pebble.model - 1).shape === "round"
                            Rectangle {
                                color: "blue"
                                anchors.centerIn: parent
                                height: image.height
                                width: parent.isRound ? height : height * 0.9
                                radius: parent.isRound ? height / 2 : 0
                            }
                        }
                    }
                    Column {
                        spacing: Theme.paddingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width-watchImage.width
                        Image {
                            height: Theme.iconSizeSmall
                            width: height
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-lock-"
                                    + (root.pebble.connected ? "transfer" : "warning")
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.pebble.connected ? qsTr("Connected") : qsTr("Disconnected")
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
                    }
                }

                Column {
                    //width: childrenRect.width
                    width: parent.width / parent.columns - Theme.paddingSmall
                    spacing: menuRepeater.count > 0 ? 0 : Theme.paddingSmall
                    Label {
                        text: qsTr("Your Pebble smartwatch is disconnected. Please make sure it is powered on, within range and it is paired properly in the Bluetooth System Settings.")
                        width: parent.width
                        anchors.horizontalCenter: parent.horizontalCenter
                        wrapMode: Text.WordWrap
                        visible: !root.pebble.connected
                        font.pixelSize: Theme.fontSizeLarge
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Button {
                        text: qsTr("Open Bluetooth Settings")
                        visible: !root.pebble.connected
                        onClicked: rockPool.startBT()
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    Label {
                        text: qsTr("Your Pebble smartwatch is in factory mode and needs to be initialized.")
                        width: parent.width
                        anchors.horizontalCenter: parent.horizontalCenter
                        wrapMode: Text.WordWrap
                        visible: root.pebble.connected && root.pebble.recovery && !root.pebble.upgradingFirmware
                        font.pixelSize: Theme.fontSizeLarge
                        horizontalAlignment: Text.AlignHCenter
                    }
                    Button {
                        text: qsTr("Initialize Pebble")
                        onClicked: root.pebble.performFirmwareUpgrade()
                        visible: root.pebble.connected && root.pebble.recovery && !root.pebble.upgradingFirmware
                        anchors.horizontalCenter: parent.horizontalCenter
                    }


                    Repeater {
                        id: menuRepeater
                        model: root.pebble.connected && !root.pebble.recovery && !root.pebble.upgradingFirmware ? mainMenuModel : null
                        delegate: ListItem {
                            contentHeight: Theme.iconSizeMedium + Theme.paddingSmall*2
                            Row {
                                height: Theme.iconSizeMedium
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.paddingSmall
                                Image {
                                    source: "image://theme/" + model.icon
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label {
                                    text: model.text
                                    anchors.verticalCenter: parent.verticalCenter
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
        onFirmwareUpgradeAvailableChanged: {
            populateMainMenu()
        }
    }

    Component.onCompleted: {
        populateMainMenu();
    }

    function populateMainMenu() {
        mainMenuModel.clear()

        mainMenuModel.append({
                                 icon: "icon-m-alarm",
                                 text: qsTr("Manage notifications"),
                                 page: "NotificationsPage.qml"
                             })
        mainMenuModel.append({
                                 icon: "icon-m-toy",
                                 text: qsTr("Manage Apps"),
                                 page: "InstalledAppsPage.qml",
                                 showWatchApps: true
                             })
        mainMenuModel.append({
                                 icon: "icon-m-watch",
                                 text: qsTr("Manage Watchfaces"),
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
                                 text: qsTr("Manage Firmware"),
                                 page: "FirmwareUpgradePage.qml"
                             })
    }

    PebbleModels {
        id: modelModel
    }
}
