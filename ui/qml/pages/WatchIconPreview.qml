import QtQuick 2.6
import Sailfish.Silica 1.0
import "../images/watch-icons/WatchIcons.js" as WatchIcons

Rectangle {
    id: root

    property string iconCode
    property color backgroundColor: "#ff0055"
    readonly property var artwork: WatchIcons.fileForCode(iconCode)
    readonly property bool available: artwork !== null

    color: backgroundColor
    radius: Theme.paddingSmall
    border.width: 1
    border.color: Theme.rgba(Theme.primaryColor, 0.4)

    Image {
        anchors.fill: parent
        anchors.margins: Theme.paddingSmall
        fillMode: Image.PreserveAspectFit
        sourceSize.width: width
        sourceSize.height: height
        source: root.available ? Qt.resolvedUrl("../images/watch-icons/" + root.artwork) : ""
    }
    Label {
        anchors.centerIn: parent
        visible: !root.available
        text: "?"
        color: "black"
    }
}
