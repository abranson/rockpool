import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    readonly property bool updateAvailable: pebble && pebble.firmwareUpgradeAvailable
    readonly property string releaseNotes: pebble ? pebble.firmwareReleaseNotes.trim() : ""

    SilicaFlickable {
        id: flickable

        anchors.fill: parent
        contentHeight: contentColumn.height

        PullDownMenu {
            MenuItem {
                text: qsTr("PebbleOS changelog")
                onClicked: Qt.openUrlExternally("https://ndocs.repebble.com/pebbleos-changelog")
            }
        }

        VerticalScrollDecorator {}

        Column {
            id: contentColumn

            width: parent.width

            PageHeader {
                id: header

                title: qsTr("Firmware")
            }

            Item {
                width: parent.width
                height: firmwareContent.height + 2 * Theme.paddingLarge

                Column {
                    id: firmwareContent

                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    y: Theme.paddingLarge
                    spacing: Theme.paddingLarge

                    Label {
                        width: parent.width
                        text: root.updateAvailable ? qsTr("Update available") : qsTr("Firmware is Up-To-Date")
                        font.pixelSize: Theme.fontSizeLarge
                        color: Theme.highlightColor
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: parent.width
                        text: qsTr("Currently installed firmware: %1").arg(root.pebble ? root.pebble.softwareVersion : "")
                        color: Theme.secondaryColor
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: parent.width
                        text: qsTr("Candidate firmware version: %1").arg(root.pebble ? root.pebble.candidateVersion : "")
                        visible: root.updateAvailable
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: parent.width
                        text: qsTr("This update will also upgrade recovery data. Make sure your Pebble smartwatch is connected to a power adapter.")
                        visible: root.updateAvailable && root.pebble.candidateVersion.indexOf("mig") > 0
                        color: Theme.highlightColor
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: parent.width
                        text: qsTr("Release notes")
                        color: Theme.highlightColor
                        visible: root.updateAvailable
                        wrapMode: Text.Wrap
                    }

                    Label {
                        width: parent.width
                        text: root.releaseNotes.length > 0
                              ? root.releaseNotes : qsTr("No release notes were provided for this update.")
                        wrapMode: Text.Wrap
                        textFormat: Text.AutoText
                        visible: root.updateAvailable
                        onLinkActivated: Qt.openUrlExternally(link)
                    }

                    Button {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Upgrade now")
                        visible: root.updateAvailable
                        enabled: root.pebble && root.pebble.connected && !root.pebble.upgradingFirmware
                        onClicked: {
                            root.pebble.performFirmwareUpgrade()
                            pageStack.pop()
                        }
                    }
                }
            }
        }
    }
}
