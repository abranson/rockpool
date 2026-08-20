import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: pickerPage

    canAccept: false
    property var selected: null
    property var activeRequest: null
    property int searchGeneration
    property string pendingQuery

    Timer {
        id: searchTimer

        interval: 350
        onTriggered: pickerPage.startLookup(pickerPage.pendingQuery,
                                               pickerPage.searchGeneration)
    }

    Component.onDestruction: {
        searchGeneration++
        searchTimer.stop()
        if (activeRequest) {
            activeRequest.abort()
            activeRequest = null
        }
    }

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
            onTextChanged: pickerPage.scheduleLookup(text)
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

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            textFormat: Text.RichText
            text: qsTr("Location search and forecasts by <a href=\"https://open-meteo.com/\">Open-Meteo</a>")
            onLinkActivated: Qt.openUrlExternally(link)
        }
    }

    function scheduleLookup(text) {
        searchGeneration++
        pendingQuery = text.trim()
        searchTimer.stop()
        if (activeRequest) {
            activeRequest.abort()
            activeRequest = null
        }
        locModel.clear()
        if (pendingQuery.length >= 2) {
            searchTimer.restart()
        }
    }

    function startLookup(query, generation) {
        if (generation !== searchGeneration || query.length < 2) {
            return
        }

        var request = new XMLHttpRequest()
        activeRequest = request
        var url = "https://geocoding-api.open-meteo.com/v1/search?count=10&format=json&name="
                + encodeURIComponent(query)
        request.open("GET", url)
        request.onreadystatechange = function() {
            if (request.readyState !== XMLHttpRequest.DONE) {
                return
            }
            if (generation !== searchGeneration || request !== activeRequest) {
                return
            }

            activeRequest = null
            locModel.clear()
            if (request.status !== 200) {
                return
            }

            try {
                var response = JSON.parse(request.responseText)
                var results = response.results || []
                for (var i = 0; i < results.length; i++) {
                    var location = results[i]
                    if (typeof location.name !== "string"
                            || typeof location.latitude !== "number"
                            || typeof location.longitude !== "number") {
                        continue
                    }

                    var name = location.name
                    if (location.admin1 && location.admin1 !== location.name) {
                        name += ", " + location.admin1
                    }
                    if (location.country) {
                        name += ", " + location.country
                    }
                    locModel.append({
                        "name": name,
                        "lat": String(location.latitude),
                        "lng": String(location.longitude)
                    })
                }
            } catch (error) {
                locModel.clear()
            }
        }
        request.send()
    }
}
