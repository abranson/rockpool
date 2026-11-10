import QtQuick 2.6
import Sailfish.Silica 1.0

// Uses the daemon's icon/colour palettes and the bundled watch artwork.
Page {
    id: root

    property var pebble: null
    property string sourceId: ""
    property string appName: ""
    property string currentIcon: ""
    property string currentColor: ""
    property string defaultIcon: ""
    property string defaultColor: ""

    readonly property string effectiveIcon: currentIcon || defaultIcon
    readonly property string effectiveColor: currentColor || defaultColor
    readonly property color previewColor: colorForName(effectiveColor)
    property var colors: root.pebble && root.pebble.timelineColorsReady
                         ? root.pebble.timelineColors : []
    property var icons: root.pebble && root.pebble.timelineIconsReady
                        ? root.pebble.timelineIcons : []
    property var shown: []
    property string query: ""

    Component.onCompleted: {
        if (root.pebble) {
            root.pebble.refreshTimelineIcons()
            root.pebble.refreshTimelineColors()
        }
        updateFilter();
    }

    onIconsChanged: updateFilter()

    function pretty(name) {
        return name.replace(/([a-z0-9])([A-Z])/g, "$1 $2")
                   .replace(/([A-Z])([A-Z][a-z])/g, "$1 $2");
    }

    function iconName(code) {
        for (var i = 0; i < icons.length; i++) {
            if (icons[i].code === code) return pretty(icons[i].name)
        }
        return code
    }

    function colorForName(name) {
        for (var i = 0; i < colors.length; i++) {
            if (colors[i].name === name) return colors[i].rgb
        }
        return "#ff0055"
    }

    function updateFilter() {
        if (query === "") {
            shown = icons;
            return;
        }
        var q = query.toLowerCase();
        var out = [];
        for (var i = 0; i < icons.length; i++) {
            if (pretty(icons[i].name).toLowerCase().indexOf(q) !== -1)
                out.push(icons[i]);
        }
        shown = out;
    }

    onQueryChanged: updateFilter()

    function apply(iconCode) {
        if (root.pebble) root.pebble.setNotificationAppIcon(root.sourceId, iconCode);
    }

    Column {
        id: searchHeader

        width: parent.width
        PageHeader {
            title: qsTr("Icon and colour")
            description: root.appName
        }
        Item {
            width: parent.width
            height: Math.max(currentPreview.height, currentDetails.height) + Theme.paddingMedium

            WatchIconPreview {
                id: currentPreview

                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSizeMedium
                height: width
                iconCode: root.effectiveIcon
                backgroundColor: root.previewColor
            }
            Column {
                id: currentDetails

                anchors.left: currentPreview.right
                anchors.leftMargin: Theme.paddingMedium
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter

                Label {
                    width: parent.width
                    text: root.effectiveIcon ? root.iconName(root.effectiveIcon) : qsTr("Automatic icon")
                    truncationMode: TruncationMode.Fade
                }
                Label {
                    width: parent.width
                    text: root.currentIcon ? qsTr("Custom icon") : qsTr("Default icon")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    truncationMode: TruncationMode.Fade
                }
                Label {
                    width: parent.width
                    visible: !currentPreview.available
                    text: qsTr("Icon preview unavailable")
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    truncationMode: TruncationMode.Fade
                }
            }
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            text: root.currentColor ? qsTr("Custom colour: %1").arg(root.pretty(root.currentColor))
                                    : qsTr("Default colour: %1").arg(root.pretty(root.defaultColor || "Folly"))
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            truncationMode: TruncationMode.Fade
        }
        SilicaListView {
            id: colorList

            width: parent.width
            height: Theme.itemSizeSmall
            orientation: ListView.Horizontal
            clip: true
            leftMargin: Theme.horizontalPageMargin
            rightMargin: Theme.horizontalPageMargin
            model: root.colors
            currentIndex: {
                for (var i = 0; i < root.colors.length; i++) {
                    if (root.colors[i].name === (root.effectiveColor || "Folly")) return i
                }
                return -1
            }
            onCurrentIndexChanged: {
                if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
            }

            delegate: BackgroundItem {
                width: Theme.itemSizeSmall
                height: colorList.height
                onClicked: root.pebble.setNotificationAppColor(root.sourceId, modelData.name)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.paddingSmall
                    radius: Theme.paddingSmall
                    color: modelData.rgb
                    border.width: modelData.name === (root.effectiveColor || "Folly") ? 4 : 1
                    border.color: modelData.name === (root.effectiveColor || "Folly")
                                  ? Theme.highlightColor : Theme.rgba(Theme.primaryColor, 0.3)
                }
            }
            HorizontalScrollDecorator {}
        }
        SearchField {
            width: parent.width
            placeholderText: qsTr("Search icons")
            onTextChanged: root.query = text
        }
    }

    SilicaListView {
        id: iconList

        anchors {
            top: searchHeader.bottom
            bottom: parent.bottom
            left: parent.left
            right: parent.right
        }
        clip: true

        PullDownMenu {
            MenuItem {
                text: qsTr("Use default icon")
                enabled: root.currentIcon !== ""
                onClicked: root.apply("")
            }
            MenuItem {
                text: qsTr("Use default colour")
                enabled: root.pebble && root.currentColor !== ""
                onClicked: root.pebble.setNotificationAppColor(root.sourceId, "")
            }
        }

        model: root.shown

        delegate: ListItem {
            id: iconItem

            width: iconList.width
            contentHeight: Theme.itemSizeSmall
            onClicked: root.apply(modelData.code)

            WatchIconPreview {
                id: iconPreview

                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSizeSmallPlus
                height: width
                iconCode: modelData.code
                backgroundColor: root.previewColor
            }
            Label {
                anchors {
                    left: iconPreview.right
                    leftMargin: Theme.paddingMedium
                    right: selected.left
                    rightMargin: Theme.paddingSmall
                    verticalCenter: parent.verticalCenter
                }
                text: root.pretty(modelData.name)
                truncationMode: TruncationMode.Fade
                color: iconItem.highlighted || modelData.code === root.effectiveIcon
                       ? Theme.highlightColor : Theme.primaryColor
            }
            Image {
                id: selected

                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                visible: modelData.code === root.effectiveIcon
                source: "image://theme/icon-m-acknowledge"
            }
        }

        ViewPlaceholder {
            enabled: root.pebble && root.pebble.timelineIconsReady
                     && root.shown.length === 0
            text: qsTr("No matching icons")
        }

        VerticalScrollDecorator {}
        BusyIndicator {
            anchors.centerIn: parent
            running: !root.pebble || !root.pebble.timelineIconsReady
        }
    }
}
