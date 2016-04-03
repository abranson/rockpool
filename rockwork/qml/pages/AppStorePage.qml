import QtQuick 2.2
import Sailfish.Silica 1.0
import RockPool 1.0

Page {
    id: root

    property var pebble: null
    property bool showWatchApps: false
    property bool showWatchFaces: false
    property bool showCategories: false
    property bool enableCategories: true
    property string grpName: ""

    property string link: ""

    AppStoreClient {
        id: client
        hardwarePlatform: pebble.hardwarePlatform
        enableCategories: root.enableCategories
    }

    function fetchHome() {
        if (showWatchApps) {
            client.fetchHome(AppStoreClient.TypeWatchapp)
        } else {
            client.fetchHome(AppStoreClient.TypeWatchface)
        }
    }

    Component.onCompleted: {
        if (root.link) {
            client.fetchLink(link)
        } else {
            root.fetchHome()
        }
    }

    SilicaListView {
        anchors.fill: parent
        PullDownMenu {
            enabled: client.enableCategories
            MenuItem {
                text: qsTr("Use")+" "+(showCategories ? qsTr("Collections") : qsTr("Categories"))
                onClicked: showCategories=!showCategories;
            }
        }

        header: PageHeader {
            title: (grpName)? grpName: qsTr("Add New")+" "+(showWatchApps ? qsTr("Watchapp") : qsTr("Watchface"))
        }

        model: ApplicationsFilterModel {
            id: appsFilterModel
            model: client.model
            showCategories: (root.enableCategories ? root.showCategories : false)
            sortByGroupId: !grpName
            filterCompanion: false
        }
        clip: true

        section.property: "groupId"
        section.labelPositioning: ViewSection.InlineLabels | (grpName ? 0 : ViewSection.CurrentLabelAtStart)
        section.delegate: ListItem {
            height: section ? Theme.itemSizeMedium : 0
            width: parent.width
            contentHeight: height
            visible: section
            Rectangle {
                anchors.fill: parent
                color: Theme.highlightDimmerColor
                opacity: Theme.highlightBackgroundOpacity
            }
            Image {
                id: groupIcon
                anchors {left: parent.left; verticalCenter: parent.verticalCenter }
                height: parent.height - Theme.paddingSmall
                width: source ? height: 0
                source: client.model.groupIcon(section)
                sourceSize.height: height
                sourceSize.width: height
            }

            Label {
                id: label
                anchors {left: groupIcon.right; verticalCenter: parent.verticalCenter}
                width: parent.width-seeAllBtn.width-groupIcon.width-seeAllLbl.width
                text: client.model.groupName(section)
                font.pixelSize: Theme.fontSizeLarge
                elide: Text.ElideRight
            }
            Label {
                id: seeAllLbl
                anchors { right: seeAllBtn.left; verticalCenter: parent.verticalCenter}
                text: qsTr("See all")
            }
            IconButton {
                id: seeAllBtn
                anchors {right:parent.right;verticalCenter: parent.verticalCenter}
                icon.source: "image://theme/icon-m-enter-accept"
                onClicked: {
                    pageStack.push(Qt.resolvedUrl("AppStorePage.qml"), {
                                       pebble: root.pebble,
                                       link: client.model.groupLink(section),
                                       grpName: client.model.groupName(section),
                                       enableCategories: false
                                   });
                }
            }
        }

        footer: Item {
            height: client.model.links.length > 0 ? Theme.itemSizeSmall : 0
            width: root.width-Theme.horizontalPageMargins
            visible: height>0

            Grid {
                anchors.fill: parent
                spacing: Theme.paddingSmall
                columns: client.model.links.length

                Repeater {
                    model: client.model.links
                    Button {
                        width: parent.width/client.model.links.length
                        text: client.model.linkName(client.model.links[index])
                        onClicked: client.fetchLink(client.model.links[index]);
                    }
                }
            }
        }

        delegate: ListItem {
            contentHeight: Math.max(delegateColumn.height,appIcon.height) + Theme.paddingSmall*2

            Row {
                id: delegateRow
                anchors.fill: parent
                anchors.margins: Theme.paddingSmall
                spacing: Theme.paddingSmall

                AnimatedImage {
                    id: appIcon
                    anchors.verticalCenter: parent.verticalCenter
                    source: model.icon
                    asynchronous: true
                }

                Column {
                    id: delegateColumn
                    Label {
                        text: model.name
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Label {
                        text: showCategories ? (model.collection ? model.collection : qsTr("All Apps")) : model.category
                    }
                    Row {
                        spacing: Theme.paddingSmall
                        Image {
                            source: "image://theme/icon-s-like"
                        }
                        Label {
                            text: model.hearts
                        }
                        Image {
                            id: tickIcon
                            source: "image://theme/icon-s-installed"
                            visible: root.pebble.installedApps.contains(model.storeId) || root.pebble.installedWatchfaces.contains(model.storeId)
                            Connections {
                                target: root.pebble.installedApps
                                onChanged: {
                                    tickIcon.visible = root.pebble.installedApps.contains(model.storeId) || root.pebble.installedWatchfaces.contains(model.storeId)
                                }
                            }

                            Connections {
                                target: root.pebble.installedWatchfaces
                                onChanged: {
                                    tickIcon.visible = root.pebble.installedApps.contains(model.storeId) || root.pebble.installedWatchfaces.contains(model.storeId)
                                }
                            }
                        }
                        Image {
                            source: "image://theme/icon-s-high-importance"
                            visible: model.companion
                        }
                        Label {
                            text: qsTr("Needs companion")
                            visible: model.companion
                        }
                    }
                    Separator {
                        width: parent.width
                        height: Theme.paddingSmall
                        color: Theme.secondaryHighlightColor
                    }
                }
            }

            onClicked: {
                client.fetchAppDetails(model.storeId);
                pageStack.push(Qt.resolvedUrl("AppStoreDetailsPage.qml"), {app: appsFilterModel.get(index), pebble: root.pebble})
            }
        }
    }
    BusyIndicator {
        anchors.centerIn: parent
        running: client.busy
        size: BusyIndicatorSize.Large
        visible: running
    }
    DockedPanel {
        id: searchField
        dock: Dock.Bottom
        width: parent.width
        height: Theme.iconSizeMedium
        open: true

        onOpenChanged: {
            if (open) {
                searchTextField.focus = true;
            }
        }
        TextField {
            id: searchTextField
            anchors { top: parent.top; left: parent.left }
            width: parent.width - parent.height
            placeholderText: qsTr("Search app or watchface")
        }
        IconButton {
            anchors { top: parent.top; right: parent.right }
            height: parent.height
            width: height
            icon.source: "image://theme/icon-m-search"
            onClicked: client.search(searchTextField.text, root.showWatchApps ? AppStoreClient.TypeWatchapp : AppStoreClient.TypeWatchface);
        }
    }
}

