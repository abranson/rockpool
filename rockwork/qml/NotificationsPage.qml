import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import RockWork 1.0

Page {
    id: root
    title: i18n.tr("Notifications")

    property var pebble: null

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: units.gu(1)

        Item {
            Layout.fillWidth: true
            implicitHeight: infoLabel.height

            Label {
                id: infoLabel
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: units.gu(2)
                }

                wrapMode: Text.WordWrap
                text: i18n.tr("Entries here will be added as notifications appear on the phone. Selected notifications will be shown on your Pebble smartwatch.")
            }
        }


        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.pebble.notifications

            delegate: ListItem {
                ListItemLayout {
                    title.text: model.name

                    UbuntuShape {
                        SlotsLayout.position: SlotsLayout.Leading;
                        height: units.gu(5)
                        width: height
                        backgroundColor: {
                            // Add some hacks for known icons
                            switch (model.icon) {
                            case "calendar":
                                return UbuntuColors.orange;
                            case "settings":
                                return "grey";
                            case "dialog-question-symbolic":
                                return UbuntuColors.red;
                            case "alarm-clock":
                                return UbuntuColors.purple;
                            case "gpm-battery-050":
                                return UbuntuColors.green;
                            }
                            return "black"
                        }
                        source: Image {
                            height: parent.height
                            width: parent.width
                            source: model.icon.indexOf("/") === 0 ? "file://" + model.icon : ""
                        }
                        Icon {
                            anchors.fill: parent
                            anchors.margins: units.gu(.5)
                            name: model.icon.indexOf("/") !== 0 ? model.icon : ""
                            color: "white"
                        }
                    }

                    Switch {
                        checked: model.enabled
                        SlotsLayout.position: SlotsLayout.Trailing;
                        onClicked: {
                            root.pebble.setNotificationFilter(model.id, checked)
                        }
                    }
                }
            }
        }
    }
}
