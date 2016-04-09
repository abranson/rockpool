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
                width: parent.width
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
            Separator {
                width: parent.width
                height: Theme.paddingSmall
                color: Theme.secondaryHighlightColor
            }
            Repeater {
                model: [[0,qsTr("Connected")],[1,qsTr("Disconnected")]]
                delegate: Slider {
                    width: parent.width
                    label: qsTr("Profile when")+" "+modelData[1]
                    minimumValue: 0
                    maximumValue: rockPool.sysProfiles.length-1
                    stepSize: 1
                    valueText: rockPool.sysProfiles[value]
                    enabled: rockPool.sysProfiles.length>1
                    visible: enabled
                    onValueChanged: {
                        if(value===0) {
                            //pebble.setProfile(modelData[0],'')
                            console.log("Disable profile change for",modelData[1])
                        } else {
                            //pebble.setProfile(modelData[0],rockPool.sysProfiles[value])
                            console.log("Set profile",rockPool.sysProfiles[value],"for",modelData[1])
                        }
                    }
                }
            }
            Separator {
                width: parent.width
                height: Theme.paddingSmall
                color: Theme.secondaryHighlightColor
            }
        }
        Component.onCompleted: rockPool.getProfiles()
    }
}
