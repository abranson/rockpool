import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null

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
                onCheckedChanged: root.pebble.devConnEnabled=checked
                checked: root.pebble.devConnEnabled
            }
            Row {
                width: parent.width
                TextField {
                    id: devConPort
                    property bool changed: false
                    width: parent.width / 2 - Theme.paddingSmall
                    label: qsTr("Listen Port")
                    placeholderText: "9000"
                    validator: IntValidator {bottom: 1025; top: 65535}
                    inputMethodHints: Qt.ImhDigitsOnly
                    onTextChanged: changed = true
                    color: errorHighlight? "red" : Theme.primaryColor
                    Component.onCompleted: {
                        text = root.pebble.devConListenPort
                        changed = false
                    }
                }
                Button {
                    width: parent.width / 2 - Theme.paddingSmall
                    text: qsTr("Apply")
                    enabled: devConPort.changed && !devConPort.acceptableInput
                    onClicked: {
                        root.pebble.devConListenPort=devConPort.text
                        devConPort.changed = false
                    }
                }
            }
            IconTextSwitch {
                width: parent.width
                text: qsTr("Enable")+" CloudPebble"
                description: qsTr("Enable DeveloperConnection over CloudPebble")
                icon.source: "image://theme/icon-s-cloud-upload"
                checked: root.pebble.devConnCloudEnabled
                onCheckedChanged: root.pebble.devConnCloudEnabled=checked
            }
            SectionHeader {
                text: qsTr("Runtime Status")
            }
            TextSwitch {
                width: parent.width
                automaticCheck: false
                text: qsTr("DeveloperConnection Status")
                description: qsTr("DeveloperConnection port listening state")
                checked: root.pebble.devConnServerRunning
            }
            TextSwitch {
                width: parent.width
                automaticCheck: false
                text: qsTr("CloudPebble Status")
                description: qsTr("Indicates CloudPebble connection state")
                checked: root.pebble.devConCloudConnected
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
                text: qsTr("Preparing logs package…")
                visible: busyIndicator.visible
                horizontalAlignment: Text.AlignHCenter
                width: parent.width
                font.pixelSize: Theme.fontSizeLarge
            }

            Connections {
                target: root.pebble
                onLogsDumped: {
                    if (success) {
                        var filename = "/tmp/pebble.log"
                        pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                            itemName: "pebble.log",
                            itemDescription: "Platform "+pebble.name+" ("+pebble.hardwarePlatform+") "+pebble.softwareVersion,
                            contentType: "text/plain",
                            filename: filename
                        })
                    }
                    sendLogsDocker.hide()
                }
            }

            Label {
                id: currentLog
                text: qsTr("Current log: ") + pebble.dumpLogFile;
                width: parent.width
                visible: pebble.dumpLogFile !== ""
            }

            Button {
                text: pebble.isLogDumping ? qsTr("Disable service logs") : qsTr("Enable service logs")
                width: parent.width
                onClicked: {
                    var file = pebble.isLogDumping ? pebble.stopLogDump() : pebble.startLogDump();
                    console.log("Toggling log to",file);
                    sendLogsDocker.hide()
                }
            }
            Button {
                text: qsTr("Send service logs")
                width: parent.width
                visible: pebble.dumpLogFile !== ""
                onClicked: {
                    if(pebble.isLogDumping) // stop and flush logs
                        pebble.stopLogDump();
                    pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                           itemName: "rockpoold.log",
                           itemDescription: "RockPool Daemon "+version,
                           contentType: "text/plain",
                           filename: pebble.dumpLogFile
                    })

                    sendLogsDocker.hide()
                }
            }
            ComboBox {
                width: parent.width
                label: qsTr("Syslog Verbosity")
                menu: ContextMenu {
                    MenuItem { text: qsTr("Debug") }
                    MenuItem { text: qsTr("Warning") }
                    MenuItem { text: qsTr("Critical") }
                }
                currentIndex: pebble.logLevel
                onValueChanged: pebble.logLevel = currentIndex
            }

            Button {
                text: qsTr("Send watch logs")
                visible: !busyIndicator.visible
                width: parent.width
                onClicked: {
                    busyIndicator.visible = true
                    root.pebble.dumpLogs("/tmp/pebble.log")
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

