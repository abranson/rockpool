import QtQuick 2.6
import Sailfish.Silica 1.0
import RockPool 1.0

Page {
    id: root

    property var pebble: null
    property var app_model: null
    property int app_index: -1
    property string app_uuid
    property var app: app_model && app_index >= 0 ? app_model.get(app_index) : null
    property bool appMutationsAllowed: rockPool.knownPebbleCount === 1
    property bool appInstallationAllowed: appMutationsAllowed && pebble && pebble.connected
    readonly property string installedSource: app ? app.storeSource : ""
    readonly property string candidateSource: app ? app.updateStoreSource : ""
    readonly property bool crossStore: installedSource.length > 0 && candidateSource.length > 0
                                       && installedSource !== candidateSource
    readonly property bool switchBuild: crossStore && app.switchAvailable
    readonly property bool canInstallCandidate: appInstallationAllowed && app !== null && !app.companion
                                                && installedSource.length > 0 && candidateSource.length > 0
                                                && candidateSource === rockPool.preferredAppstore
                                                && app.updateStoreId.length > 0
                                                && (switchBuild || (!app.switchAvailable && app.latest !== app.version))
    property string expectedStoreId
    property string expectedVersion
    property string installError

    function storeName(source) {
        if (source === "core")
            return "Core Devices"
        if (source === "rebble")
            return "Rebble"
        return ""
    }

    function candidateActionText() {
        if (switchBuild)
            return qsTr("Switch build to %1").arg(storeName(candidateSource))
        if (crossStore)
            return qsTr("Upgrade from %1").arg(storeName(candidateSource))
        return qsTr("Upgrade")
    }

    function installCandidate(switching) {
        if (!canInstallCandidate || installButton.installing || switching !== switchBuild)
            return
        installError = ""
        expectedStoreId = app.updateStoreId
        expectedVersion = app.latest
        installButton.switching = switching
        installButton.installing = true
        if (switching)
            pebble.switchAppBuild(app.uuid, expectedStoreId)
        else
            pebble.upgradeApp(app.uuid, expectedStoreId)
    }

    Component {
        id: switchConfirmation

        Dialog {
            id: confirmation

            property string targetUuid
            property string installedId
            property string installedVersion
            property string candidateId
            property string candidateVersion
            property string candidateSourceName
            canAccept: root.canInstallCandidate && root.switchBuild && !installButton.installing
                       && root.app.uuid === targetUuid && root.app.storeId === installedId
                       && root.app.version === installedVersion && root.app.updateStoreId === candidateId
                       && root.app.latest === candidateVersion
            onAccepted: {
                if (canAccept)
                    root.installCandidate(true)
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium

                DialogHeader {
                    acceptText: qsTr("Switch build")
                }
                Label {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Install the %1 build of version %2?").arg(confirmation.candidateSourceName).arg(confirmation.candidateVersion)
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                }
            }
        }
    }

    AppStoreClient {
        id: client
        source: rockPool.preferredAppstore === "core" ? AppStoreClient.CoreDevices : AppStoreClient.Rebble

        hardwarePlatform: root.pebble ? root.pebble.hardwarePlatform : ""
        model: root.app_model
    }

    function findRequestedApp() {
        if (!app_uuid || !pebble)
            return null

        var models = [pebble.installedApps, pebble.installedWatchfaces]
        for (var m = 0; m < models.length; ++m) {
            for (var i = 0; ; ++i) {
                var item = models[m].get(i)
                if (!item)
                    break
                if (item.uuid.toLowerCase() === app_uuid.toLowerCase())
                    return { model: models[m], index: i, item: item }
            }
        }
        return null
    }

    function resolveRequestedApp(force) {
        var requested = findRequestedApp()
        if (!requested) {
            app_index = -1
            return
        }
        if (!force && requested.item === app)
            return

        app_index = -1
        app_model = requested.model
        app_index = requested.index
        client.fetchInstalledListing(requested.item, rockPool.preferredAppstore, false)
    }

    // The two models are cleared and repopulated together by a single D-Bus reply.
    Timer {
        id: applicationsReloaded

        interval: 0
        onTriggered: {
            var requested = root.findRequestedApp()
            if (installButton.installing) {
                installButton.installing = false
                if (requested && requested.item.storeId.toLowerCase() === root.expectedStoreId.toLowerCase()
                        && requested.item.version === root.expectedVersion) {
                    if (pageStack.currentPage === root)
                        pageStack.pop()
                } else {
                    root.installError = qsTr("Could not install the selected build.")
                }
                root.expectedStoreId = ""
                root.expectedVersion = ""
            }
            root.resolveRequestedApp()
        }
    }

    Connections {
        target: root.pebble ? root.pebble.installedApps : null
        onChanged: applicationsReloaded.restart()
    }
    Connections {
        target: root.pebble ? root.pebble.installedWatchfaces : null
        onChanged: applicationsReloaded.restart()
    }
    Connections {
        target: rockPool
        onPreferredAppstoreChanged: root.resolveRequestedApp(true)
    }
    Connections {
        target: client
        onHardwarePlatformChanged: root.resolveRequestedApp(true)
    }
    Component.onCompleted: resolveRequestedApp()
    property var compatibilityPlatforms: [
        { label: "Classic", key: "aplite" },
        { label: "Time", key: "basalt" },
        { label: "Time Round", key: "chalk" },
        { label: "Pebble 2", key: "diorite" },
        { label: "Pebble 2 SE", key: "emery" },
        { label: "Core Time 2", key: "flint" },
        { label: "Core 2 Duo", key: "gabbro" }
    ]

    function compatibilityValue(key) {
        if (!app || !app.compatibility || app.compatibility[key] === undefined)
            return "-"

        return app.compatibility[key]
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: contentColumn.height
        clip: true

        Column {
            id: contentColumn
            width: parent.width
            height: childrenRect.height
            spacing: Theme.paddingSmall

            PageHeader {
                title: app ? app.name : qsTr("Upgrading")
                description: app ? app.vendor : qsTr("Upgrading")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !root.appMutationsAllowed
                text: qsTr("App changes are available only when exactly one watch is paired.")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.appMutationsAllowed && !root.appInstallationAllowed
                text: qsTr("Connect the watch to install apps.")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }

            Row {
                width: parent.width
                height: Theme.iconSizeLarge
                visible: app != null
                Image {
                    source: app ? root.app.icon : ""
                    height: parent.height
                    width: height
                }
                Separator {
                    width: Theme.paddingSmall
                    height: parent.height
                    color: Theme.secondaryHighlightColor
                }
                Row {
                    width: parent.width - parent.height - Theme.paddingSmall
                    spacing: Theme.paddingSmall
                    visible: app != null
                    Column {
                        width: parent.width/2
                        Label {
                            text: qsTr("Version")
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                        Label {
                            text: app ? (app.latest && !root.switchBuild ? app.version + " → " + app.latest : app.version) : ""
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                    Column {
                        width: parent.width / 2
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: app && app.isWatchFace ? "Watchface" : "Watchapp"
                        }
                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            source: "image://theme/icon-m-" + (app && app.isWatchFace ? "watch" : "toy")
                            height: Theme.iconSizeSmall
                            width: height
                        }
                    }
                }
            }
            Column {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.crossStore
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    text: qsTr("Linked to %1").arg(root.storeName(root.installedSource))
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
                Label {
                    width: parent.width
                    text: qsTr("Selected build from %1").arg(root.storeName(root.candidateSource))
                    color: Theme.secondaryColor
                    wrapMode: Text.Wrap
                }
            }
            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.installError.length > 0
                text: root.installError
                wrapMode: Text.Wrap
            }
            Button {
                id: installButton

                width: parent.width
                enabled: root.canInstallCandidate && !installing
                property bool installing
                property bool switching
                text: {
                    if (installing)
                        return switching ? qsTr("Switching build…") : qsTr("Upgrading...")
                    if (root.app && root.app.companion)
                        return qsTr("Needs Companion")
                    return root.candidateActionText()
                }
                onClicked: {
                    if (!root.canInstallCandidate)
                        return
                    if (!root.switchBuild) {
                        root.installCandidate(false)
                        return
                    }
                    pageStack.push(switchConfirmation, {
                        targetUuid: root.app.uuid,
                        installedId: root.app.storeId,
                        installedVersion: root.app.version,
                        candidateId: root.app.updateStoreId,
                        candidateVersion: root.app.latest,
                        candidateSourceName: root.storeName(root.candidateSource)
                    })
                }
            }
            Label {
                text: qsTr("Compatibility")
            }
            Row {
                width: parent.width
                visible: root.app != null
                Column {
                    width: parent.width / 2
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Android"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: "image://theme/icon-s-"+(app && app.compatibility.android ? "installed" : "high-importance")
                    }
                }
                Column {
                    width: parent.width / 2
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "iOS"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: "image://theme/icon-s-"+(app && app.compatibility.ios ? "installed" : "high-importance")
                    }
                }
            }
            Flow {
                width: parent.width
                visible: root.app != null
                spacing: Theme.paddingSmall

                Repeater {
                    model: root.compatibilityPlatforms

                    delegate: Column {
                        width: (contentColumn.width - Theme.paddingSmall) / 2

                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: modelData.label
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                            wrapMode: Text.Wrap
                        }

                        Label {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: root.compatibilityValue(modelData.key)
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                        }
                    }
                }
            }
            Label {
                text: qsTr("Change Log")
            }
            Repeater {
                model: root.app ? root.app.changeLog : 0
                delegate: Column {
                    width: contentColumn.width
                    Separator {
                        width: parent.width
                        color: Theme.secondaryHighlightColor
                        height: Theme.paddingSmall
                    }

                    Row {
                        width: parent.width
                        Label {
                            width: parent.width / 3
                            text: app.changeLog[index].version
                            horizontalAlignment: Text.AlignHCenter
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Label {
                            width: parent.width / 3
                            text: app.changeLog[index].published_date
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                    Label {
                        width: parent.width
                        text: app.changeLog[index].release_notes
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }
    }
}
