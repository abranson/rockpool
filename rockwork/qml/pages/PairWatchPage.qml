import QtQuick 2.2
import Sailfish.Silica 1.0

// BLE watches pair through the daemon (libpebble3d), not the system
// Bluetooth settings: scanning and connecting go over org.rockwork.
Page {
    id: pairPage
    property string connectingTo: ""
    property string failureMessage: ""

    Component.onCompleted: pebbles.startScan()
    Component.onDestruction: {
        if (pairPage.connectingTo !== "") {
            pebbles.disconnectWatch(pairPage.connectingTo)
        }
        if (rockPool.pendingPairAddress === pairPage.connectingTo) {
            rockPool.pendingPairAddress = ""
        }
        pebbles.stopScan()
    }

    // Pairing succeeded once the newly known watch reports its address. An
    // unrelated watch-list change must not retire this pending connection.
    Connections {
        target: pebbles
        onPebbleIdentityAvailable: {
            if (pairPage.connectingTo !== ""
                    && address.toLowerCase()
                       === pairPage.connectingTo.toLowerCase()) {
                rockPool.pendingPairAddress = ""
                pairPage.connectingTo = ""
            }
        }
    }

    // Backstop: a pairing that fails before the watch ever becomes known (e.g. createBond fails)
    // never changes the watch count, so without this the spinner runs forever. Resume scanning so
    // the user can retry.
    Timer {
        interval: 60000
        running: pairPage.connectingTo !== ""
        onTriggered: {
            pairPage.failureMessage = qsTr("Pairing timed out. Put the watch in pairing mode and try again.")
            pebbles.disconnectWatch(pairPage.connectingTo)
            rockPool.pendingPairAddress = ""
            pairPage.connectingTo = ""
            pebbles.startScan()
        }
    }

    SilicaListView {
        id: resultsView
        anchors.fill: parent
        model: pebbles.scanResults

        header: PageHeader {
            title: qsTr("Pair a Pebble")
            description: pairPage.failureMessage !== "" ? pairPage.failureMessage
                         : pebbles.scanning ? qsTr("Scanning for watches…")
                                            : qsTr("Scan stopped")
        }

        PullDownMenu {
            visible: pairPage.connectingTo === ""
            MenuItem {
                text: pebbles.scanning ? qsTr("Stop scanning") : qsTr("Scan again")
                onClicked: pebbles.scanning ? pebbles.stopScan() : pebbles.startScan()
            }
        }

        delegate: ListItem {
            contentHeight: Theme.itemSizeMedium
            enabled: pairPage.connectingTo === ""

            Column {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter

                Label {
                    text: modelData.name
                    color: highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    text: pairPage.connectingTo === modelData.address
                          ? qsTr("Connecting…")
                          : modelData.address + "   " + modelData.rssi + " dBm"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                }
            }

            onClicked: {
                pairPage.failureMessage = ""
                pairPage.connectingTo = modelData.address
                rockPool.pendingPairAddress = modelData.address
                // Keep the radio free for the connection attempt.
                pebbles.stopScan()
                pebbles.connectWatch(modelData.address)
            }
        }

        ViewPlaceholder {
            enabled: resultsView.count === 0
            text: qsTr("Searching for Pebble watches")
            hintText: qsTr("Put the watch in pairing mode: Settings → Bluetooth on the watch.")
        }

        VerticalScrollDecorator {}
    }

    BusyIndicator {
        size: BusyIndicatorSize.Large
        anchors.centerIn: parent
        running: pairPage.connectingTo !== ""
                 || (pebbles.scanning && resultsView.count === 0)
    }
}
