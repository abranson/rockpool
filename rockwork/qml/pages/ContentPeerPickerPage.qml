import QtQuick 2.4
import Ubuntu.Components 1.3
import Ubuntu.Content 1.3
import RockWork 1.0

Page {
    id: pickerPage
    head {
        locked: true
        visible: false
    }

    property alias contentType: contentPeerPicker.contentType
    property string itemName
    property alias handler: contentPeerPicker.handler
    property string filename

    Component {
        id: exportItemComponent
        ContentItem {
            name: pickerPage.itemName
        }
    }
    ContentPeerPicker {
        id: contentPeerPicker
        anchors.fill: parent

        onCancelPressed: pageStack.pop()

        onPeerSelected: {
            var transfer = peer.request();
            var items = [];
            var item = exportItemComponent.createObject();
            item.url = "file://" + pickerPage.filename;
            items.push(item)
            transfer.items = items;
            transfer.state = ContentTransfer.Charged;
            pageStack.pop();
        }
    }
}
