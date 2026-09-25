import QtQuick 2.6
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    readonly property var settings: pebble ? pebble.quietTimeSettings : ({})
    readonly property bool ready: pebble && pebble.quietTimeReady
    readonly property bool editable: ready && !pebble.quietTimeBusy && settings.syncEnabled

    function setValue(key, value) {
        if (editable) pebble.setQuietTimeSetting(key, value)
    }

    function chooseTime(key, end) {
        var schedule = settings[key]
        if (!schedule || !editable) return
        var range = schedule.split("-")
        var time = range[end ? 1 : 0].split(":")
        var watch = pebble
        var dialog = pageStack.push("Sailfish.Silica.TimePickerDialog", {
                                        hour: Number(time[0]), minute: Number(time[1])
                                    })
        dialog.accepted.connect(function() {
            if (root.pebble !== watch || !root.editable) return
            // Preserve the other endpoint if a watch-side change arrived while picking.
            var current = root.settings[key].split("-")
            var hour = dialog.hour
            var minute = dialog.minute
            current[end ? 1 : 0] = (hour < 10 ? "0" : "") + hour
                    + ":" + (minute < 10 ? "0" : "") + minute
            root.setValue(key, current.join("-"))
        })
    }

    onStatusChanged: if (status === PageStatus.Active && pebble) pebble.refreshQuietTime()

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Refresh")
                enabled: root.pebble && !root.pebble.quietTimeBusy
                onClicked: root.pebble.refreshQuietTime()
            }
        }
        VerticalScrollDecorator {}

        Column {
            id: content

            width: parent.width

            PageHeader { title: qsTr("Quiet Time") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: qsTr("These settings are shared by your watches and sync when connected. Firmware support may vary.")
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: root.pebble && root.pebble.quietTimeError.length > 0
                text: root.pebble ? root.pebble.quietTimeError : ""
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: root.ready && !root.settings.syncEnabled
                text: qsTr("Watch settings synchronization is disabled.")
                color: Theme.highlightColor
                wrapMode: Text.Wrap
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Medium
                running: root.pebble && root.pebble.quietTimeBusy
                visible: running
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Manual Quiet Time")
                enabled: root.editable
                automaticCheck: false
                checked: root.settings.dndManuallyEnabled === "1"
                onClicked: root.setValue("dndManuallyEnabled", checked ? "0" : "1")
            }
            TextSwitch {
                width: parent.width
                text: qsTr("During calendar events")
                enabled: root.editable
                automaticCheck: false
                checked: root.settings.dndSmartEnabled === "1"
                onClicked: root.setValue("dndSmartEnabled", checked ? "0" : "1")
            }
            Repeater {
                model: [
                    {title: qsTr("Weekdays"), enabledKey: "dndWeekdayScheduleEnabled", hoursKey: "dndWeekdaySchedule"},
                    {title: qsTr("Weekends"), enabledKey: "dndWeekendScheduleEnabled", hoursKey: "dndWeekendSchedule"}
                ]
                Column {
                    width: content.width

                    SectionHeader { text: modelData.title }
                    TextSwitch {
                        width: parent.width
                        text: qsTr("Scheduled Quiet Time")
                        enabled: root.editable
                        automaticCheck: false
                        checked: root.settings[modelData.enabledKey] === "1"
                        onClicked: root.setValue(modelData.enabledKey, checked ? "0" : "1")
                    }
                    ValueButton {
                        width: parent.width
                        label: qsTr("Start")
                        value: root.settings[modelData.hoursKey] ? root.settings[modelData.hoursKey].split("-")[0] : ""
                        enabled: root.editable
                        onClicked: root.chooseTime(modelData.hoursKey, false)
                    }
                    ValueButton {
                        width: parent.width
                        label: qsTr("End")
                        value: root.settings[modelData.hoursKey] ? root.settings[modelData.hoursKey].split("-")[1] : ""
                        enabled: root.editable
                        onClicked: root.chooseTime(modelData.hoursKey, true)
                    }
                }
            }
            SectionHeader { text: qsTr("During Quiet Time") }
            TextSwitch {
                width: parent.width
                text: qsTr("Allow phone calls")
                enabled: root.editable
                automaticCheck: false
                checked: root.settings.dndInterruptionsMask === "2"
                onClicked: root.setValue("dndInterruptionsMask", checked ? "0" : "2")
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Show notifications")
                enabled: root.editable
                automaticCheck: false
                checked: root.settings.dndShowNotifications === "1"
                onClicked: root.setValue("dndShowNotifications", checked ? "0" : "1")
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Automatically dismiss notifications")
                enabled: root.editable && root.settings.dndShowNotifications === "1"
                automaticCheck: false
                checked: root.settings.dndAutoDismiss === "1"
                onClicked: root.setValue("dndAutoDismiss", checked ? "0" : "1")
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Motion backlight")
                enabled: root.editable
                automaticCheck: false
                checked: root.settings.dndMotionBacklight === "1"
                onClicked: root.setValue("dndMotionBacklight", checked ? "0" : "1")
            }
        }
    }
}
