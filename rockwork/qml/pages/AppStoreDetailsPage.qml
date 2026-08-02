import QtQuick 2.2
import Sailfish.Silica 1.0
import QtGraphicalEffects 1.0

Page {
    id: root

    property var pebble: null
    property var app: null
    property bool appMutationsAllowed: rockPool.knownPebbleCount === 1
    property bool appInstallationAllowed: appMutationsAllowed && pebble && pebble.connected

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: contentColumn.height
        clip: true

        Column {
            id: contentColumn
            width: parent.width
            height: childrenRect.height

            PageHeader {
                title: " "
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
            Label {
                text: root.app.name //qsTr("App details")
                font.pixelSize: Theme.fontSizeLarge
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Image {
                width: parent.width
                // ss.w : ss.h = w : h
                height: sourceSize.height * width / sourceSize.width
                fillMode: Image.PreserveAspectFit
                source: root.app.headerImage
            }

            Row {
                anchors {
                    left: parent.left
                    right: parent.right
                }
                height: Theme.iconSizeMedium

                Item {
                    width: parent.width/2 - Theme.paddingSmall*2
                    height: parent.height
                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.paddingSmall
                        Image {
                            source: "image://theme/icon-s-like"
                            height: parent.height
                            width: height
                        }
                        Label {
                            text: root.app.hearts
                        }
                    }
                }

                Separator {
                    width: Theme.paddingSmall
                    height: parent.height
                    color: Theme.secondaryHighlightColor
                }

                Item {
                    width: parent.width/2 - Theme.paddingSmall*2
                    height: parent.height
                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.paddingSmall
                        Image {
                            source: "image://theme/icon-m-" + (root.app.isWatchFace ? "watch" : "toy")
                            height: parent.height
                            width: height
                        }
                        Label {
                            text: root.app.isWatchFace ? "Watchface" : "Watchapp"
                        }
                    }
                }
            }

            Column {
                anchors { left: parent.left; right: parent.right; margins: Theme.horizontalPageMargin }
                spacing: Theme.paddingSmall

                PebbleModels {
                    id: modelModel
                }


                Item {
                    id: screenshotsItem
                    width: parent.width
                    height: watchImage.height

                    property var watchModel: modelModel.getOrFallback(root.pebble.model)
                    property bool isRound: watchModel.shape === "round"

                    ListView {
                        id: screenshotsListView
                        anchors.centerIn: parent
                        width: parent.width
                        height: screenshotsItem.watchModel.screenHeight
                        orientation: ListView.Horizontal
                        spacing: Theme.paddingSmall
                        snapMode: ListView.SnapToItem
                        preferredHighlightBegin: (width - screenshotWidth) / 2
                        preferredHighlightEnd: (width + screenshotWidth) / 2
                        highlightRangeMode: ListView.StrictlyEnforceRange

                        property real screenshotWidth: height
                                                       * screenshotsItem.watchModel.screenWidth
                                                       / screenshotsItem.watchModel.screenHeight

                        model: root.app.screenshotImages
                        delegate: AnimatedImage {
                            height: screenshotsListView.height
                            width: height * screenshotsItem.watchModel.screenWidth
                                   / screenshotsItem.watchModel.screenHeight
                            fillMode: Image.PreserveAspectFit
                            source: modelData
                        }
                        //visible: false
                    }
                    Image {
                        id: watchImage
                        height: (sourceSize.height ? sourceSize.height : 350)
                        width: (sourceSize.width ? sourceSize.width : 251)
                        fillMode: Image.PreserveAspectFit
                        anchors.centerIn: parent
                        source: screenshotsItem.watchModel.image
                        Rectangle {
                            color: "black"
                            width: maskFace.width
                            height: maskFace.height
                            anchors.centerIn: parent
                            radius: screenshotsItem.isRound ? height / 2 : 0
                        }
                    }

                    OpacityMask {
                        anchors.fill: screenshotsListView
                        anchors.centerIn: parent
                        source: screenshotsListView
                        maskSource: maskRect
                        z: 5
                    }

                    Rectangle {
                        id: maskRect
                        anchors.fill: screenshotsListView
                        color: "transparent"
                        visible: false

                        Rectangle {
                            id: maskFace
                            color: "blue"
                            anchors.centerIn: parent
                            height: parent.height
                            width: height * screenshotsItem.watchModel.screenWidth
                                   / screenshotsItem.watchModel.screenHeight
                            radius: screenshotsItem.isRound ? height / 2 : 0
                        }
                    }

                }

                Label {
                    width: parent.width
                    font.bold: true
                    text: qsTr("Description")
                }

                Separator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: Theme.paddingSmall
                    width: parent.width
                    color: Theme.secondaryHighlightColor
                }

                Label {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.app.description
                }

                Grid {
                    width: parent.width
                    spacing: Theme.paddingSmall
                    columns: 2
                    Label {
                        text: qsTr("Developer")
                        font.bold: true
                        width: parent.width/2
                    }
                    Label {
                        text: qsTr("Version")
                        font.bold: true
                        width: parent.width/2
                    }
                    Label {
                        text: root.app.vendor
                    }
                    Label {
                        text: root.app.version
                    }
                }
            }
        }
    }
    DockedPanel {
        id: appDock
        width: parent.width
        height: headerColumn.height
        dock: Dock.Top
        Item {
            anchors.fill: parent
            anchors.margins: Theme.paddingSmall

            Image {
                id: appIcon
                anchors { left: parent.left; top: parent.top; margins: Theme.paddingSmall }
                height: headerColumn.height
                width: height
                source: root.app.icon
            }

            Column {
                anchors { left: appIcon.right; right: installButton.left; top: parent.top; margins: Theme.paddingSmall }
                id: headerColumn
                width: parent.width-installButton.width
                Label {
                    text: root.app.name
                    elide: Text.ElideRight
                    width: parent.width
                }
                Label {
                    text: root.app.vendor
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            Button {
                id: installButton
                anchors { right: parent.right; top: parent.top; margins: Theme.paddingSmall }
                enabled: root.appInstallationAllowed && !installed && !installing && !root.app.companion
                text: installing && !installed ? qsTr("Installing...")
                                               : (root.app.companion ? qsTr("Needs Companion")
                                                                     : (installed ? qsTr("Installed") : qsTr("Install")))
                property bool installing: false
                property bool installed: root.pebble.installedApps.contains(root.app.storeId) || root.pebble.installedWatchfaces.contains(root.app.storeId)
                Connections {
                    target: root.pebble.installedApps
                    onChanged: {
                        installButton.installed = root.pebble.installedApps.contains(root.app.storeId) || root.pebble.installedWatchfaces.contains(root.app.storeId)
                    }
                }

                Connections {
                    target: root.pebble.installedWatchfaces
                    onChanged: {
                        installButton.installed = root.pebble.installedApps.contains(root.app.storeId) || root.pebble.installedWatchfaces.contains(root.app.storeId)
                    }
                }

                onClicked: {
                    if (!root.appInstallationAllowed)
                        return

                    root.pebble.installApp(root.app.storeId)
                    installButton.installing = true
                }
            }
        }
    }
    Component.onCompleted: appDock.show();
}
