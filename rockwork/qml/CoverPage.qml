import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    property var pebble: null
    Image {
        fillMode: Image.PreserveAspectCrop
        anchors.fill: parent
        source: "back-cover.png"
    }

    Label {
        id: label
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: state.top
        anchors.bottomMargin: Theme.paddingSmall
        font.pointSize: Theme.fontSizeExtraLarge
        text: pebble.name ? pebble.name : "Pebble"
    }
    Label {
        id: state
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.verticalCenter
        font.pointSize: Theme.fontSizeExtraSmall
        color: Theme.highlightColor
        text: pebble.connected ? qsTr("connected") : qsTr("disconnected")
    }

    CoverActionList {
        id: coverAction
        enabled: pebble.connected

        CoverAction {
            iconSource: pebble.connected ? "image://theme/icon-cover-transfers" : "image://theme/icon-cover-sync"
            onTriggered: {
                if (pebble.connected) {
                    pebble.requestScreenshot();
                } else {
                    pebble.reconnect();
                }
            }
        }
    }
}
