import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    property var pebble
    Image {
        fillMode: Image.PreserveAspectCrop
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: label.top
        source: "image://theme/icon-m-watch"
    }

    Label {
        id: label
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: state.top
        anchors.bottomMargin: Theme.paddingSmall
        font.pointSize: Theme.fontSizeExtraLarge
        text: (pebble && pebble.name) ? pebble.name : "Pebble"
    }
    Label {
        id: state
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.verticalCenter
        font.pointSize: Theme.fontSizeExtraSmall
        color: Theme.highlightColor
        text: (pebble && pebble.connected) ? qsTr("connected") : qsTr("disconnected")
    }
    onStatusChanged: {if(status===Cover.Activating) pebble=rockPool.getCurPebble()}

    CoverActionList {
        id: coverAction
        enabled: (pebble && pebble.connected)

        CoverAction {
            iconSource: "image://theme/icon-cover-"+((pebble && pebble.connected) ? "transfers" : "sync")
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
