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
        "logs": function(){sendLogsDocker.show()}
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
            icon: "developer",
            text: qsTr("Disable Service"),
            page: "",
            call: "stop"
        });
        devMenuModel.append({
            icon: "time",
            text: qsTr("Screenshots"),
            page: "ScreenshotsPage.qml",
            call: null
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
        id: sendLogsDocker
        width: parent.width
        height: content.childrenRect.height
        dock: Dock.Bottom
        open: false
        Column {
            id: content
            width: parent.width
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
            /* On jolla it's in journald
            Button {
                text: qsTr("Send")+" rockworkd.log"
                width: parent.width
                visible: !busyIndicator.visible
                onClicked: {
                    var filename = homePath + "/.cache/upstart/rockworkd.log"
                    pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                           itemName: "rockpoold.log",
                           itemDescription: "RockPool Daemon "+version,
                           contentType: "text/plain",
                           fileName: filename
                    })

                    sendLogsDocker.hide()
                }
            }
            */
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

