import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null

    SilicaGridView {
        id: grid
        anchors.fill: parent
        clip: true

        property int columns: Math.round(width/200)

        cellWidth: width / columns - Theme.paddingSmall
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

        delegate: ListItem {
            //width: grid.cellWidth
            //height: grid.cellHeight
            contentWidth: grid.cellWidth
            contentHeight: grid.cellHeight
            Image {
                //anchors.fill: parent
                //anchors.margins: Theme.paddingSmall
                anchors.centerIn: parent
                fillMode: Image.PreserveAspectFit
                source: "file://" + model.filename
            }
            menu: ContextMenu {
                closeOnActivation: true
                MenuItem {
                    text: qsTr("Share")
                    onClicked: pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                        itemName: qsTr("Pebble screenshot"),
                        handler: ContentHandler.Share,
                        contentType: ContentType.Pictures,
                        filename:model.filename
                    })
                }
                MenuItem {
                    text: qsTr("Save")
                    onClicked: pageStack.push(Qt.resolvedUrl("ContentPeerPickerPage.qml"), {
                        itemName: qsTr("Pebble screenshot"),
                        handler: ContentHandler.Destination,
                        contentType: ContentType.Pictures,
                        filename: model.filename
                    })
                }
                MenuItem {
                    text: qsTr("Delete")
                    onClicked: root.pebble.removeScreenshot(model.filename)
                }
            }
        }
    }
}

