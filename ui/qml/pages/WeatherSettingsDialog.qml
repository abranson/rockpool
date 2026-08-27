import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var pebble: null
    property var locStash: []
    property string units
    property string initialUnits
    property bool settingsLoaded
    property bool dirty
    property bool settingsEditable: settingsLoaded && pebble && pebble.weatherSettingsReady

    canAccept: settingsEditable && dirty

    SilicaFlickable {
        id: view
        anchors.fill: parent
        contentHeight: content.height
        Column {
            id:content
            width: parent.width

            DialogHeader {
                title: qsTr("Weather Settings")
                defaultAcceptText: qsTr("OK")
                defaultCancelText: qsTr("Cancel")
            }
            SectionHeader {
                text: qsTr("Locations")
            }

            ListModel {
                id: locations
            }

            SilicaListView {
                id: locList
                width: parent.width
                height: contentItem.childrenRect.height
                model: locations
                enabled: root.settingsEditable
                delegate: ListItem {
                    id: liLoc
                    width: locList.width - Theme.paddingSmall
                    contentHeight: contentItem.height
                    anchors.horizontalCenter: parent.horizontalCenter
                    highlighted: down || menuOpen || !enabled
                    Column {
                        id: contentItem
                        width: parent.width
                        TextField {
                            id: leName
                            width: parent.width
                            label: qsTr("Location Name")
                            text: model.name
                            visible: !liLoc.enabled
                            onTextChanged: {
                                if (root.settingsLoaded && activeFocus) {
                                    root.dirty = true
                                }
                            }
                        }
                        TextField {
                            id: leLat
                            width: parent.width
                            label: qsTr("Latitude")
                            text: model.lat
                            visible: !liLoc.enabled
                            onTextChanged: {
                                if (root.settingsLoaded && activeFocus) {
                                    root.dirty = true
                                }
                            }
                        }
                        TextField {
                            id: leLng
                            width: parent.width
                            label: qsTr("Longitude")
                            text: model.lng
                            visible: !liLoc.enabled
                            onTextChanged: {
                                if (root.settingsLoaded && activeFocus) {
                                    root.dirty = true
                                }
                            }
                        }
                        Row {
                            width: parent.width
                            spacing: Theme.paddingSmall
                            Button {
                                text: qsTr("Cancel")
                                onClicked: liLoc.enabled = true
                                visible: !liLoc.enabled
                                width: parent.width/2 - Theme.paddingSmall
                            }
                            Button {
                                text: qsTr("Save Changes")
                                width: parent.width/2 - Theme.paddingSmall
                                visible: !liLoc.enabled
                                onClicked: {
                                    locations.setProperty(index,"name",leName.text);
                                    locations.setProperty(index,"lat",leLat.text);
                                    locations.setProperty(index,"lng",leLng.text);
                                    liLoc.enabled = true;
                                    root.dirty = true;
                                }
                            }
                        }
                        Row {
                            width: parent.width
                            height: Theme.itemSizeSmall
                            visible: liLoc.enabled
                            Label {
                                width: parent.width*0.75
                                anchors.verticalCenter: parent.verticalCenter
                                text: model.name
                            }
                            Column {
                                width: parent.width/4
                                anchors.verticalCenter: parent.verticalCenter
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
                        }
                    }
                    menu: ContextMenu {
                        MenuItem {
                            text: qsTr("Edit")
                            visible: index>0
                            onClicked: liLoc.enabled = false
                        }
                        MenuItem {
                            text: qsTr("Move Up")
                            visible: index>1
                            onClicked: {locations.move(index,index-1,1);root.dirty=true}
                        }
                        MenuItem {
                            text: qsTr("Move Down")
                            visible: index>0 && index<(locations.count-1)
                            onClicked: {locations.move(index,index+1,1);root.dirty=true}
                        }
                        MenuItem {
                            text: qsTr("Delete")
                            visible: index>0 || locations.count==1
                            onClicked: liLoc.remorseAction("Delete?",function(){locations.remove(index);root.dirty=true})
                        }
                    }
                }
            }
            Button {
                width: parent.width
                text: qsTr("Add Location")
                enabled: root.settingsEditable
                onClicked: {
                    if(locations.count>0) {
                        var locpick = pageStack.push(Qt.resolvedUrl("LocationPicker.qml"));
                        locpick.accepted.connect(function(){
                            locations.append(locpick.selected);
                            root.dirty=true;
                        });
                    } else {
                        locations.append({"name":qsTr("Current Location"),"lat":"n/a","lng":"n/a"})
                        root.dirty=true;
                    }
                }
            }

            ComboBox {
                id: boxUnits
                label: qsTr("Units")
                enabled: root.settingsEditable
                menu: ContextMenu {
                    Repeater {
                        model: modUnits
                        delegate: MenuItem {
                            text: model.lbl
                            onClicked: {
                                if(model.val !== root.units) {
                                    root.units = model.val;
                                    root.dirty = true;
                                }
                            }
                        }
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
    }

    ListModel {
        id: modUnits
    }

    Connections {
        target: root.pebble
        onWeatherSettingsReadyChanged: {
            if (root.pebble.weatherSettingsReady && !root.dirty) {
                root.loadFromPebble()
            }
        }
        onWeatherUnitsChanged: root.loadFromPebble()
        onWeatherLocationsChanged: root.loadFromPebble()
    }

    function loadFromPebble() {
        if (!pebble || !pebble.weatherSettingsReady || dirty) {
            return
        }

        var list = pebble.weatherLocations
        locations.clear();
        locStash = [];
        for(var i in list) {
            var loc = list[i];
            locStash.push(loc);
            locations.append({"name":loc[0],"lat":loc[1],"lng":loc[2]});
            console.log("Location",i,loc);
        }
        root.units = pebble.weatherUnits
        initialUnits = root.units
        boxUnits.currentIndex = 0
        for(i = 0; i<modUnits.count; i++) {
            if(root.units === modUnits.get(i).val)
                boxUnits.currentIndex = i;
        }
        settingsLoaded = true
        dirty = false
    }

    Component.onCompleted: {
        var mUnits = [
            { "val": "m", "lbl": qsTr("Metric") },
            { "val": "e", "lbl": qsTr("Imperial") },
            { "val": "h", "lbl": qsTr("Hybrid") }
        ]
        for(var i = 0; i< mUnits.length; i++) {
            modUnits.append(mUnits[i])
        }
        if (pebble) {
            pebble.refreshWeatherSettings()
        }
    }

    onDone: {
        if(result === DialogResult.Accepted) {
            if(root.units !== initialUnits)
                pebble.weatherUnits = root.units;
            var ret = [];
            var locStore = locations.count !== locStash.length;
            for(var i=0;i<locations.count;i++) {
                var loc = locations.get(i);
                ret[i] = [loc.name,loc.lat,loc.lng];
                if(!locStore && (locStash.length<=i || ret[i][0] !== locStash[i][0] || ret[i][1] !== locStash[i][1] || ret[i][2] !== locStash[i][2])) {
                    locStore = true;
                    console.log("Diff spotted",i,loc.name,loc.lat,loc.lng);
                }
            }
            if(locStore)
                pebble.weatherLocations = ret;
        }
    }
}
