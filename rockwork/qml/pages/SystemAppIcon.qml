import QtQuick 2.4
import Ubuntu.Components 1.3

Item {
    id: root

    property bool isSystemApp: false
    property string uuid: ""
    property string iconSource: ""

    UbuntuShape {
        anchors.fill: parent
        visible: root.isSystemApp
        backgroundColor: {
            switch (root.uuid) {
            case "{07e0d9cb-8957-4bf7-9d42-35bf47caadfe}":
                return "gray";
            case "{18e443ce-38fd-47c8-84d5-6d0c775fbe55}":
                return "blue";
            case "{36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c}":
                return UbuntuColors.red;
            case "{1f03293d-47af-4f28-b960-f2b02a6dd757}":
                return "gold"
            case "{b2cae818-10f8-46df-ad2b-98ad2254a3c1}":
                return "darkviolet"
            case "{67a32d95-ef69-46d4-a0b9-854cc62f97f9}":
                return "green";
            case "{8f3c8686-31a1-4f5f-91f5-01600c9bdc59}":
                return "black"
            }

            return "";
        }
    }
    Icon {
        anchors.fill: parent
        implicitHeight: height
        anchors.margins: units.gu(1)
        visible: root.isSystemApp
        color: "white"
        name: {
            switch (root.uuid) {
            case "{07e0d9cb-8957-4bf7-9d42-35bf47caadfe}":
                return "settings";
            case "{18e443ce-38fd-47c8-84d5-6d0c775fbe55}":
                return "clock-app-symbolic";
            case "{36d8c6ed-4c83-4fa1-a9e2-8f12dc941f8c}":
                return "like";
            case "{1f03293d-47af-4f28-b960-f2b02a6dd757}":
                return "stock_music";
            case "{b2cae818-10f8-46df-ad2b-98ad2254a3c1}":
                return "stock_notification";
            case "{67a32d95-ef69-46d4-a0b9-854cc62f97f9}":
                return "stock_alarm-clock";
            case "{8f3c8686-31a1-4f5f-91f5-01600c9bdc59}":
                return "clock-app-symbolic";
            }
            return "";
        }
    }

    Image {
        source: root.isSystemApp ? "" : "file://" + root.iconSource;
        anchors.fill: parent
        visible: !root.isSystemApp
    }
}
