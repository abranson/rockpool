import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.TransferEngine 1.0

Page {
    id: pickerPage
    property alias contentType: contentPeerPicker.filter
    property string itemName
    property string filename
    property string itemDescription

    ShareMethodList {
        id: contentPeerPicker
        anchors.fill: parent
        header: PageHeader {
            title: qsTr("Share Via")
        }
        source: "file://"+filename
        content: {
            "linkTitle": itemName,
            "status": itemDescription,
            "type": contentPeerPicker.filter
        }
        ViewPlaceholder {
            enabled: contentPeerPicker.model.count === 0
            text: qsTr("No Share Providers configured. Please add provider's account in System Settings")
        }
    }
}
