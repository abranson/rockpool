import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Share 1.0

Page {
    id: root

    property var pebble: null
    property string watchLogFile: StandardPaths.home + "/Downloads/pebble.log"

    property bool collectingWatchLogs: false
    property bool collectingServiceLogs: false
    property string logExportError

    ShareAction {
        id: logShare

        // AppSupport treats text/* as inline content, not file attachments.
        mimeType: "application/octet-stream"
    }

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
        "logs": function(){if(devConnDocker.open){devConnDocker.hide()};root.logExportError = "";sendLogsDocker.show()},
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
            text: qsTr("Install from file"),
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

                visible: root.collectingWatchLogs || root.collectingServiceLogs
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
                    if (!root.collectingWatchLogs) return
                    root.collectingWatchLogs = false
                    if (success) {
                        logShare.title = qsTr("Send watch logs")
                        logShare.resources = [root.watchLogFile]
                        logShare.trigger()
                        sendLogsDocker.hide()
                    } else {
                        root.logExportError = qsTr("Could not export watch logs. Please try again.")
                    }
                }
                onServiceLogsDumped: {
                    if (!root.collectingServiceLogs) return
                    root.collectingServiceLogs = false
                    if (filename !== "") {
                        logShare.title = qsTr("Send service logs")
                        logShare.resources = [filename]
                        logShare.trigger()
                        sendLogsDocker.hide()
                    } else {
                        root.logExportError = qsTr("Could not export service logs. Please try again.")
                    }
                }
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.logExportError
                visible: text !== ""
                wrapMode: Text.Wrap
                color: Theme.errorColor
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Debug logging")
                description: qsTr("Include debug messages in service logs")
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
                text: qsTr("Send service logs")
                visible: !busyIndicator.visible
                enabled: root.pebble !== null
                width: parent.width
                onClicked: {
                    root.logExportError = ""
                    root.collectingServiceLogs = true
                    root.pebble.dumpServiceLogs()
                }
            }

            Button {
                text: qsTr("Send watch logs")
                visible: !busyIndicator.visible
                enabled: root.pebble && root.pebble.connected
                width: parent.width
                onClicked: {
                    root.logExportError = ""
                    root.collectingWatchLogs = true
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
