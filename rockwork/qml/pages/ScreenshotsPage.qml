import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root
    allowedOrientations: Orientation.All
    property var pebble: null

    Drawer {
        id: pageDrawer
        anchors.fill: parent
        SilicaGridView {
            id: grid
            clip: true
            anchors.fill: parent

            property int columns: Math.round(width/200)

            cellWidth: width / columns
            cellHeight: cellWidth

            header: PageHeader {
                title: qsTr("Screenshots")
            }
            PullDownMenu {
                MenuItem {
                    text: qsTr("Take Screenshot")
                    onClicked: root.pebble.requestScreenshot()
                }
            }

            model: root.pebble.screenshots
            currentIndex: -1
            delegate: ListItem {
                contentWidth: grid.cellWidth
                contentHeight: grid.cellHeight
                Image {
                    anchors.centerIn: parent
                    fillMode: Image.PreserveAspectFit
                    source: "file://" + model.filename
                    opacity: (grid.currentIndex>=0 && grid.currentIndex != index) ? 0.4 : 1
                }
                onClicked: grid.currentIndex = index
            }
        }
        open: grid.currentIndex>=0
        backgroundSize: menu.childrenRect.height + Theme.paddingMedium
        background: Column {
            id: menu
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.paddingSmall
            Button {
                text: qsTr("Share")
                onClicked: pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                    itemName: qsTr("Pebble screenshot"),
                    itemDescription: qsTr("Screen snapshot of Pebble Smartwatches"),
                    contentType: "image/jpeg",
                    filename: grid.model.get(grid.currentIndex)
                });
            }
            Button {
                text: qsTr("Delete")
                onClicked: {
                    var index = grid.currentIndex;
                    grid.currentItem.remorseAction(qsTr("Really Delete?"),function(){
                        root.pebble.removeScreenshot(root.pebble.screenshots.get(index))
                    });
                    grid.currentIndex=-1;
                }
            }
            Button {
                text: qsTr("Cancel")
                onClicked: grid.currentIndex=-1
            }
        }
    }
}

