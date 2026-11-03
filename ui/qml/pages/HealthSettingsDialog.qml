import QtQuick 2.6
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
        var fields = cloneParams(dirtyFields);
        fields[field] = true;
        dirtyFields = fields;
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
        if (!dirtyFields["hrmEnabled"]) {
            heartRateSwitch.checked = params["hrmEnabled"] === true;
        }
        if (!dirtyFields["hrmMeasurementInterval"]) {
            heartRateInterval.currentIndex = intervalIndex(params["hrmMeasurementInterval"]);
        }
        if (!dirtyFields["bloodOxygenEnabled"]) {
            bloodOxygenSwitch.checked = params["bloodOxygenEnabled"] === true;
        }
        if (!dirtyFields["spo2MeasurementInterval"]) {
            bloodOxygenInterval.currentIndex = intervalIndex(params["spo2MeasurementInterval"]);
        }
        snapshotLoaded = true;
        loadingSnapshot = false;
    }

    function intervalIndex(value) {
        return typeof value === "number" && value >= 0 && value <= 3
                && Math.floor(value) === value ? value : -1;
    }

    function monitoringChanges(updated, enabledKey, intervalKey, checked, index) {
        if (dirtyFields[enabledKey]) {
            updated[enabledKey] = checked;
        }
        if (dirtyFields[intervalKey] && index >= 0) {
            updated[intervalKey] = index;
        } else if (dirtyFields[enabledKey] && checked
                   && intervalIndex(healthParams[intervalKey]) < 0) {
            updated[intervalKey] = 0; // Default only when no interval has been received.
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: settingsColumn.height

        Column {
            id: settingsColumn

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

            TextSwitch {
                id: heartRateSwitch

                text: qsTr("Heart rate")
                description: root.healthParams["hrmEnabled"] === undefined
                             && !root.dirtyFields["hrmEnabled"]
                             ? qsTr("Setting not received from the watch.")
                             : qsTr("Measure heart rate automatically.")
                enabled: root.settingsReady
                width: parent.width
                onClicked: root.markDirty("hrmEnabled")
            }

            ComboBox {
                id: heartRateInterval

                width: parent.width
                label: qsTr("Measurement interval")
                enabled: root.settingsReady && heartRateSwitch.checked
                value: currentIndex < 0 ? qsTr("Not received from watch") : currentItem ? currentItem.text : ""
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("10 minutes")
                        onClicked: root.markDirty("hrmMeasurementInterval")
                    }
                    MenuItem {
                        text: qsTr("30 minutes")
                        onClicked: root.markDirty("hrmMeasurementInterval")
                    }
                    MenuItem {
                        text: qsTr("1 hour")
                        onClicked: root.markDirty("hrmMeasurementInterval")
                    }
                    MenuItem {
                        text: qsTr("Disabled")
                        onClicked: root.markDirty("hrmMeasurementInterval")
                    }
                }
            }

            TextSwitch {
                id: bloodOxygenSwitch

                text: qsTr("Blood oxygen (SpO₂)")
                description: root.healthParams["bloodOxygenEnabled"] === undefined
                             && !root.dirtyFields["bloodOxygenEnabled"]
                             ? qsTr("Setting not received from the watch.")
                             : qsTr("Measure blood oxygen automatically.")
                enabled: root.settingsReady
                width: parent.width
                onClicked: root.markDirty("bloodOxygenEnabled")
            }

            ComboBox {
                id: bloodOxygenInterval

                width: parent.width
                label: qsTr("Measurement interval")
                enabled: root.settingsReady && bloodOxygenSwitch.checked
                value: currentIndex < 0 ? qsTr("Not received from watch") : currentItem ? currentItem.text : ""
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("10 minutes")
                        onClicked: root.markDirty("spo2MeasurementInterval")
                    }
                    MenuItem {
                        text: qsTr("30 minutes")
                        onClicked: root.markDirty("spo2MeasurementInterval")
                    }
                    MenuItem {
                        text: qsTr("1 hour")
                        onClicked: root.markDirty("spo2MeasurementInterval")
                    }
                    MenuItem {
                        text: qsTr("Disabled")
                        onClicked: root.markDirty("spo2MeasurementInterval")
                    }
                }
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

        VerticalScrollDecorator {}
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
            for (var key in updated) {
                if (!dirtyFields[key]) delete updated[key];
            }
            if (dirtyFields["enabled"]) updated["enabled"] = enabledSwitch.checked;
            if (dirtyFields["gender"]) updated["gender"] = genderSelector.currentIndex === 0 ? "female" : "male";
            if (dirtyFields["age"]) updated["age"] = ageField.text;
            if (dirtyFields["height"]) updated["height"] = heightField.text;
            if (dirtyFields["weight"]) updated["weight"] = weightField.text;
            if (dirtyFields["moreActive"]) updated["moreActive"] = moreActiveSwitch.checked;
            if (dirtyFields["sleepMore"]) updated["sleepMore"] = sleepMoreSwitch.checked;
            monitoringChanges(updated, "hrmEnabled", "hrmMeasurementInterval",
                              heartRateSwitch.checked, heartRateInterval.currentIndex);
            monitoringChanges(updated, "bloodOxygenEnabled", "spo2MeasurementInterval",
                              bloodOxygenSwitch.checked, bloodOxygenInterval.currentIndex);
            if (Object.keys(updated).length > 0) {
                pebble.healthParams = updated;
            }
        }
    }
}
