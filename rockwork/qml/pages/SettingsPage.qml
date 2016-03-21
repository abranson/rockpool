import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null

    SilicaFlickable {
        anchors.fill: parent
        anchors.margins: Theme.horizontalPageMargin

        Column {
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: qsTr("Settings")
            }
            Slider {
                width: parent.width
                label: qsTr("Distance Units")
                valueText: [qsTr("Metric"), qsTr("Imperial")][value]
                minimumValue: 0
                maximumValue: 1
                stepSize: 1
                onValueChanged: {
                    root.pebble.imperialUnits = (value===1)
                }
                value: (root.pebble.imperialUnits) ? 1 : 0
            }
            Separator {
                width: parent.width * 0.7
                anchors.horizontalCenter: parent.horizontalCenter
                height: Theme.paddingSmall
                color: Theme.secondaryHighlightColor
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Sync calendar to timeline")
                checked: root.pebble.calendarSyncEnabled
                onClicked: {
                    root.pebble.calendarSyncEnabled = checked
                }
            }
        }
    }
}
