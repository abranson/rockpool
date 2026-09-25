import QtQuick 2.6
import Sailfish.Silica 1.0

// Picks a TimelineIcon for an app's notifications. The watch renders these system images; the
// phone has no copy of them, so this is a searchable name list rather than a visual grid. The
// palette comes from the daemon (org.rockpool.Pebble.TimelineIcons).
Page {
    id: root

    property var pebble: null
    property string sourceId: ""
    property string appName: ""
    property string currentIcon: ""

    property var icons: root.pebble && root.pebble.timelineIconsReady
                        ? root.pebble.timelineIcons : []
    property var shown: []
    property string query: ""

    Component.onCompleted: {
        if (root.pebble) {
            root.pebble.refreshTimelineIcons()
        }
        updateFilter();
    }

    onIconsChanged: updateFilter()

    function pretty(name) {
        return name.replace(/([a-z0-9])([A-Z])/g, "$1 $2")
                   .replace(/([A-Z])([A-Z][a-z])/g, "$1 $2");
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
        root.pebble.setNotificationAppIcon(root.sourceId, iconCode);
        pageStack.pop();
    }

    Column {
        id: searchHeader

        width: parent.width
        PageHeader {
            title: qsTr("Icon")
            description: root.appName
        }
        SearchField {
            width: parent.width
            placeholderText: qsTr("Search icons")
            onTextChanged: root.query = text
        }
    }

    SilicaListView {
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
                onClicked: root.apply("")
            }
        }

        model: root.shown

        delegate: ListItem {
            contentHeight: Theme.itemSizeSmall
            onClicked: root.apply(modelData.code)

            Label {
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: selected.left
                    verticalCenter: parent.verticalCenter
                }
                text: root.pretty(modelData.name)
                truncationMode: TruncationMode.Fade
                color: highlighted || modelData.code === root.currentIcon
                       ? Theme.highlightColor : Theme.primaryColor
            }
            Image {
                id: selected
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                visible: modelData.code === root.currentIcon
                source: "image://theme/icon-m-acknowledge"
            }
        }

        ViewPlaceholder {
            enabled: root.pebble && root.pebble.timelineIconsReady
                     && root.shown.length === 0
            text: qsTr("No matching icons")
        }

        VerticalScrollDecorator {}
    }


    BusyIndicator {
        anchors.centerIn: parent
        running: !root.pebble || !root.pebble.timelineIconsReady
    }
}
