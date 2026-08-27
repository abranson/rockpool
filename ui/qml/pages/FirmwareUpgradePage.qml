import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root
    property var pebble: null

    SilicaFlickable {
        anchors.fill: parent
        anchors.margins: Theme.horizontalPageMargin
        Column {
            anchors.fill: parent
            spacing: Theme.paddingSmall
            PageHeader {
                title: qsTr("Firmware upgrade")
            }
            Label {
                text: qsTr("Currently installed firmware: %1").arg("<b>" + root.pebble.softwareVersion + "</b>")
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Label {
                text: qsTr("A new firmware upgrade is available for your Pebble smartwatch.")
                font.pixelSize: Theme.fontSizeLarge
                width: parent.width
                wrapMode: Text.WordWrap
                visible: root.pebble.firmwareUpgradeAvailable
            }

            Label {
                text: qsTr("Candidate firmware version: %1").arg("<b>" + root.pebble.candidateVersion + "</b>")
                width: parent.width
                wrapMode: Text.WordWrap
                visible: root.pebble.firmwareUpgradeAvailable
            }

            Label {
                text: "<b>" + qsTr("Release Notes: %1").arg("</b><br>" + root.pebble.firmwareReleaseNotes)
                width: parent.width
                wrapMode: Text.WordWrap
                visible: root.pebble.firmwareUpgradeAvailable
            }

            Label {
                text: "<b>" + qsTr("Important:") + "</b> " + qsTr("This update will also upgrade recovery data. Make sure your Pebble smartwarch is connected to a power adapter.")
                width: parent.width
                wrapMode: Text.WordWrap
                visible: root.pebble.candidateVersion.indexOf("mig") > 0
            }

            Button {
                text: enabled ? qsTr("Upgrade now") : qsTr("Firmware is Up-To-Date")
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: root.pebble.firmwareUpgradeAvailable
                onClicked: {
                    root.pebble.performFirmwareUpgrade();
                    pageStack.pop();
                }
            }
        }
    }
}
