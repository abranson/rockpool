import QtQuick 2.6
import Sailfish.Silica 1.0

ComboBox {
    id: root

    property var pebble: null
    property var languages: []
    property bool loading
    property string loadError
    readonly property string languageVersion: pebble ? pebble.languageVersion : ""
    readonly property string languageCode: languageVersion.split(":")[0]

    signal languageSelected(string file, string name, string version)

    width: parent.width
    label: qsTr("Watch language")
    enabled: pebble && pebble.connected && languages.length > 0
    description: loading ? qsTr("Loading languages…") : loadError
    currentIndex: {
        for (var i = 0; i < languages.length; ++i) {
            if (languages[i].ISOLocal === languageCode) {
                return i
            }
        }
        return -1
    }
    value: {
        for (var i = 0; i < languages.length; ++i) {
            if (languages[i].ISOLocal === languageCode) {
                return languages[i].localName
            }
        }
        if (!languageCode) {
            return qsTr("Unknown")
        }
        var name = Qt.locale(languageCode).nativeLanguageName
        return name && name !== "C" ? name : languageCode
    }

    menu: ContextMenu {
        Repeater {
            model: root.languages
            delegate: MenuItem {
                text: modelData.localName
                onClicked: {
                    if (root.pebble && root.pebble.connected
                            && root.languageVersion !== modelData.ISOLocal + ":" + modelData.version) {
                        root.languageSelected(modelData.file, modelData.localName,
                                              modelData.ISOLocal + ":" + modelData.version)
                    }
                }
            }
        }
    }

    function loadLanguages() {
        if (!pebble || !pebble.connected || !pebble.platformString || loading) {
            return
        }
        var watch = pebble
        var match = /(\d+)\.(\d+)\.?(\d+)?/.exec(watch.softwareVersion)
        var version = match ? match[1] + "." + match[2] + "." + (match[3] || "0") : "3.8.0"
        var url = "https://lp.rebble.io/v1/languages?mobileVersion=3.13.0-1055-06644a6"
                + "&mobilePlatform=android&isoLocal=" + encodeURIComponent(locale)
                + "&hardware=" + encodeURIComponent(watch.platformString)
                + "&firmware=" + encodeURIComponent(version)
        var xhr = new XMLHttpRequest()
        loading = true
        loadError = ""
        xhr.open("GET", url)
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE) {
                return
            }
            root.loading = false
            if (root.pebble !== watch) {
                root.loadLanguages()
                return
            }
            if (xhr.status === 200) {
                try {
                    var available = JSON.parse(xhr.responseText).languages
                    if (Array.isArray(available)) {
                        // Keep the newest compatible pack for each locale.
                        var unique = []
                        for (var i = 0; i < available.length; ++i) {
                            var language = available[i]
                            if (!language.ISOLocal || !language.localName || !language.file) {
                                continue
                            }
                            var existing = -1
                            for (var j = 0; j < unique.length; ++j) {
                                if (unique[j].ISOLocal === language.ISOLocal) {
                                    existing = j
                                    break
                                }
                            }
                            if (existing < 0) {
                                unique.push(language)
                            } else if (Number(language.version) > Number(unique[existing].version)) {
                                unique[existing] = language
                            }
                        }
                        root.languages = unique
                        if (unique.length > 0) {
                            return
                        }
                    }
                } catch (error) {
                    console.log("Unable to parse language catalogue:", error)
                }
            }
            root.loadError = qsTr("Languages could not be loaded. Reopen Settings to try again.")
        }
        xhr.send()
    }

    onPebbleChanged: {
        languages = []
        loadLanguages()
    }

    Connections {
        target: root.pebble
        onConnectedChanged: if (root.pebble.connected) root.loadLanguages()
    }
}
