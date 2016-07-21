import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var params: null
    property var contacts: []
    canAccept: false

    Column {
        width: parent.width

        DialogHeader {
            title: qsTr("Messaging Settings")
            defaultAcceptText: qsTr("OK")
            defaultCancelText: qsTr("Cancel")
        }

        SectionHeader {
            text: qsTr("Contacts")
        }
        Label {
            width: parent.width
            text: "Not implemented yet"
        }

        SectionHeader {
            text: qsTr("Messages")
        }
        Label {
            width: parent.width
            text: "Not implemented yet"
        }
    }

    onDone: {
        if(result === DialogResult.Accepted) {
            root.params["contacts"] = contacts;
        }
    }
}
