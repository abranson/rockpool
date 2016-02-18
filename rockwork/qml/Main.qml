import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import RockWork 1.0

/*!
    \brief MainView with a Label and Button elements.
*/

MainView {
    applicationName: "rockwork.mzanetti"

    width: units.gu(40)
    height: units.gu(70)

    ServiceController {
        id: serviceController
        serviceName: "rockworkd"
        Component.onCompleted: {
            if (!serviceController.serviceFileInstalled) {
                print("Service file not installed. Installing now.")
                serviceController.installServiceFile();
            }
            if (!serviceController.serviceRunning) {
                print("Service not running. Starting now.")
                serviceController.startService();
            }
            if (pebbles.version !== version) {
                print("Service file version (", version, ") is not equal running service version (", pebbles.version, "). Restarting service.")
                serviceController.restartService();
            }
        }
    }

    Pebbles {
        id: pebbles
        onCountChanged: loadStack()
        onConnectedToServiceChanged: loadStack();
    }

    function loadStack() {
        pageStack.clear()
        if (!pebbles.connectedToService) {
            pageStack.push(loadingComponent)
        } else if (pebbles.count == 1) {
            pageStack.push(Qt.resolvedUrl("MainMenuPage.qml"), {pebble: pebbles.get(0)})
        } else {
            pageStack.push(Qt.resolvedUrl("PebblesPage.qml"))
        }
    }

    PageStack {
        id: pageStack
        Component.onCompleted: loadStack();
    }

    Component {
        id: loadingComponent
        Page {
            title: "RockWork"

            Column {
                width: parent.width - units.gu(4)
                anchors.centerIn: parent
                spacing: units.gu(4)

                Rectangle {
                    id: upgradeIcon
                    height: units.gu(10)
                    width: height
                    radius: width / 2
                    color: UbuntuColors.blue
                    anchors.horizontalCenter: parent.horizontalCenter
                    Icon {
                        anchors.fill: parent
                        anchors.margins: units.gu(1)
                        name: "preferences-system-updates-symbolic"
                        color: "white"
                    }

                    RotationAnimation on rotation {
                        duration: 2000
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        running: upgradeIcon.visible
                    }
                    visible: true
                }

                Label {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    fontSize: "large"
                    text: i18n.tr("Loading...")
                }
            }

        }
    }
}
