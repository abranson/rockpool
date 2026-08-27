import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    SilicaListView {
        anchors.fill: parent
        model: pebbles
        header: PageHeader {
            title: qsTr("RockPool")
            description: qsTr("Manage Pebble Watches")
        }
        PullDownMenu {
            MenuItem {
                text: qsTr("Restart service")
                onClicked: rockPool.restartService()
            }
            MenuItem {
                text: qsTr("Pair new watch")
                onClicked: pageStack.push(Qt.resolvedUrl("PairWatchPage.qml"))
            }
        }

        delegate: ListItem {
            id: watchItem
            // Not fontSizeMedium*2: that is twice the font *size*, and a label's line height is
            // taller than its font size, so the two labels overflowed the highlight.
            // itemSizeSmall is Silica's height for a two-line item.
            contentHeight: Theme.itemSizeSmall
            ListView.onRemove: animateRemoval(watchItem)

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Connect")
                    // A known watch that isn't connected or already trying: re-arm the daemon's
                    // connect goal. (connectionState: 0=Disconnected, 4=Failed.)
                    visible: model.connectionState === 0 || model.connectionState === 4
                    onClicked: pebbles.connectWatch(model.address)
                }
                MenuItem {
                    text: qsTr("Disconnect")
                    // While connected or attempting: clears the daemon's connect goal, which also
                    // lets the user cancel a watch stuck retrying. (states 1/2/3.)
                    visible: model.connectionState === 1 || model.connectionState === 2 || model.connectionState === 3
                    onClicked: pebbles.disconnectWatch(model.address)
                }
                MenuItem {
                    text: qsTr("Forget watch")
                    onClicked: watchItem.remorseAction(qsTr("Forgetting watch"), function() {
                        pebbles.forgetWatch(model.address)
                    })
                }
            }

            Column {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargins
                    rightMargin: Theme.horizontalPageMargins
                    verticalCenter: parent.verticalCenter
                }

                Label {
                    width: parent.width
                    text: model.name
                    truncationMode: TruncationMode.Fade
                    color: watchItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                Label {
                    width: parent.width
                    // connectionState: 0=Disconnected 1=Connecting 2=Negotiating 3=Connected 4=Failed
                    text: {
                        switch (model.connectionState) {
                        case 1: return qsTr("Connecting…")
                        case 2: return qsTr("Negotiating…")
                        case 3: return qsTr("Connected")
                        case 4: return qsTr("Connection failed")
                        default: return qsTr("Disconnected")
                        }
                    }
                    font.pixelSize: Theme.fontSizeSmall
                    truncationMode: TruncationMode.Fade
                    color: watchItem.highlighted ? Theme.secondaryHighlightColor
                                                 : Theme.secondaryColor
                }
            }

            onClicked: {
                var p = pebbles.get(index);
                print("opening pebble:", p.name, p.hardwarePlatform)
                rockPool.curPebble=index;
                pageStack.push(Qt.resolvedUrl("MainMenuPage.qml"), {pebble: pebbles.get(index)})
            }
        }
    }

    ViewPlaceholder {
        anchors.fill: parent
        enabled: pebbles.count === 0

        Label {
            text: qsTr("No Pebble smartwatches configured yet. Put your watch in pairing mode and pair it from here.")
            font.pixelSize: Theme.fontSizeLarge
            width: parent.width-(Theme.paddingSmall*2)
            anchors.centerIn: parent
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Button {
            text: qsTr("Pair new watch")
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            onClicked: pageStack.push(Qt.resolvedUrl("PairWatchPage.qml"))
        }
    }
}
