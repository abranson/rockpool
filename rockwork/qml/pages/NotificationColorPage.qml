import QtQuick 2.2
import Sailfish.Silica 1.0

// Picks a TimelineColor for an app's notifications. The palette comes from the daemon
// (org.rockwork.Pebble.TimelineColors) so it tracks libpebble3's colour set.
Page {
    id: root

    property var pebble: null
    property string sourceId: ""
    property string appName: ""
    property string currentColor: ""

    property var colors: root.pebble && root.pebble.timelineColorsReady
                         ? root.pebble.timelineColors : []

    Component.onCompleted: {
        if (root.pebble) {
            root.pebble.refreshTimelineColors()
        }
    }

    function apply(colorName) {
        root.pebble.setNotificationAppColor(root.sourceId, colorName);
        pageStack.pop();
    }

    SilicaGridView {
        id: grid
        anchors.fill: parent
        cellWidth: width / Math.floor(width / (Theme.itemSizeMedium * 1.2))
        cellHeight: cellWidth

        header: Column {
            width: grid.width
            PageHeader {
                title: qsTr("Colour")
                description: root.appName
            }
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Use default colour")
                onClicked: root.apply("")
            }
        }

        model: root.colors

        delegate: BackgroundItem {
            width: grid.cellWidth
            height: grid.cellHeight
            onClicked: root.apply(modelData.name)

            Rectangle {
                anchors.centerIn: parent
                width: parent.width - Theme.paddingMedium
                height: parent.height - Theme.paddingMedium
                radius: Theme.paddingSmall
                color: modelData.rgb
                // The current colour is marked by a thick highlight border rather than a glyph:
                // a monochrome theme checkmark would be invisible on light swatches.
                border.width: modelData.name === root.currentColor ? 4 : 1
                border.color: modelData.name === root.currentColor
                              ? Theme.highlightColor
                              : Theme.rgba(Theme.primaryColor, 0.3)
            }
        }

        VerticalScrollDecorator {}
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: !root.pebble || !root.pebble.timelineColorsReady
    }
}
