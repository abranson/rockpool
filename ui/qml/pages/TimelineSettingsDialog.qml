import QtQuick 2.6
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var pebble: null
    property bool timelineWindowDirty
    property bool loadingTimelineWindow

    canAccept: pebble && pebble.timelineWindowReady && timelineWindowDirty
               && timelineWindowStartField.acceptableInput && timelineWindowStartField.text.length > 0
               && timelineWindowEndField.acceptableInput && timelineWindowEndField.text.length > 0
               && timelineWindowFadeField.acceptableInput && timelineWindowFadeField.text.length > 0
               && -Number(timelineWindowStartField.text) <= Number(timelineWindowEndField.text)

    function loadTimelineWindow() {
        if (!pebble || !pebble.timelineWindowReady || timelineWindowDirty) {
            return
        }
        loadingTimelineWindow = true
        timelineWindowStartField.text = pebble.timelineWindowStart
        timelineWindowEndField.text = pebble.timelineWindowEnd
        timelineWindowFadeField.text = pebble.timelineWindowFade
        loadingTimelineWindow = false
    }

    Component.onCompleted: {
        if (pebble) {
            root.pebble.refreshTimelineWindow()
        }
        loadTimelineWindow()
    }

    Connections {
        target: root.pebble
        onTimelineWindowChanged: root.loadTimelineWindow()
        onTimelineWindowReadyChanged: root.loadTimelineWindow()
    }

    onAccepted: {
        root.pebble.setTimelineWindow(
                    Number(timelineWindowStartField.text),
                    Number(timelineWindowFadeField.text),
                    Number(timelineWindowEndField.text))
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: content

            width: parent.width

            DialogHeader {
                title: qsTr("Timeline window")
                acceptText: qsTr("Save")
            }

            TextField {
                id: timelineWindowStartField

                width: parent.width
                enabled: root.pebble && root.pebble.timelineWindowReady
                label: qsTr("Start (days included before today)")
                placeholderText: label
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: IntValidator { bottom: 1; top: 365 }
                onTextChanged: if (activeFocus && !root.loadingTimelineWindow) root.timelineWindowDirty = true
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: timelineWindowEndField.focus = true
            }

            TextField {
                id: timelineWindowEndField

                width: parent.width
                enabled: root.pebble && root.pebble.timelineWindowReady
                label: qsTr("End (days included after today)")
                placeholderText: label
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: IntValidator { bottom: -365; top: 365 }
                onTextChanged: if (activeFocus && !root.loadingTimelineWindow) root.timelineWindowDirty = true
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: timelineWindowFadeField.focus = true
            }

            SectionHeader {
                text: qsTr("Notifications")
            }

            TextField {
                id: timelineWindowFadeField

                width: parent.width
                enabled: root.pebble && root.pebble.timelineWindowReady
                label: qsTr("Re-delivery expiration (seconds)")
                placeholderText: label
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                validator: IntValidator { bottom: 0; top: 2592000 }
                onTextChanged: if (activeFocus && !root.loadingTimelineWindow) root.timelineWindowDirty = true
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }
        }
    }
}
