import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3

Page {
    id: root
    title: pebble.name

    property var pebble: null

    head {
        actions: [
            Action {
                iconName: "info"
                text: i18n.tr("About")
                onTriggered: {
                    pageStack.push(Qt.resolvedUrl("InfoPage.qml"))
                }
            },
            Action {
                iconName: "ubuntu-sdk-symbolic"
                text: i18n.tr("Developer tools")
                onTriggered: {
                    pageStack.push(Qt.resolvedUrl("DeveloperToolsPage.qml"), {pebble: root.pebble})
                }
            }
        ]
    }

    //Creating the menu list this way to allow the text field to be translatable (http://askubuntu.com/a/476331)
    ListModel {
        id: mainMenuModel
        dynamicRoles: true
    }

    Component.onCompleted: {
        populateMainMenu();
    }

    Connections {
        target: root.pebble
        onFirmwareUpgradeAvailableChanged: {
            populateMainMenu();
        }
    }

    function populateMainMenu() {
        mainMenuModel.clear();

        mainMenuModel.append({
            icon: "stock_notification",
            text: i18n.tr("Manage notifications"),
            page: "NotificationsPage.qml",
            color: "blue"
        });

        mainMenuModel.append({
            icon: "stock_application",
            text: i18n.tr("Manage Apps"),
            page: "InstalledAppsPage.qml",
            showWatchApps: true,
            color: UbuntuColors.green
        });

        mainMenuModel.append({
            icon: "clock-app-symbolic",
            text: i18n.tr("Manage Watchfaces"),
            page: "InstalledAppsPage.qml",
            showWatchFaces: true,
            color: "black"
        });

        mainMenuModel.append({
            icon: "settings",
            text: i18n.tr("Settings"),
            page: "SettingsPage.qml",
            showWatchFaces: true,
            color: "gold"
        });

        if (root.pebble.firmwareUpgradeAvailable) {
            mainMenuModel.append({
                icon: "preferences-system-updates-symbolic",
                text: i18n.tr("Firmware upgrade"),
                page: "FirmwareUpgradePage.qml",
                color: "red"
            });
        }

    }

    PebbleModels {
        id: modelModel
    }

    GridLayout {
        anchors.fill: parent
        columns: parent.width > parent.height ? 2 : 1

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.maximumHeight: units.gu(30)

            RowLayout {
                anchors.fill: parent
                anchors.margins: units.gu(1)
                spacing: units.gu(1)

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Layout.minimumWidth: watchImage.width
                    Image {
                        id: watchImage
                        width: implicitWidth * height / implicitHeight
                        height: parent.height
                        anchors.horizontalCenter: parent.horizontalCenter

                        source:  modelModel.get(root.pebble.model).image
                        fillMode: Image.PreserveAspectFit

                        Item {
                            id: watchFace
                            height: parent.height * (modelModel.get(root.pebble.model - 1).shape === "rectangle" ? .5 : .515)
                            width: height * (modelModel.get(root.pebble.model - 1).shape === "rectangle" ? .85 : 1)
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: units.dp(1)
                            anchors.verticalCenterOffset: units.dp(modelModel.get(root.pebble.model - 1).shape === "rectangle" ? 0 : 1)

                            Image {
                                id: image
                                anchors.fill: parent
                                source: "file://" + root.pebble.screenshots.latestScreenshot
                                visible: false
                            }

                            Component.onCompleted: {
                                if (!root.pebble.screenshots.latestScreenshot) {
                                    root.pebble.requestScreenshot();
                                }
                            }

                            Rectangle {
                                id: textItem
                                anchors.fill: parent
                                layer.enabled: true
                                radius: modelModel.get(root.pebble.model - 1).shape === "rectangle" ? units.gu(.5) : height / 2
                                // This item should be used as the 'mask'
                                layer.samplerName: "maskSource"
                                layer.effect: ShaderEffect {
                                    property var colorSource: image;
                                    fragmentShader: "
                                        uniform lowp sampler2D colorSource;
                                        uniform lowp sampler2D maskSource;
                                        uniform lowp float qt_Opacity;
                                        varying highp vec2 qt_TexCoord0;
                                        void main() {
                                            gl_FragColor =
                                                texture2D(colorSource, qt_TexCoord0)
                                                * texture2D(maskSource, qt_TexCoord0).a
                                                * qt_Opacity;
                                        }
                                    "
                                }
                            }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: units.gu(2)
                    Rectangle {
                        height: units.gu(10)
                        width: height
                        radius: height / 2
                        color: root.pebble.connected ? UbuntuColors.green : UbuntuColors.red

                        Icon {
                            anchors.fill: parent
                            anchors.margins: units.gu(2)
                            color: "white"
                            name: root.pebble.connected ? "tick" : "dialog-error-symbolic"
                        }
                    }

                    Label {
                        text: root.pebble.connected ? i18n.tr("Connected") : i18n.tr("Disconnected")
                        Layout.fillWidth: true
                    }
                }
            }
        }


        Column {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: menuRepeater.count > 0 ? 0 : units.gu(2)
            Label {
                text: i18n.tr("Your Pebble smartwatch is disconnected. Please make sure it is powered on, within range and it is paired properly in the Bluetooth System Settings.")
                width: parent.width - units.gu(4)
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.WordWrap
                visible: !root.pebble.connected
                fontSize: "large"
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                text: i18n.tr("Open System Settings")
                visible: !root.pebble.connected
                onClicked: Qt.openUrlExternally("settings://system/bluetooth")
                color: UbuntuColors.orange
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Label {
                text: i18n.tr("Your Pebble smartwatch is in factory mode and needs to be initialized.")
                width: parent.width - units.gu(4)
                anchors.horizontalCenter: parent.horizontalCenter
                wrapMode: Text.WordWrap
                visible: root.pebble.connected && root.pebble.recovery && !root.pebble.upgradingFirmware
                fontSize: "large"
                horizontalAlignment: Text.AlignHCenter
            }
            Button {
                text: i18n.tr("Initialize Pebble")
                onClicked: root.pebble.performFirmwareUpgrade();
                visible: root.pebble.connected && root.pebble.recovery && !root.pebble.upgradingFirmware
                color: UbuntuColors.orange
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                id: upgradeIcon
                height: units.gu(10)
                width: height
                radius: width / 2
                color: UbuntuColors.orange
                anchors.horizontalCenter: parent.horizontalCenter
                Icon {
                    anchors.fill: parent
                    anchors.margins: units.gu(1)
                    name: "preferences-system-updates-symbolic"
                    color: "white"
                }

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
                text: i18n.tr("Upgrading...")
                fontSize: "large"
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.pebble.connected && root.pebble.upgradingFirmware
            }

            Repeater {
                id: menuRepeater
                model: root.pebble.connected && !root.pebble.recovery && !root.pebble.upgradingFirmware ? mainMenuModel : null
                delegate: ListItem {

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: units.gu(1)

                        UbuntuShape {
                            Layout.fillHeight: true
                            Layout.preferredWidth: height
                            backgroundColor: model.color
                            Icon {
                                anchors.fill: parent
                                anchors.margins: units.gu(.5)
                                name: model.icon
                                color: "white"
                            }
                        }


                        Label {
                            text: model.text
                            Layout.fillWidth: true
                        }
                    }

                    onClicked: {
                        var options = {};
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

    Connections {
        target: pebble
        onOpenURL: {
            if (url) {
                pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), {uuid: uuid, url: url, pebble: pebble})
            }
        }
    }
}
