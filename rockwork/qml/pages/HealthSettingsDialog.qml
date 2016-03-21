import QtQuick 2.2
import Sailfish.Silica 1.0

Dialog {
    id: root

    property var healthParams: null

    Column {
        width: parent.width

        DialogHeader {
            title: qsTr("Health settings")
            defaultAcceptText: qsTr("OK")
            //acceptText: qsTr("OK")
            defaultCancelText: qsTr("Cancel")
            //cancelText: qsTr("Cancel")
        }
        TextSwitch {
            text: qsTr("Health app enabled")
            id: enabledSwitch
            checked: healthParams["enabled"]
            width: parent.width
        }
        TextField {
            label: qsTr("Age")
            id: ageField
            inputMethodHints: Qt.ImhDigitsOnly
            text: healthParams["age"]
            width: parent.width
            EnterKey.enabled: text.length > 0 && text != "0"
            EnterKey.onClicked: heightField.focus = true
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
        }
        TextField {
            id: heightField
            label: qsTr("Height (cm)")
            inputMethodHints: Qt.ImhDigitsOnly
            text: healthParams["height"]
            width: parent.width
            EnterKey.enabled: text.length > 0 && text != "0"
            EnterKey.onClicked: weightField.focus = true
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
        }
        TextField {
            id: weightField
            label: qsTr("Weight (kg)")
            inputMethodHints: Qt.ImhDigitsOnly
            text: healthParams["weight"]
            width: parent.width
            EnterKey.enabled: text.length > 0 && text != "0"
            EnterKey.onClicked: genderSelector.focus = true
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
        }
        Slider {
            id: genderSelector
            width: parent.width
            label: qsTr("Gender")
            valueText: [qsTr("Female"), qsTr("Male")][value]
            value: (root.healthParams["gender"] === "female") ? 0 : 1
            minimumValue: 0
            maximumValue: 1
            stepSize: 1
        }
        TextSwitch {
            id: moreActiveSwitch
            description: qsTr("I want to be more active")
            text: qsTr("More Active")
            checked: healthParams["moreActive"]
            width: parent.width
        }
        TextSwitch {
            id: sleepMoreSwitch
            description: qsTr("I want to sleep more")
            text: qsTr("Sleep More")
            checked: healthParams["sleepMore"]
            width: parent.width
        }
    }

    onDone: {
        if(result === DialogResult.Accepted) {
            root.healthParams["enabled"] = enabledSwitch.checked;
            root.healthParams["gender"] = genderSelector.value == 0 ? "female" : "male"
            root.healthParams["age"] = ageField.text;
            root.healthParams["height"] = heightField.text;
            root.healthParams["weight"] = weightField.text;
            root.healthParams["moreActive"] = moreActiveSwitch.checked;
            root.healthParams["sleepMore"] = sleepMoreSwitch.checked;
        }
    }
}

