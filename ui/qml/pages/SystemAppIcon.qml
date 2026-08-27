import QtQuick 2.2
import Sailfish.Silica 1.0

Item {
    property bool isSystemApp: false
    property string uuid: ""
    property string iconSource: ""

    Image {
        anchors.verticalCenter: parent.verticalCenter
        visible: parent.isSystemApp
        source: {
            var icon = "";
            switch (parent.uuid) {
            case "{07e0d9cb-8957-4bf7-9d42-35bf47caadfe}":
                icon = "developer-mode";
                break;
            case "{18e443ce-38fd-47c8-84d5-6d0c775fbe55}":
                icon = "watch";
                break;
            case "{36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c}":
                icon = "diagnostic";
                break;
            case "{1f03293d-47af-4f28-b960-f2b02a6dd757}":
                icon = "music";
                break;
            case "{b2cae818-10f8-46df-ad2b-98ad2254a3c1}":
                icon = "alarm";
                break;
            case "{67a32d95-ef69-46d4-a0b9-854cc62f97f9}":
                icon = "timer";
                break;
            case "{8f3c8686-31a1-4f5f-91f5-01600c9bdc59}":
                icon = "clock";
                break;
            case "{61b22bc8-1e29-460d-a236-3fe409a439ff}":
                icon = "day";
                break;
            case "{0863fc6a-66c5-4f62-ab8a-82ed00a98b5d}":
                icon = "chat";
                break;
            case "{4dab81a6-d2fc-458a-992c-7a1f3b96a970}":
                icon = "person";
            }
            if (icon)
                return "image://theme/icon-m-" + icon
            return "image://theme/icon-m-other"
        }
    }

    Image {
        source: parent.isSystemApp ? "" : "file://" + parent.iconSource
        anchors.fill: parent
        visible: !parent.isSystemApp
    }
}
