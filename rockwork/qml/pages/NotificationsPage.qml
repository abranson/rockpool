import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    // TimelineColor.name -> "#RRGGBB", so the list can show a swatch for an app's colour override
    // without each delegate re-querying the daemon. Filled once from the same palette the picker uses.
    property var colorMap: buildColorMap()

    Component.onCompleted: {
        if (root.pebble) {
            root.pebble.refreshTimelineColors()
        }
    }

    function buildColorMap() {
        var map = {}
        if (!root.pebble || !root.pebble.timelineColorsReady) {
            return map
        }
        var colors = root.pebble.timelineColors
        for (var i = 0; i < colors.length; i++) {
            map[colors[i].name] = colors[i].rgb;
        }
        return map
    }

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
                    width: parent.width - x - Theme.itemSizeSmall
                    truncationMode: TruncationMode.Fade
                }
            }
            // Appearance-override indicators, right-aligned: a colour swatch and/or an icon glyph.
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSizeSmall / 2
                    height: width
                    radius: width / 2
                    visible: model.colorName !== "" && root.colorMap[model.colorName] !== undefined
                    color: visible ? root.colorMap[model.colorName] : "transparent"
                    border.width: 1
                    border.color: Theme.rgba(Theme.primaryColor, 0.4)
                }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-s-installed"
                    visible: model.iconCode !== ""
                    opacity: 0.6
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
                    text: qsTr("Colour…")
                    onClicked: pageStack.push(Qt.resolvedUrl("NotificationColorPage.qml"), {
                                                  pebble: root.pebble,
                                                  sourceId: model.id,
                                                  appName: model.name,
                                                  currentColor: model.colorName
                                              })
                }
                MenuItem {
                    text: qsTr("Icon…")
                    onClicked: pageStack.push(Qt.resolvedUrl("NotificationIconPage.qml"), {
                                                  pebble: root.pebble,
                                                  sourceId: model.id,
                                                  appName: model.name,
                                                  currentIcon: model.iconCode
                                              })
                }
                MenuItem {
                    text: qsTr("Forget")
                    onClicked: root.pebble.forgetNotificationFilter(model.id)
                }
            }
        }
    }
}
