import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var pebble: null
    property var healthParams: ({})
    property var dirtyFields: ({})
    property bool dirty
    property bool loadingSnapshot
    property bool snapshotLoaded
    property bool settingsReady: pebble && pebble.healthParamsReady && snapshotLoaded

    canAccept: settingsReady

    function cloneParams(source) {
        var result = {};
        for (var key in source) {
            result[key] = source[key];
        }
        return result;
    }

    function markDirty(field) {
        if (loadingSnapshot || !snapshotLoaded) {
            return;
        }
        dirtyFields[field] = true;
        dirty = true;
    }

    function loadHealthParams() {
        if (!pebble || !pebble.healthParamsReady) {
            snapshotLoaded = false;
            return;
        }
        var params = cloneParams(pebble.healthParams);
        loadingSnapshot = true;
        healthParams = params;
        if (!dirtyFields["enabled"]) {
            enabledSwitch.checked = !!params["enabled"];
        }
        if (!dirtyFields["age"]) {
            ageField.text = String(params["age"]);
        }
        if (!dirtyFields["height"]) {
            heightField.text = String(params["height"]);
        }
        if (!dirtyFields["weight"]) {
            weightField.text = String(params["weight"]);
        }
        if (!dirtyFields["gender"]) {
            genderSelector.currentIndex = params["gender"] === "male" ? 1 : 0;
        }
        if (!dirtyFields["moreActive"]) {
            moreActiveSwitch.checked = !!params["moreActive"];
        }
        if (!dirtyFields["sleepMore"]) {
            sleepMoreSwitch.checked = !!params["sleepMore"];
        }
        snapshotLoaded = true;
        loadingSnapshot = false;
    }

    Column {
        width: parent.width

        DialogHeader {
            title: qsTr("Health settings")
            defaultAcceptText: qsTr("OK")
            defaultCancelText: qsTr("Cancel")
        }

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            running: !root.settingsReady
            visible: running
        }

        TextSwitch {
            id: enabledSwitch

            text: qsTr("Health app enabled")
            enabled: root.settingsReady
            width: parent.width
            onClicked: root.markDirty("enabled")
        }

        TextField {
            id: ageField

            label: qsTr("Age")
            inputMethodHints: Qt.ImhDigitsOnly
            enabled: root.settingsReady
            width: parent.width
            onTextChanged: if (activeFocus) root.markDirty("age")
            EnterKey.enabled: text.length > 0 && text != "0"
            EnterKey.onClicked: heightField.focus = true
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
        }

        TextField {
            id: heightField

            label: qsTr("Height (cm)")
            inputMethodHints: Qt.ImhDigitsOnly
            enabled: root.settingsReady
            width: parent.width
            onTextChanged: if (activeFocus) root.markDirty("height")
            EnterKey.enabled: text.length > 0 && text != "0"
            EnterKey.onClicked: weightField.focus = true
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
        }

        TextField {
            id: weightField

            label: qsTr("Weight (kg)")
            inputMethodHints: Qt.ImhDigitsOnly
            enabled: root.settingsReady
            width: parent.width
            onTextChanged: if (activeFocus) root.markDirty("weight")
            EnterKey.enabled: text.length > 0 && text != "0"
            EnterKey.onClicked: genderSelector.focus = true
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
        }

        ComboBox {
            id: genderSelector

            width: parent.width
            label: qsTr("Gender")
            enabled: root.settingsReady
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Female")
                    onClicked: root.markDirty("gender")
                }
                MenuItem {
                    text: qsTr("Male")
                    onClicked: root.markDirty("gender")
                }
            }
        }

        TextSwitch {
            id: moreActiveSwitch

            description: qsTr("I want to be more active")
            text: qsTr("More Active")
            enabled: root.settingsReady
            width: parent.width
            onClicked: root.markDirty("moreActive")
        }

        TextSwitch {
            id: sleepMoreSwitch

            description: qsTr("I want to sleep more")
            text: qsTr("Sleep More")
            enabled: root.settingsReady
            width: parent.width
            onClicked: root.markDirty("sleepMore")
        }
    }

    Connections {
        target: root.pebble
        onHealthParamsChanged: root.loadHealthParams()
        onHealthParamsReadyChanged: root.loadHealthParams()
    }

    Component.onCompleted: {
        if (pebble) {
            pebble.refreshHealthParams();
        }
        loadHealthParams();
    }

    onDone: {
        if (result === DialogResult.Accepted && settingsReady) {
            var updated = cloneParams(healthParams);
            updated["enabled"] = enabledSwitch.checked;
            updated["gender"] = genderSelector.currentIndex === 0 ? "female" : "male";
            updated["age"] = ageField.text;
            updated["height"] = heightField.text;
            updated["weight"] = weightField.text;
            updated["moreActive"] = moreActiveSwitch.checked;
            updated["sleepMore"] = sleepMoreSwitch.checked;
            pebble.healthParams = updated;
        }
    }
}
