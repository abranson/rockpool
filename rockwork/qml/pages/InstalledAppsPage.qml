import QtQuick 2.2
import Sailfish.Silica 1.0
import RockPool 1.0

Page {
    id: root

    property var pebble: null
    property bool showWatchApps: false
    property bool showWatchFaces: false
    property var model: showWatchApps ? pebble.installedApps : pebble.installedWatchfaces

    AppStoreClient {
        id: client
        hardwarePlatform: pebble.hardwarePlatform
        model: root.model
    }
    SilicaListView {
        id: listView
        anchors.fill: parent
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
        }

        model: root.model
        clip: true

        delegate: InstalledAppDelegate {
            uuid: model.uuid
            name: model.name
            vendor: model.vendor
            version: model.version
            candidate: (model.latest && model.latest !== model.version)?model.latest:""
            iconSource: model.icon
            isLastApp: !listView.model.get(index+1)
            isSystemApp: model.isSystemApp
            hasSettings: model.hasSettings

            onDeleteApp: pebble.removeApp(model.uuid)
            onLaunchApp: pebble.launchApp(model.uuid)
            onConfigureApp: root.configureApp(model.uuid)
            onMoveApp: root.moveApp(index,dir)
            Component.onCompleted: if(!model.isSystemApp && !model.latest) { client.fetchAppDetails(model.storeId) }
            onUpgrade: pageStack.push(Qt.resolvedUrl("AppUpgradePage.qml"), {
                                           pebble: root.pebble,
                                           app_model: root.model,
                                           app_index: index
                                      })
        }
        VerticalScrollDecorator {}
    }
    function configureApp(uuid) {
        var popup = null;
        var cbacc = null;
        if (uuid === "{36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c}") {
            popup = pageStack.push(Qt.resolvedUrl("HealthSettingsDialog.qml"), {
                                            healthParams: pebble.healthParams
                                        },PageStackAction.Immediate);
            cbacc = function () {
                pebble.healthParams = popup.healthParams
            };
        } else if(uuid === "{61b22bc8-1e29-460d-a236-3fe409a439ff}") {
            popup = pageStack.push(Qt.resolvedUrl("WeatherSettingsDialog.qml"), {
                                            pebble: pebble
                                        },PageStackAction.Immediate);
        } else if(uuid === "{0863fc6a-66c5-4f62-ab8a-82ed00a98b5d}") {
            popup = pageStack.push(Qt.resolvedUrl("SendTextSettingsDialog.qml"), {
                                            pebble: pebble
                                        },PageStackAction.Immediate);
            cbacc = function () {
                pebble.setCannedContacts(popup.contacts)
            };
        }
        if(popup && cbacc) {
            popup.accepted.connect(cbacc);
        } else {
            pebble.requestConfigurationURL(uuid)
        }
    }
    function moveApp(idx,dir) {
        listView.model.move(idx,idx+dir);
        moveDock.show();
    }

    DockedPanel {
        id: moveDock
        dock: Dock.Top
        width: parent.width
        height: Theme.itemSizeSmall
        Button {
            text: qsTr("Save Apps Order")
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: {
                moveDock.hide();
                listView.model.commitMove();
            }
        }
    }
}
