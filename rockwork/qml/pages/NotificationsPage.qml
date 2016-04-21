import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null

    SilicaListView {
        anchors.fill: parent
        header: Column {
            width: parent.width
            height: childrenRect.height
            PageHeader {
                title: qsTr("Notifications")
            }
            Label {
                text: qsTr("Entries here will be added as notifications appear on the phone. Selected notifications will be shown on your Pebble smartwatch.")
                wrapMode: Text.WordWrap
                width: parent.width
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                opacity: 0.7
            }
        }
        clip: true
        model: root.pebble.notifications

        delegate: ListItem {
            width: parent.width
            contentHeight: Theme.itemSizeSmall
            Row {
                width: parent.width
                height: parent.contentHeight
                spacing: Theme.paddingSmall
                Image {
                    source: {
                        if (model.icon.indexOf("image://") === 0 || model.icon.indexOf("file://")  === 0)
                            return model.icon;
                        else if (model.icon.indexOf("/") === 0)
                            return "file://" + model.icon
                        else
                            return "image://theme/"+model.icon

                    }
                    width: height
                    height: parent.height
                }
                Image {
                    height: parent.height
                    width: height
                    source: "image://theme/icon-m-"+(model.enabled===0?"dismiss":(model.enabled===1?"screenlock":"acknowledge"))
                }

                Label {
                    text: model.name
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            onClicked: showMenu()
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Always Enabled")
                    onClicked: root.pebble.setNotificationFilter(model.id, 2)
                    highlighted: !enabled
                    enabled: model.enabled !== 2
                }
                MenuItem {
                    text: qsTr("Disabled When Active")
                    onClicked: root.pebble.setNotificationFilter(model.id, 1)
                    highlighted: !enabled
                    enabled: model.enabled !== 1
                }
                MenuItem {
                    text: qsTr("Always Disabled")
                    onClicked: root.pebble.setNotificationFilter(model.id, 0)
                    highlighted: !enabled
                    enabled: model.enabled !== 0
                }
                MenuItem {
                    text: qsTr("Forget")
                    onClicked: root.pebble.forgetNotificationFilter(model.id)
                }
            }
        }
    }
}
