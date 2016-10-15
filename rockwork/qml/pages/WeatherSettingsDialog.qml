import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var pebble: null
    property var locStash: []
    property string apiKey: ""
    property string lang
    property string units
    canAccept: false

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
            TextSwitch {
                id: altProvider
                text: qsTr("Alternate Provider")
                width: parent.width
                checked: apiKey.length>0
            }
            TextField {
                id: altKeyField
                text: apiKey
                placeholderText: qsTr("Provider's Key, eg. API Key")
                label: qsTr("Provider Key")
                enabled: altProvider.checked
                visible: enabled
                width: parent.width
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
                        }
                        TextField {
                            id: leLat
                            width: parent.width
                            label: qsTr("Latitude")
                            text: model.lat
                            visible: !liLoc.enabled
                        }
                        TextField {
                            id: leLng
                            width: parent.width
                            label: qsTr("Longitude")
                            text: model.lng
                            visible: !liLoc.enabled
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
                            onClicked: {locations.move(index,index-1,1);root.canAccept=true}
                        }
                        MenuItem {
                            text: qsTr("Move Down")
                            visible: index>0 && index<(locations.count-1)
                            onClicked: {locations.move(index,index+1,1);root.canAccept=true}
                        }
                        MenuItem {
                            text: qsTr("Delete")
                            visible: index>0 || locations.count==1
                            onClicked: liLoc.remorseAction("Delete?",function(){locations.remove(index);root.canAccept=true})
                        }
                    }
                }
            }
            Button {
                width: parent.width
                text: qsTr("Add Location")
                onClicked: {
                    if(locations.count>0) {
                        var locpick = pageStack.push(Qt.resolvedUrl("LocationPicker.qml"));
                        locpick.accepted.connect(function(){
                            locations.append(locpick.selected);
                            root.canAccept=true;
                        });
                    } else {
                        locations.append({"name":qsTr("Current Location"),"lat":"n/a","lng":"n/a"})
                    }
                }
            }

            SectionHeader {
                text: qsTr("Locales")
            }

            ComboBox {
                id: boxUnits
                label: qsTr("Units")
                menu: ContextMenu {
                    Repeater {
                        model: modUnits
                        delegate: MenuItem {
                            text: model.lbl
                            onClicked: {
                                if(model.val !== root.units) {
                                    root.units = model.val;
                                    root.canAccept = true;
                                }
                            }
                        }
                    }
                }
            }

            ComboBox {
                id: boxLang
                label: qsTr("Language")
                menu: ContextMenu {
                    Repeater {
                        model: modLang
                        delegate: MenuItem {
                            text: model.lbl
                            onClicked: {
                                if(model.val !== root.lang) {
                                    root.lang = model.val;
                                    root.canAccept = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ListModel {
        id: modUnits
    }

    ListModel {
      id: modLang
      ListElement { val: "AF"; lbl: "Afrikaans" }
      ListElement { val: "AL"; lbl: "Albanian" }
      ListElement { val: "AR"; lbl: "Arabic" }
      ListElement { val: "HY"; lbl: "Armenian" }
      ListElement { val: "AZ"; lbl: "Azerbaijani" }
      ListElement { val: "EU"; lbl: "Basque" }
      ListElement { val: "BY"; lbl: "Belarusian" }
      ListElement { val: "BU"; lbl: "Bulgarian" }
      ListElement { val: "LI"; lbl: "BritishEnglish" }
      ListElement { val: "MY"; lbl: "Burmese" }
      ListElement { val: "CA"; lbl: "Catalan" }
      ListElement { val: "CN"; lbl: "Chinese-Simplified" }
      ListElement { val: "TW"; lbl: "Chinese-Traditional" }
      ListElement { val: "CR"; lbl: "Croatian" }
      ListElement { val: "CZ"; lbl: "Czech" }
      ListElement { val: "DK"; lbl: "Danish" }
      ListElement { val: "DV"; lbl: "Dhivehi" }
      ListElement { val: "NL"; lbl: "Dutch" }
      ListElement { val: "EN"; lbl: "English" }
      ListElement { val: "EO"; lbl: "Esperanto" }
      ListElement { val: "ET"; lbl: "Estonian" }
      ListElement { val: "FA"; lbl: "Farsi" }
      ListElement { val: "FI"; lbl: "Finnish" }
      ListElement { val: "FR"; lbl: "French" }
      ListElement { val: "FC"; lbl: "FrenchCanadian" }
      ListElement { val: "GZ"; lbl: "Galician" }
      ListElement { val: "DL"; lbl: "German" }
      ListElement { val: "KA"; lbl: "Georgian" }
      ListElement { val: "GR"; lbl: "Greek" }
      ListElement { val: "GU"; lbl: "Gujarati" }
      ListElement { val: "HT"; lbl: "HaitianCreole" }
      ListElement { val: "IL"; lbl: "Hebrew" }
      ListElement { val: "HI"; lbl: "Hindi" }
      ListElement { val: "HU"; lbl: "Hungarian" }
      ListElement { val: "IS"; lbl: "Icelandic" }
      ListElement { val: "IO"; lbl: "Ido" }
      ListElement { val: "ID"; lbl: "Indonesian" }
      ListElement { val: "IR"; lbl: "IrishGaelic" }
      ListElement { val: "IT"; lbl: "Italian" }
      ListElement { val: "JP"; lbl: "Japanese" }
      ListElement { val: "JW"; lbl: "Javanese" }
      ListElement { val: "KM"; lbl: "Khmer" }
      ListElement { val: "KR"; lbl: "Korean" }
      ListElement { val: "KU"; lbl: "Kurdish" }
      ListElement { val: "LA"; lbl: "Latin" }
      ListElement { val: "LV"; lbl: "Latvian" }
      ListElement { val: "LT"; lbl: "Lithuanian" }
      ListElement { val: "ND"; lbl: "LowGerman" }
      ListElement { val: "MK"; lbl: "Macedonian" }
      ListElement { val: "MT"; lbl: "Maltese" }
      ListElement { val: "GM"; lbl: "Mandinka" }
      ListElement { val: "MI"; lbl: "Maori" }
      ListElement { val: "MR"; lbl: "Marathi" }
      ListElement { val: "MN"; lbl: "Mongolian" }
      ListElement { val: "NO"; lbl: "Norwegian" }
      ListElement { val: "OC"; lbl: "Occitan" }
      ListElement { val: "PS"; lbl: "Pashto" }
      ListElement { val: "GN"; lbl: "Plautdietsch" }
      ListElement { val: "PL"; lbl: "Polish" }
      ListElement { val: "BR"; lbl: "Portuguese" }
      ListElement { val: "PA"; lbl: "Punjabi" }
      ListElement { val: "RO"; lbl: "Romanian" }
      ListElement { val: "RU"; lbl: "Russian" }
      ListElement { val: "SR"; lbl: "Serbian" }
      ListElement { val: "SK"; lbl: "Slovak" }
      ListElement { val: "SL"; lbl: "Slovenian" }
      ListElement { val: "SP"; lbl: "Spanish" }
      ListElement { val: "SI"; lbl: "Swahili" }
      ListElement { val: "SW"; lbl: "Swedish" }
      ListElement { val: "CH"; lbl: "Swiss" }
      ListElement { val: "TL"; lbl: "Tagalog" }
      ListElement { val: "TT"; lbl: "Tatarish" }
      ListElement { val: "TH"; lbl: "Thai" }
      ListElement { val: "TR"; lbl: "Turkish" }
      ListElement { val: "TK"; lbl: "Turkmen" }
      ListElement { val: "UA"; lbl: "Ukrainian" }
      ListElement { val: "UZ"; lbl: "Uzbek" }
      ListElement { val: "VU"; lbl: "Vietnamese" }
      ListElement { val: "CY"; lbl: "Welsh" }
      ListElement { val: "SN"; lbl: "Wolof" }
      ListElement { val: "JI"; lbl: "Yiddish-transliterated" }
      ListElement { val: "YI"; lbl: "Yiddish-unicode" }
    }
    Component.onCompleted: {
        var list = pebble.weatherLocations;
        locations.clear();
        locStash = [];
        for(var i in list) {
            var loc = list[i];
            locStash.push(loc);
            locations.append({"name":loc[0],"lat":loc[1],"lng":loc[2]});
            console.log("Location",i,loc);
        }
        root.lang = pebble.weatherLanguage;
        for(i = 0; i<modLang.count;i++) {
            if(root.lang === modLang.get(i).val) {
                boxLang.currentIndex = i;
                break;
            }
        }
        modLang.insert(0,{ "val": "", "lbl": qsTr("Default (English)")});
        root.units = pebble.weatherUnits;
        var mUnits = [
            { "val": "m", "lbl": qsTr("Metric") },
            { "val": "e", "lbl": qsTr("Imperial") },
            { "val": "h", "lbl": qsTr("Hybrid") }
        ];

        for(i = 0; i< mUnits.length; i++) {
            modUnits.append(mUnits[i]);
            if(root.units === mUnits[i].val)
                boxUnits.currentIndex = i;
            console.log("Uni",mUnits[i],modUnits.get(i));
        }
        apiKey = pebble.weatherApiKey
    }

    onDone: {
        if(result === DialogResult.Accepted) {
            var ret = [];
            var locStore = false;
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
            if(altProvider.checked && altKeyField.text.length>0 && apiKey !== altKeyField.text) {
                pebble.weatherApiKey = altKeyField.text;
                console.log("Setting AltKey to",altKeyField.text);
            } else if(apiKey.length>0 && !altProvider.checked) {
                pebble.weatherApiKey = "";
                console.log("Clearing AltKey");
            }
        }
    }
}
