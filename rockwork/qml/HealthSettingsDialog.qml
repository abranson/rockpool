import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import Ubuntu.Components.Popups 1.3
import Ubuntu.Components.ListItems 1.3

Dialog {
    id: root
    title: i18n.tr("Health settings")

    property var healthParams: null

    signal accepted();

    RowLayout {
        Label {
            text: i18n.tr("Health app enabled")
            Layout.fillWidth: true
        }
        Switch {
            id: enabledSwitch
            checked: healthParams["enabled"]
        }
    }

    ItemSelector {
        id: genderSelector
        model: [i18n.tr("Female"), i18n.tr("Male")]
        selectedIndex: root.healthParams["gender"] === "female" ? 0 : 1
    }

    RowLayout {
        Label {
            text: i18n.tr("Age")
            Layout.fillWidth: true
        }
        TextField {
            id: ageField
            inputMethodHints: Qt.ImhDigitsOnly
            text: healthParams["age"]
            Layout.preferredWidth: units.gu(10)
        }
    }

    RowLayout {
        Label {
            text: i18n.tr("Height (cm)")
            Layout.fillWidth: true
        }
        TextField {
            id: heightField
            inputMethodHints: Qt.ImhDigitsOnly
            text: healthParams["height"]
            Layout.preferredWidth: units.gu(10)
        }
    }

    RowLayout {
        Label {
            text: i18n.tr("Weight")
            Layout.fillWidth: true
        }
        TextField {
            id: weightField
            inputMethodHints: Qt.ImhDigitsOnly
            text: healthParams["weight"]
            Layout.preferredWidth: units.gu(10)
        }
    }

    RowLayout {
        Label {
            text: i18n.tr("I want to be more active")
            Layout.fillWidth: true
        }
        Switch {
            id: moreActiveSwitch
            checked: healthParams["moreActive"]
        }
    }

    RowLayout {
        Label {
            text: i18n.tr("I want to sleep more")
            Layout.fillWidth: true
        }
        Switch {
            id: sleepMoreSwitch
            checked: healthParams["sleepMore"]
        }
    }


    Button {
        text: i18n.tr("OK")
        color: UbuntuColors.green
        onClicked: {
            root.healthParams["enabled"] = enabledSwitch.checked;
            root.healthParams["gender"] = genderSelector.selectedIndex == 0 ? "female" : "male"
            root.healthParams["age"] = ageField.text;
            root.healthParams["height"] = heightField.text;
            root.healthParams["weight"] = weightField.text;
            root.healthParams["moreActive"] = moreActiveSwitch.checked;
            root.healthParams["sleepMore"] = sleepMoreSwitch.checked;
            root.accepted();
            PopupUtils.close(root);
        }
    }
    Button {
        text: i18n.tr("Cancel")
        color: UbuntuColors.red
        onClicked: PopupUtils.close(root)
    }
}

