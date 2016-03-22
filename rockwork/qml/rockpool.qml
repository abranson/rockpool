import QtQuick 2.2
import Sailfish.Silica 1.0
import RockPool 1.0
import "pages"

/*!
    \brief MainView with a Label and Button elements.
*/

ApplicationWindow {
    id: rockPool
    initialPage: Qt.resolvedUrl("pages/LoadingPage.qml")
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    property int curPebble: -1

    ServiceController {
        id: serviceController
        Component.onCompleted: initService()
    }

    Pebbles {
        id: pebbles
        onCountChanged: loadStack()
        onConnectedToServiceChanged: loadStack();
    }
    function initService() {
        if (!serviceController.serviceRunning) {
            console.log("Service not running. Starting now.");
            serviceController.startService();
        }
        if (pebbles.version !== version) {
            console.log("Service file version (", version, ") is not equal running service version (", pebbles.version, "). Restarting service.");
            serviceController.restartService();
        }
    }
    function stopService() {
        console.log("Request to stop and disable service");
        serviceController.stopService()
    }

    function loadStack() {
        if (pebbles.connectedToService) {
            pageStack.clear()
            if (pebbles.count === 1) {
                curPebble = 0;
                pageStack.push(Qt.resolvedUrl("pages/MainMenuPage.qml"), {pebble: pebbles.get(curPebble)})
            } else {
                pageStack.push(Qt.resolvedUrl("pages/PebblesPage.qml"))
            }
        } else {
            console.log("Waiting for service")
            if(curPebble>=0) {
                pageStack.clear();
                pageStack.push(initialPage);
                curPebble = -1;
            }
        }
    }
    Component.onCompleted: loadStack();
    function getCurPebble() {
        if(curPebble>=0) return pebbles.get(curPebble);
        return null;
    }
}
