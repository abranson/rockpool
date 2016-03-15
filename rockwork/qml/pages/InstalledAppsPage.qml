import QtQuick 2.2
import Sailfish.Silica 1.0
import RockPool 1.0

Page {
    id: root

    property var pebble: null
    property bool showWatchApps: false
    property bool showWatchFaces: false

    SilicaListView {
        id: listView
        anchors.fill: parent
        //pullDownMenu:
        PullDownMenu {
            MenuItem {
                text: qsTr("Add New")
                onClicked: pageStack.push(Qt.resolvedUrl("AppStorePage.qml"), {
                                              pebble: root.pebble,
                                              showWatchApps: root.showWatchApps,
                                              showWatchFaces: root.showWatchFaces
                                          })
            }
        }
        header: PageHeader {
            title: showWatchApps ? (showWatchFaces ? qsTr("Apps & Watchfaces") : qsTr("Apps")) : qsTr("Watchfaces")
            width: parent.width
        }

        model: root.showWatchApps ? root.pebble.installedApps : root.pebble.installedWatchfaces
        //clip: true

        delegate: InstalledAppDelegate {
            uuid: model.uuid
            name: model.name
            vendor: model.vendor
            iconSource: model.icon
            isSystemApp: model.isSystemApp
            hasSettings: model.hasSettings

            onDeleteApp: pebble.removeApp(model.uuid)
            onLaunchApp: pebble.launchApp(model.uuid)
            onConfigureApp: root.configureApp(model.uuid)
        }
        VerticalScrollDecorator {}
    }
    function configureApp(uuid) {
        // The health app is special :/
        if (uuid === "{36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c}") {
            var popup = pageStack.push(Qt.resolvedUrl("HealthSettingsDialog.qml"), {
                                            healthParams: pebble.healthParams
                                        },PageStackAction.Immediate);
            popup.accepted.connect(function () {
                pebble.healthParams = popup.healthParams
            })
        } else {
            pebble.requestConfigurationURL(uuid)
        }
    }
}
