import QtQuick 2.4
import Ubuntu.Components 1.3

Page {
    id: root
    title: i18n.tr("Firmware upgrade")

    property var pebble: null

    Column {
        anchors.fill: parent
        anchors.margins: units.gu(1)
        spacing: units.gu(2)

        Label {
            text: i18n.tr("A new firmware upgrade is available for your Pebble smartwatch.")
            fontSize: "large"
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Label {
            text: i18n.tr("Currently installed firmware: %1").arg("<b>" + root.pebble.softwareVersion + "</b>")
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Label {
            text: i18n.tr("Candidate firmware version: %1").arg("<b>" + root.pebble.candidateVersion + "</b>")
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Label {
            text: "<b>" + i18n.tr("Release Notes: %1").arg("</b><br>" + root.pebble.firmwareReleaseNotes)
            width: parent.width
            wrapMode: Text.WordWrap
        }

        Label {
            text: "<b>" + i18n.tr("Important:") + "</b> " + i18n.tr("This update will also upgrade recovery data. Make sure your Pebble smartwarch is connected to a power adapter.")
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.pebble.candidateVersion.indexOf("mig") > 0
        }

        Button {
            text: "Upgrade now"
            anchors.horizontalCenter: parent.horizontalCenter
            color: UbuntuColors.blue
            onClicked: {
                root.pebble.performFirmwareUpgrade();
                pageStack.pop();
            }
        }
    }
}

