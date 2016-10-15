import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: pickerPage
    canAccept: false
    property var selected: null

    Column {
        width: parent.width
        DialogHeader {
            title: qsTr("Select Location")
            defaultAcceptText: ""
            defaultCancelText: qsTr("Cancel")
        }
        TextField {
            width: parent.width
            label: qsTr("Location Name")
            placeholderText: qsTr("Type in location name")
            onTextChanged: getHints(text)
        }

        ListModel {
            id: locModel
        }

        SilicaListView {
            id: locPicker
            width: parent.width
            height: contentItem.childrenRect.height

            ViewPlaceholder {
                enabled: locModel.count === 0
                text: qsTr("Matching locations will be appearing as you type")
            }

            model: locModel
            delegate: ListItem {
                id: liLoc
                anchors.horizontalCenter: parent.horizontalCenter
                width: locPicker.width-Theme.paddingSmall
                contentHeight: Theme.itemSizeSmall
                Label {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.name
                }
                Column {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: parent.width/4
                    Row {
                        Label {
                            text: "LAT: "
                            font.pixelSize: Theme.fontSizeTiny
                            horizontalAlignment: Text.AlignRight
                        }
                        Label {
                            text: model.lat
                            font.pixelSize: Theme.fontSizeTiny
                        }
                    }
                    Row {
                        Label {
                            text: "LON: "
                            font.pixelSize: Theme.fontSizeTiny
                            horizontalAlignment: Text.AlignRight
                        }
                        Label {
                            text: model.lng
                            font.pixelSize: Theme.fontSizeTiny
                        }
                    }
                }
                onClicked: {
                    selected = model;
                    pickerPage.canAccept=true;
                    pickerPage.accept();
                }
            }
        }
    }
    function getHints(blah) {
        if(blah && blah.length === 0) return;
        var url = "http://autocomplete.wunderground.com/aq?query="+blah;
        var xhr = new XMLHttpRequest();
        xhr.open("GET",url);
        xhr.onreadystatechange = function() {
            if(xhr.readyState === xhr.DONE) {
                if(xhr.status === 200) {
                    var json = JSON.parse(xhr.responseText);
                    if(json.hasOwnProperty("RESULTS") && json.RESULTS.length > 0) {
                        locModel.clear();
                        for(var i=0;i<json.RESULTS.length;i++) {
                            var loc = json.RESULTS[i];
                            if(loc.type !== "city") continue;
                            locModel.append({"name":loc.name,"lat":loc.lat,"lng":loc.lon});
                        }
                    } else if(!json.hasOwnProperty("RESULTS"))
                        console.log("WTF",json,xhr.responseText);
                }
            }
        };
        xhr.send();
    }
}
