import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: loadingComponent

    Column {
        width:parent.width
        //anchors.fill: parent

        PageHeader {
            //width: parent.width
            title: "RockPool"
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeLarge
            text: qsTr("Loading and Connecting...")
        }
    }
    Image {
        id: upgradeIcon
        anchors.centerIn: parent
        source: "image://theme/icon-m-sync"

        RotationAnimation on rotation {
            duration: 2000
            loops: Animation.Infinite
            from: 0
            to: 360
            running: upgradeIcon.visible
        }
        visible: !pebbles.connectedToService
    }
}
