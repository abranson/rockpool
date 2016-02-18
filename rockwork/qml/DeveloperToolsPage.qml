import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3
import Ubuntu.Content 1.3

Page {
    id: root
    title: i18n.tr("Developer Tools")

    property var pebble: null

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
            icon: "camera-app-symbolic",
            text: i18n.tr("Screenshots"),
            page: "ScreenshotsPage.qml",
            dialog: "",
            color: "gold"
        });
        devMenuModel.append({
            icon: "dialog-warning-symbolic",
            text: i18n.tr("Report problem"),
            page: "",
            dialog: sendLogsComponent,
            color: UbuntuColors.red
        });
        devMenuModel.append({
            icon: "stock_application",
            text: i18n.tr("Install app or watchface from file"),
            page: "ImportPackagePage.qml",
            dialog: null,
            color: UbuntuColors.blue
        });

    }

    ColumnLayout {
        anchors.fill: parent

        Repeater {
            id: menuRepeater
            model: devMenuModel
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
                    if (model.page) {
                        pageStack.push(Qt.resolvedUrl(model.page), {pebble: root.pebble})
                    }
                    if (model.dialog) {
                        PopupUtils.open(model.dialog)
                    }
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }

    Component {
        id: sendLogsComponent
        Dialog {
            id: sendLogsDialog
            title: i18n.tr("Report problem")
            ActivityIndicator {
                id: busyIndicator
                visible: false
                running: visible
            }
            Label {
                text: i18n.tr("Preparing logs package...")
                visible: busyIndicator.visible
                horizontalAlignment: Text.AlignHCenter
                fontSize: "large"
            }

            Connections {
                target: root.pebble
                onLogsDumped: {
                    if (success) {
                        var filename = "/tmp/pebble.log"
                        pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {itemName: i18n.tr("pebble.log"),handler: ContentHandler.Share, contentType: ContentType.All, filename: filename })
                    }
                    PopupUtils.close(sendLogsDialog)
                }
            }

            Button {
                text: i18n.tr("Send rockworkd.log")
                color: UbuntuColors.blue
                visible: !busyIndicator.visible
                onClicked: {
                    var filename = homePath + "/.cache/upstart/rockworkd.log"
                    pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {itemName: i18n.tr("rockworkd.log"),handler: ContentHandler.Share, contentType: ContentType.All, filename: filename })
                    PopupUtils.close(sendLogsDialog)
                }
            }
            Button {
                text: i18n.tr("Send watch logs")
                color: UbuntuColors.blue
                visible: !busyIndicator.visible
                onClicked: {
                    busyIndicator.visible = true
                    root.pebble.dumpLogs("/tmp/pebble.log")
                }
            }
            Button {
                text: i18n.tr("Cancel")
                color: UbuntuColors.red
                visible: !busyIndicator.visible
                onClicked: {
                    PopupUtils.close(sendLogsDialog)
                }
            }
        }
    }

}

