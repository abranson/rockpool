import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    property string watchLogFile: StandardPaths.home + "/Downloads/pebble.log"

    SilicaListView {
        header: PageHeader {
            title: qsTr("Developer Tools")
        }
        model: devMenuModel
        anchors.fill: parent
        delegate: ListItem {
            contentHeight: Theme.itemSizeSmall
            Row {
                height: Theme.itemSizeSmall
                width: parent.width
                spacing: Theme.paddingSmall
                Image {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "image://theme/icon-s-"+model.icon
                }
                Label {
                    text: model.text
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            onClicked: {
                if (model.page) {
                    pageStack.push(Qt.resolvedUrl(model.page), {pebble: root.pebble})
                } else if (model.call) {
                    _menu_calls[model.call]()
                }
            }
        }
    }
    property var _menu_calls: {
        "stop": function(){rockPool.stopService()},
        "restart": function(){rockPool.restartService()},
        "logs": function(){if(devConnDocker.open){devConnDocker.hide()};sendLogsDocker.show()},
        "dcon": function(){if(sendLogsDocker.open){sendLogsDocker.hide()};devConnDocker.show()}
    }

    //Creating the menu list this way to allow the text field to be translatable (http://askubuntu.com/a/476331)
    ListModel {
        id: devMenuModel
        dynamicRoles: true
    }

    Component.onCompleted: {
        populateDevMenu();
        if (root.pebble) {
            root.pebble.refreshDeveloperSettings()
        }
    }
    function populateDevMenu() {
        devMenuModel.clear();

        devMenuModel.append({
            icon: "low-importance",
            text: qsTr("Disable Service"),
            page: "",
            call: "stop"
        });
        devMenuModel.append({
            icon: "sync",
            text: qsTr("Restart Service"),
            page: "",
            call: "restart"
        });
        devMenuModel.append({
            icon: "time",
            text: qsTr("Screenshots"),
            page: "ScreenshotsPage.qml",
            call: null
        });
        devMenuModel.append({
            icon: "developer",
            text: qsTr("Developer Connection"),
            page: "",
            call: "dcon"
        });
        devMenuModel.append({
            icon: "task",
            text: qsTr("Report problem"),
            page: "",
            call: "logs"
        });
        devMenuModel.append({
            icon: "device-upload",
            text: qsTr("Install app or watchface from file"),
            page: "ImportPackagePage.qml",
            call: null
        });
    }

    DockedPanel {
        id: devConnDocker
        width: parent.width
        height: devContent.childrenRect.height
        dock: Dock.Bottom
        open: false
        //z:5
        Rectangle {
            anchors.fill: parent
            color: "black"
            opacity: 0.66
        }

        Column {
            id: devContent
            width: parent.width
            SectionHeader {
                text: qsTr("Developer Connection Settings")
            }

            IconTextSwitch {
                width: parent.width
                text: qsTr("Enable Connection")
                icon.source: "image://theme/icon-s-high-importance"
                description: qsTr("Enable Developer Connection Service")
                enabled: root.pebble && root.pebble.connected
                         && root.pebble.developerSettingsReady
                automaticCheck: false
                checked: root.pebble ? root.pebble.devConnEnabled : false
                onClicked: {
                    if (root.pebble && root.pebble.connected
                            && root.pebble.developerSettingsReady) {
                        root.pebble.devConnEnabled = !root.pebble.devConnEnabled
                    }
                }
            }
            SectionHeader {
                text: qsTr("Runtime Status")
            }
            TextSwitch {
                width: parent.width
                automaticCheck: false
                text: qsTr("DeveloperConnection Status")
                description: qsTr("DeveloperConnection port listening state")
                enabled: root.pebble && root.pebble.connected
                         && root.pebble.developerSettingsReady
                checked: root.pebble ? root.pebble.devConnServerRunning : false
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Close")
                onClicked: devConnDocker.hide()
            }
        }
    }

    DockedPanel {
        id: sendLogsDocker
        width: parent.width
        height: content.childrenRect.height
        dock: Dock.Bottom
        open: false
        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingSmall
            Label {
                text: qsTr("Report problem")
                font.pixelSize: Theme.fontSizeLarge
                horizontalAlignment: Text.AlignRight
                width: parent.width
                color: Theme.secondaryHighlightColor
            }
            BusyIndicator {
                id: busyIndicator
                visible: false
                running: visible
                anchors.horizontalCenter: parent.horizontalCenter
            }
            Label {
                text: qsTr("Preparing logs package...")
                visible: busyIndicator.visible
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                font.pixelSize: Theme.fontSizeLarge
            }

            Connections {
                target: root.pebble
                onLogsDumped: {
                    busyIndicator.visible = false
                    if (success) {
                        pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                            itemName: "pebble.log",
                            itemDescription: "Platform "+pebble.name+" ("+pebble.hardwarePlatform+") "+pebble.softwareVersion,
                            contentType: "text/plain",
                            filename: root.watchLogFile
                        })
                    }
                    sendLogsDocker.hide()
                }
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Debug logging")
                description: qsTr("Write debug messages to the system journal")
                enabled: root.pebble && root.pebble.developerSettingsReady
                automaticCheck: false
                checked: root.pebble ? root.pebble.logLevel === 0 : false
                onClicked: {
                    if (root.pebble && root.pebble.developerSettingsReady) {
                        root.pebble.logLevel = root.pebble.logLevel === 0 ? 1 : 0
                    }
                }
            }

            Button {
                text: qsTr("Send watch logs")
                visible: !busyIndicator.visible
                enabled: root.pebble && root.pebble.connected
                width: parent.width
                onClicked: {
                    busyIndicator.visible = true
                    root.pebble.dumpLogs(root.watchLogFile)
                }
            }

            Button {
                text: qsTr("Cancel")
                visible: !busyIndicator.visible
                width: parent.width
                onClicked: {
                    sendLogsDocker.hide()
                }
            }
        }
    }
}
