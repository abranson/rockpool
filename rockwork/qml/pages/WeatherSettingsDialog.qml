import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var pebble: null
    property var locStash: []
    property string lang
    property string units
    property string initialLang
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

            SectionHeader {
                text: qsTr("Locales")
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

            ComboBox {
                id: boxLang
                label: qsTr("Language")
                enabled: root.settingsEditable
                menu: ContextMenu {
                    Repeater {
                        model: modLang
                        delegate: MenuItem {
                            text: model.lbl
                            onClicked: {
                                if(model.val !== root.lang) {
                                    root.lang = model.val;
                                    root.dirty = true;
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
    Connections {
        target: root.pebble
        onWeatherSettingsReadyChanged: {
            if (root.pebble.weatherSettingsReady && !root.dirty) {
                root.loadFromPebble()
            }
        }
        onWeatherUnitsChanged: root.loadFromPebble()
        onWeatherLanguageChanged: root.loadFromPebble()
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
        root.lang = pebble.weatherLanguage
        initialLang = root.lang
        boxLang.currentIndex = 0
        for(i = 0; i<modLang.count;i++) {
            if(root.lang === modLang.get(i).val) {
                boxLang.currentIndex = i;
                break;
            }
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
        modLang.insert(0,{ "val": "", "lbl": qsTr("Default (English)")})
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
            if(root.lang !== initialLang)
                pebble.weatherLanguage = root.lang;
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
