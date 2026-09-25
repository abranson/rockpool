import QtQuick 2.6
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

        PullDownMenu {
            MenuItem {
                id: installMenuItem

                enabled: root.appInstallationAllowed && !installed && !installing && !root.app.companion
                text: installing && !installed ? qsTr("Installing...")
                                               : (root.app.companion ? qsTr("Needs Companion")
                                                                     : (installed ? qsTr("Installed") : qsTr("Install")))
                property bool installing
                property bool installed: root.pebble.installedApps.contains(root.app.storeId) || root.pebble.installedWatchfaces.contains(root.app.storeId)
                Connections {
                    target: root.pebble.installedApps
                    onChanged: {
                        installMenuItem.installed = root.pebble.installedApps.contains(root.app.storeId) || root.pebble.installedWatchfaces.contains(root.app.storeId)
                    }
                }

                Connections {
                    target: root.pebble.installedWatchfaces
                    onChanged: {
                        installMenuItem.installed = root.pebble.installedApps.contains(root.app.storeId) || root.pebble.installedWatchfaces.contains(root.app.storeId)
                    }
                }

                onClicked: {
                    if (!root.appInstallationAllowed)
                        return

                    root.pebble.installApp(root.app.storeId)
                    installMenuItem.installing = true
                }
            }
        }

        Column {
            id: contentColumn

            width: parent.width
            height: childrenRect.height
            spacing: 2 * Theme.paddingLarge

            PageHeader {
                title: root.app.name
                description: root.app.vendor
                descriptionWrapMode: Text.Wrap
                leftMargin: Theme.horizontalPageMargin + appIcon.width + Theme.paddingMedium

                Image {
                    id: appIcon

                    anchors.left: parent.left
                    anchors.leftMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSizeLarge
                    height: Math.min(width, parent.height - 2 * Theme.paddingMedium)
                    fillMode: Image.PreserveAspectFit
                    source: root.app.icon
                }
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
                spacing: Theme.paddingLarge

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
                    columns: 2
                    columnSpacing: Theme.paddingSmall
                    rowSpacing: Theme.paddingLarge

                    Label {
                        text: qsTr("Developer")
                        font.bold: true
                        width: (parent.width - parent.columnSpacing) / 2
                    }
                    Label {
                        text: qsTr("Version")
                        font.bold: true
                        width: (parent.width - parent.columnSpacing) / 2
                    }
                    Label {
                        text: root.app.vendor
                    }
                    Label {
                        text: root.app.version
                    }
                }
            }

            Item {
                width: 1
                height: Theme.itemSizeLarge
            }
        }
    }
}
