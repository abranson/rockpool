import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var params: null
    property var locations: []
    canAccept: false

    Column {
        width: parent.width

        DialogHeader {
            title: qsTr("Weather Settings")
            defaultAcceptText: qsTr("OK")
            defaultCancelText: qsTr("Cancel")
        }

        ComboBox {
            label: qsTr("Units")
            menu: ContextMenu {
                MenuItem { text: qsTr("Celsius") }
                MenuItem { text: qsTr("Fahrenheit") }
            }
        }

        SectionHeader {
            text: qsTr("Locations")
        }

        Label {
            width: parent.width
            text: "Not implemented yet"
        }
    }

    onDone: {
        if(result === DialogResult.Accepted) {
            root.params["locations"] = locations;
        }
    }
}
