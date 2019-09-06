import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    property var app_model: null
    property int app_index: model.indexOf(app)
    property var app: app_model.get(app_index)

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
                            text: app ? app.version : ""
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
            Button {
                id: installButton
                width: parent.width
                enabled: !installing && !root.app.companion
                property bool installing: false
                text: enabled ? qsTr("Upgrade") : (installing ? qsTr("Upgrading…") : qsTr("Needs Companion"))
                Connections {
                    target: root.pebble.installedApps
                    onChanged: pageStack.pop()
                }
                Connections {
                    target: root.pebble.installedWatchfaces
                    onChanged: pageStack.pop()
                }
                onClicked: {
                    root.pebble.installApp(root.app.storeId)
                    installButton.installing = true
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
            Row {
                width: parent.width
                visible: root.app != null
                Column {
                    width: parent.width / 3
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Classic"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: app ? app.compatibility.aplite : "?"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                }
                Column {
                    width: parent.width / 3
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Time"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: app ? app.compatibility.basalt : "?"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                }
                Column {
                    width: parent.width / 3
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Time Round"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: app ? app.compatibility.chalk : "?"
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: true
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
