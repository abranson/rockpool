import QtQuick 2.2
import Sailfish.Silica 1.0

DockedPanel {
    id: dockedField
    dock: Dock.Bottom
    width: parent.width
    height: Theme.iconSizeMedium
    open: false
    property string icon: ""
    property string hint: ""
    property alias text: dockedTextField.text

    signal submit

    onMovingChanged: {
        if (open && visibleSize === height) {
            dockedTextField.focus = true;
        }
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.highlightDimmerColor
        opacity: 0.75
    }
    IconButton {
        icon.source: "image://theme/icon-m-reset"
        anchors {top: parent.top; left:parent.left}
        height:parent.height
        width: height
        onClicked: dockedField.open=false;
    }
    TextField {
        id: dockedTextField
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - parent.height*1.8
        placeholderText: dockedField.hint
    }
    IconButton {
        anchors { top: parent.top; right: parent.right }
        height: parent.height
        width: height
        icon.source: dockedField.icon
        onClicked: {
            dockedField.open=false;
            dockedField.submit()
        }
    }
}
