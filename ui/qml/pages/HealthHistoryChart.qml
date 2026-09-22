import QtQuick 2.6
import Sailfish.Silica 1.0
import "HealthHistory.js" as History

Column {
    id: chart

    property var entries: []
    property string valueKey: "steps"
    property string secondaryKey
    property string presenceKey: "movementDays"
    property bool hours
    property color barColor
    property color secondaryColor
    property real position
    property int visibleCount: 7
    readonly property real maximum: History.maximum(entries, valueKey)
    signal scrolled(real position)

    spacing: Theme.paddingSmall

    function restorePosition() {
        if (!flick.moving)
            flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width,
                                               position * flick.width / visibleCount))
    }

    onPositionChanged: restorePosition()
    onEntriesChanged: reposition.restart()
    onWidthChanged: reposition.restart()

    Timer {
        id: reposition

        interval: 0
        onTriggered: chart.restorePosition()
    }

    SilicaFlickable {
        id: flick

        width: parent.width
        height: Theme.itemSizeMedium + Theme.fontSizeTiny * 4 + Theme.paddingMedium
        contentWidth: Math.max(width, chart.entries.length * width / chart.visibleCount)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        onContentXChanged: {
            if (moving && width > 0)
                chart.scrolled(contentX * chart.visibleCount / width)
        }
        onMovementEnded: {
            // Finish on a whole date so both charts show the same seven bars.
            var target = Math.round(contentX * chart.visibleCount / width)
            contentX = target * width / chart.visibleCount
            chart.scrolled(target)
        }

        Row {
            Repeater {
                model: chart.entries

                delegate: Column {
                    width: flick.width / chart.visibleCount
                    spacing: Theme.paddingSmall / 2

                    Label {
                        width: parent.width
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeTiny
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: Theme.fontSizeTiny * 0.7
                        horizontalAlignment: Text.AlignHCenter
                        text: !modelData[chart.presenceKey] ? "—"
                              : chart.hours ? (modelData[chart.valueKey] / 3600).toFixed(1)
                                            : String(modelData[chart.valueKey])
                    }
                    Item {
                        width: parent.width
                        height: Theme.itemSizeMedium

                        Rectangle {
                            width: Theme.paddingLarge
                            height: modelData[chart.valueKey] > 0
                                    ? Math.max(2, modelData[chart.valueKey] / chart.maximum * parent.height) : 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            radius: Theme.paddingSmall
                            color: chart.barColor
                        }
                        Rectangle {
                            width: Theme.paddingLarge
                            height: chart.secondaryKey && modelData[chart.secondaryKey] > 0
                                    ? Math.max(2, modelData[chart.secondaryKey] / chart.maximum * parent.height) : 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: parent.bottom
                            radius: Theme.paddingSmall
                            color: chart.secondaryColor
                        }
                    }
                    Label {
                        width: parent.width
                        color: Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeTiny
                        horizontalAlignment: Text.AlignHCenter
                        text: Qt.formatDate(History.localDate(modelData.date), "d\nMMM")
                    }
                }
            }
        }

        HorizontalScrollDecorator {}
    }
}
