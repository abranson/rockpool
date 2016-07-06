import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root
    property var pebble: null
    property var languages: []
    property string langVer: pebble.languageVersion
    onPebbleChanged: if(pebble) loadLangs()

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height

        Column {
            id: content
            width: parent.width
            PageHeader {
                title: qsTr("Language Settings")
                description: qsTr("Set langugage for application and your Pebble")
            }
            SectionHeader {
                text: qsTr("Pebble Language")+"("+langVer+")"
            }

            ComboBox {
                id: langSel
                width: parent.width
                label: qsTr("Select Language")
                menu: ContextMenu {
                    Repeater {
                        model: languages.map(function(l){return l.localName;})
                        delegate: MenuItem {
                            text: modelData
                        }
                    }
                }
            }
            Button {
                width: parent.width
                text: qsTr("Submit")
                onClicked: pebble.loadLanguagePack(languages[langSel.currentIndex].file)
                enabled: languages.length>0 && langVer != languages[langSel.currentIndex].ISOLocal+":"+languages[langSel.currentIndex].version
            }
        }
    }
    function loadLangs() {
        if(languages.length>0) return;
        var re = /(\d+)\.(\d+)\.?(\d+)?/;
        var ret = re.exec(pebble.softwareVersion);
        var ver = "3.8.0";
        if(ret) {
            ver = ret[1]+"."+ret[2]+"."+(ret[3]? ret[3] : "0");
        }
        var url = "https://lp.getpebble.com/v1/languages?mobileVersion=3.13.0-1055-06644a6&mobilePlatform=android&isoLocal="+locale+"&hardware="+pebble.platformString+"&firmware="+ver;
        var xhr = new XMLHttpRequest();
        xhr.open("GET",url);
        xhr.onreadystatechange = function() {
            if(xhr.readyState == xhr.DONE) {
                if(xhr.status == 200) {
                    var json = JSON.parse(xhr.responseText);
                    if("languages" in json) {
                        languages = json.languages;
                        return;
                    }
                }
                console.log("XHR Error:",xhr.status,xhr.responseText);
            }
        };
        console.log("Fetching langugages from",url);
        xhr.send();
    }
}

