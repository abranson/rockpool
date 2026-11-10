import QtQuick 2.6
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

    function forwardingText(mode) {
        if (mode === 2) return qsTr("Always send to watch")
        if (mode === 1) return qsTr("Only when phone is inactive")
        if (mode === 0) return qsTr("Do not send to watch")
        return qsTr("Unknown forwarding setting")
    }

    SilicaListView {
        id: notificationsList

        anchors.fill: parent
        header: Column {
            width: notificationsList.width
            PageHeader {
                title: qsTr("Notifications")
            }
            Label {
                text: qsTr("Tap an app to change notification forwarding. Tap its watch icon to customise the icon and colour. Apps appear here after sending a notification.")
                wrapMode: Text.Wrap
                width: parent.width - Theme.horizontalPageMargin
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
            }
            Label {
                text: qsTr("* Custom icon or colour")
                width: parent.width - Theme.horizontalPageMargin
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        footer: Item { width: 1; height: Theme.paddingLarge }
        clip: true
        model: root.pebble ? root.pebble.notifications : null
        section.property: "enabled"
        section.labelPositioning: ViewSection.InlineLabels | ViewSection.CurrentLabelAtStart
        section.delegate: Item {
            width: notificationsList.width
            height: sectionTitle.height

            Rectangle {
                anchors.fill: parent
                color: Theme.rgba(Theme.highlightDimmerColor, 1.0)
            }
            SectionHeader {
                id: sectionTitle

                text: root.forwardingText(Number(section))
            }
        }

        VerticalScrollDecorator {}

        delegate: ListItem {
            id: notificationItem

            readonly property real sourceOpacity: model.enabled === 0 ? 0.5 : 1.0
            readonly property string defaultIconCode: model.defaultIconCode
            readonly property string watchIconCode: model.iconCode || defaultIconCode
            readonly property string watchColorName: model.colorName || model.defaultColorName

            function openAppearance() {
                pageStack.push(Qt.resolvedUrl("NotificationIconPage.qml"), {
                                   pebble: root.pebble,
                                   sourceId: model.id,
                                   appName: model.name,
                                   currentIcon: Qt.binding(function() { return model.iconCode }),
                                   currentColor: Qt.binding(function() { return model.colorName }),
                                   defaultIcon: Qt.binding(function() { return notificationItem.defaultIconCode }),
                                   defaultColor: Qt.binding(function() { return model.defaultColorName })
                               })
            }

            width: notificationsList.width
            contentHeight: Math.max(details.height, appIcon.height) + 2 * Theme.paddingMedium

            Image {
                id: appIcon

                y: (notificationItem.contentHeight - height) / 2
                width: Theme.iconSizeSmallPlus
                height: width
                opacity: notificationItem.sourceOpacity
                fillMode: Image.PreserveAspectFit
                source: {
                    if (model.icon.indexOf("image://") === 0 || model.icon.indexOf("file://") === 0)
                        return model.icon
                    if (model.icon.indexOf("/") === 0)
                        return "file://" + model.icon
                    return "image://theme/" + model.icon
                }
            }
            BackgroundItem {
                id: appearanceButton

                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingSmall
                width: Theme.itemSizeSmall
                height: notificationItem.contentHeight
                onClicked: notificationItem.openAppearance()

                WatchIconPreview {
                    id: watchPreview

                    anchors.centerIn: parent
                    width: Theme.iconSizeSmallPlus
                    height: width
                    opacity: notificationItem.sourceOpacity
                    iconCode: notificationItem.watchIconCode
                    // Pebble's notification layout uses Folly when no background colour is supplied.
                    backgroundColor: root.colorMap[notificationItem.watchColorName] || "#ff0055"

                    Label {
                        anchors.right: parent.right
                        anchors.bottom: parent.top
                        anchors.bottomMargin: -Theme.paddingMedium
                        visible: model.iconCode !== "" || model.colorName !== ""
                        text: "*"
                        color: Theme.highlightColor
                        font.bold: true
                    }
                }
            }
            Column {
                id: details

                x: appIcon.width + Theme.paddingMedium
                y: (notificationItem.contentHeight - height) / 2
                opacity: notificationItem.sourceOpacity
                width: appearanceButton.x - x - Theme.paddingMedium
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    color: notificationItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: {
                        var icon = model.iconCode !== "" ? qsTr("Custom icon")
                                   : notificationItem.defaultIconCode !== "" ? qsTr("Default icon")
                                   : qsTr("Automatic icon")
                        var colour = model.colorName !== "" ? qsTr("Custom colour") : qsTr("Default colour")
                        return icon + " · " + colour
                    }
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: notificationItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
                }
            }
            onClicked: openMenu()
            menu: ContextMenu {
                MenuItem {
                    text: root.forwardingText(2)
                    onClicked: root.pebble.setNotificationFilter(model.id, 2)
                    highlighted: !enabled
                    enabled: model.enabled !== 2
                }
                MenuItem {
                    text: root.forwardingText(1)
                    onClicked: root.pebble.setNotificationFilter(model.id, 1)
                    highlighted: !enabled
                    enabled: model.enabled !== 1
                }
                MenuItem {
                    text: root.forwardingText(0)
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
