import QtQuick 2.4
import Ubuntu.Components 1.3
import QtQuick.Layouts 1.1
import RockWork 1.0

Page {
    id: root
    title: showWatchApps ? i18n.tr("Add new watchapp") : i18n.tr("Add new watchface")

    property var pebble: null
    property bool showWatchApps: false
    property bool showWatchFaces: false

    property string link: ""

    function fetchHome() {
        if (showWatchApps) {
            client.fetchHome(AppStoreClient.TypeWatchapp)
        } else {
            client.fetchHome(AppStoreClient.TypeWatchface)
        }
    }

    head {
        actions: [
            Action {
                iconName: "search"
                onTriggered: {
                    if (searchField.shown) {
                        searchField.shown = false;
                        root.fetchHome();
                    } else {
                        searchField.shown = true;
                    }
                }
            }
        ]
    }

    Component.onCompleted: {
        if (root.link) {
            client.fetchLink(link)
        } else {
            root.fetchHome()
        }
    }

    AppStoreClient {
        id: client
        hardwarePlatform: pebble.hardwarePlatform
    }

    Item {
        id: searchField
        anchors { left: parent.left; right: parent.right; top: parent.top }
        anchors.topMargin: shown ? 0 : -height
        Behavior on anchors.topMargin { UbuntuNumberAnimation {} }
        opacity: shown ? 1 : 0
        Behavior on opacity { UbuntuNumberAnimation {} }
        height: units.gu(6)

        property bool shown: false
        onShownChanged: {
            if (shown) {
                searchTextField.focus = true;
            }
        }

        TextField {
            id: searchTextField
            anchors.centerIn: parent
            width: parent.width - units.gu(2)
            onDisplayTextChanged: {
                searchTimer.restart()
            }

            Timer {
                id: searchTimer
                interval: 300
                onTriggered: {
                    client.search(searchTextField.displayText, root.showWatchApps ? AppStoreClient.TypeWatchapp : AppStoreClient.TypeWatchface);
                }
            }
        }
    }

    Item {
        anchors { left: parent.left; top: searchField.bottom; right: parent.right; bottom: parent.bottom }
        ListView {
            anchors.fill: parent
            model: ApplicationsFilterModel {
                id: appsFilterModel
                model: client.model
            }
            clip: true
            section.property: "groupId"
            section.labelPositioning: ViewSection.CurrentLabelAtStart |
                                      ViewSection.InlineLabels
            section.delegate: ListItem {
                height: section ? label.implicitHeight + units.gu(3) : 0

                Rectangle {
                    anchors.fill: parent
                    color: "white"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: units.gu(1)
                    Label {
                        id: label
                        text: client.model.groupName(section)
                        fontSize: "large"
//                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    AbstractButton {
                        Layout.fillHeight: true
                        implicitWidth: seeAllLabel.implicitWidth + height
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            Label {
                                id: seeAllLabel
                                text: i18n.tr("See all")
                            }
                            Icon {
                                implicitHeight: parent.height
                                implicitWidth: height
                                name: "go-next"
                            }
                        }
                        onClicked: {
                            pageStack.push(Qt.resolvedUrl("AppStorePage.qml"), {pebble: root.pebble, link: client.model.groupLink(section), title: client.model.groupName(section)});
                        }
                    }
                }
            }

            footer: Item {
                height: client.model.links.length > 0 ? units.gu(6) : 0
                width: parent.width

                RowLayout {
                    anchors {
                        fill: parent
                        margins: units.gu(1)
                    }
                    spacing: units.gu(1)

                    Repeater {
                        model: client.model.links
                        Button {
                            text: client.model.linkName(client.model.links[index])
                            onClicked: client.fetchLink(client.model.links[index]);
                            color: UbuntuColors.orange
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            delegate: ListItem {
                height: delegateColumn.height + units.gu(2)

                RowLayout {
                    id: delegateRow
                    anchors.fill: parent
                    anchors.margins: units.gu(1)
                    spacing: units.gu(1)

                    AnimatedImage {
                        Layout.fillHeight: true
                        Layout.preferredWidth: height
                        source: model.icon
                        asynchronous: true
//                        sourceSize.width: width
//                        sourceSize.height: height
                    }

                    ColumnLayout {
                        id: delegateColumn
                        Layout.fillWidth: true;
                        Layout.fillHeight: true;
                        Label {
                            Layout.fillWidth: true
                            text: model.name
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: model.category
                        }
                        RowLayout {
                            Icon {
                                name: "like"
                                Layout.preferredHeight: parent.height
                                Layout.preferredWidth: height
                                implicitHeight: parent.height
                            }
                            Label {
                                Layout.fillWidth: true
                                text: model.hearts
                            }
                            Icon {
                                id: tickIcon
                                name: "tick"
                                implicitHeight: parent.height
                                Layout.preferredWidth: height
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
                        }
                    }

                }

                onClicked: {
                    client.fetchAppDetails(model.storeId);
                    pageStack.push(Qt.resolvedUrl("AppStoreDetailsPage.qml"), {app: appsFilterModel.get(index), pebble: root.pebble})
                }
            }
        }

//        RowLayout {
//            id: buttonRow
//            anchors { left: parent.left; bottom: parent.bottom; right: parent.right; margins: units.gu(1) }
//            spacing: units.gu(1)
//            Button {
//                text: i18n.tr("Previous")
//                Layout.fillWidth: true
//                enabled: client.offset > 0
//                onClicked: {
//                    client.previous()
//                }
//            }
//            Button {
//                text: i18n.tr("Next")
//                Layout.fillWidth: true
//                onClicked: {
//                    client.next()
//                }
//            }
//        }
    }

    ActivityIndicator {
        anchors.centerIn: parent
        running: client.busy
    }
}

