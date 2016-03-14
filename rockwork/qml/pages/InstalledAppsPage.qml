import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3
import RockWork 1.0

Page {
    id: root
    title: showWatchApps ? (showWatchFaces ? i18n.tr("Apps & Watchfaces") : i18n.tr("Apps")) : i18n.tr("Watchfaces")

    property var pebble: null
    property bool showWatchApps: false
    property bool showWatchFaces: false

    head {
        actions: [
            Action {
                iconName: "add"
                onTriggered: pageStack.push(Qt.resolvedUrl("AppStorePage.qml"), {pebble: root.pebble, showWatchApps: root.showWatchApps, showWatchFaces: root.showWatchFaces})
            }
        ]
    }

    function configureApp(uuid) {
        // The health app is special :/
        if (uuid == "{36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c}") {
            var popup = PopupUtils.open(Qt.resolvedUrl("HealthSettingsDialog.qml"), root, {healthParams: pebble.healthParams});
            popup.accepted.connect(function() {
                pebble.healthParams = popup.healthParams
            })
        } else {
            pebble.requestConfigurationURL(uuid);
        }
    }

    Item {
        anchors.fill: parent
        ListView {
            id: listView
            anchors.fill: parent
            model: root.showWatchApps ? root.pebble.installedApps : root.pebble.installedWatchfaces
            clip: true
            property real realContentY: contentY + originY

            delegate: InstalledAppDelegate {
                id: delegate
                uuid: model.uuid
                name: model.name
                iconSource: model.icon
                vendor: model.vendor
                visible: dndArea.draggedIndex !== index
                hasGrip: index > 0
                isSystemApp: model.isSystemApp
                hasSettings: model.hasSettings

                onDeleteApp: {
                    pebble.removeApp(model.uuid)
                }
                onConfigureApp: {
                    root.configureApp(model.uuid)
                }
                onClicked: {
                    PopupUtils.open(dialogComponent, root, {app: listView.model.get(index)})
                }
            }
        }
        MouseArea {
            id: dndArea
            anchors {
                top: parent.top
                bottom: parent.bottom
                right: parent.right
            }
            drag.axis: Drag.YAxis
            propagateComposedEvents: true
            width: units.gu(5)

            property int startY: 0
            property int draggedIndex: -1


            onPressAndHold: {
                startY = mouseY;
                draggedIndex = Math.floor((listView.realContentY + mouseY) / fakeDragItem.height)
                if (draggedIndex == 0) {
                    print("cannot drag settings app");
                    return;
                }

                var draggedItem = listView.model.get(draggedIndex);
                fakeDragItem.uuid = draggedItem.uuid;
                fakeDragItem.name = draggedItem.name;
                fakeDragItem.vendor = draggedItem.vendor;
                fakeDragItem.iconSource = draggedItem.icon;
                fakeDragItem.isSystemApp = draggedItem.isSystemApp;
                fakeDragItem.y = (fakeDragItem.height * draggedIndex) - listView.realContentY
                drag.target = fakeDragItem;
            }

            onMouseYChanged: {
                var newIndex = Math.floor((listView.realContentY + mouseY) / fakeDragItem.height)

                if (newIndex > draggedIndex) {
                    newIndex = draggedIndex + 1;
                } else if (newIndex < draggedIndex) {
                    newIndex = draggedIndex - 1;
                } else {
                    return;
                }

                if (newIndex >= 1 && newIndex < listView.count) {
                    listView.model.move(draggedIndex, newIndex);
                    draggedIndex = newIndex;
                }
            }

            onReleased: {
                if (draggedIndex > -1) {
                    listView.model.commitMove();
                    draggedIndex = -1;
                    drag.target = null;
                }
            }
        }
    }



    InstalledAppDelegate {
        id: fakeDragItem
        visible: dndArea.draggedIndex != -1

    }

    Component {
        id: dialogComponent
        Dialog {
            id: dialog
            property var app: null

            RowLayout {
                SystemAppIcon {
                    height: titleCol.height
                    width: height
                    isSystemApp: app.isSystemApp
                    uuid: app.uuid
                    iconSource: app.icon
                }

                ColumnLayout {
                    id: titleCol
                    Layout.fillWidth: true

                    Label {
                        Layout.fillWidth: true
                        text: app.name
                        fontSize: "large"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: app.vendor
                    }
                }
            }

            Button {
                text: i18n.tr("Launch")
                color: UbuntuColors.green
                onClicked: {
                    pebble.launchApp(app.uuid);
                    PopupUtils.close(dialog);
                }
            }

            Button {
                text: i18n.tr("Configure")
                color: UbuntuColors.blue
                visible: app.hasSettings
                onClicked: {
                    root.configureApp(app.uuid);
                    PopupUtils.close(dialog);
                }
            }

            Button {
                text: i18n.tr("Delete")
                color: UbuntuColors.red
                visible: !app.isSystemApp
                onClicked: {
                    pebble.removeApp(app.uuid);
                    PopupUtils.close(dialog);
                }
            }

            Button {
                text: i18n.tr("Close")
                onClicked: PopupUtils.close(dialog)
            }
        }
    }
}
