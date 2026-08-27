import QtQuick 2.2
import Sailfish.Silica 1.0
import RockPool 1.0
import Nemo.DBus 2.0
import "pages"

/*!
    \brief MainView with a Label and Button elements.
*/

ApplicationWindow {
    id: rockPool
    initialPage: Qt.resolvedUrl("pages/LoadingPage.qml")
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    property int curPebble: -1
    property int knownPebbleCount: 0
    property bool waitingForPebbleIdentity: false
    property string pendingPairAddress: ""
    property var sysProfiles: [ ]

    ServiceController {
        id: serviceController

        property bool initialStateHandled

        onReadyChanged: {
            if (ready) {
                rockPool.initService()
            } else {
                initialStateHandled = false
            }
        }
        Component.onCompleted: rockPool.initService()
    }

    Timer {
        id: stackReloadTimer

        interval: 0
        repeat: false
        onTriggered: rockPool.loadStack()
    }

    Pebbles {
        id: pebbles

        // A newly inserted Pebble receives its identity asynchronously. Do
        // not replace PairWatchPage until that identity is available, so it
        // can retire only the matching pending connection.
        onCountChanged: {
            if (pebbles.count < rockPool.knownPebbleCount) {
                stackReloadTimer.restart()
            } else if (pebbles.count > rockPool.knownPebbleCount) {
                rockPool.waitingForPebbleIdentity = true
            }
            rockPool.knownPebbleCount = pebbles.count
        }
        // The zero-delay timer lets the page-local signal handler clear its
        // pending address before loadStack() destroys the active page.
        onPebbleIdentityAvailable: {
            if (rockPool.waitingForPebbleIdentity
                    && (rockPool.pendingPairAddress === ""
                        || address.toLowerCase()
                           === rockPool.pendingPairAddress.toLowerCase())) {
                rockPool.waitingForPebbleIdentity = false
                stackReloadTimer.restart()
            }
        }
        onConnectedToServiceChanged: {
            if (!pebbles.connectedToService || pebbles.count === 0) {
                stackReloadTimer.restart()
            }
        }
    }
    DBusInterface {
        id: profiled
        service: "com.nokia.profiled"
        path: "/com/nokia/profiled"
        iface: "com.nokia.profiled"
    }
    function getProfiles() {
        if(sysProfiles.length===0) {
            profiled.typedCall("get_profiles",[],
               function(r){sysProfiles=[].concat(sysProfiles,r);console.log("Now",sysProfiles,sysProfiles.length)},
               function(e){console.log("com.nokia.profiled error",e)})
        }
    }
    function initService() {
        if (!serviceController.ready || serviceController.initialStateHandled) {
            return
        }
        serviceController.initialStateHandled = true
        if (!pebbles.connectedToService && !serviceController.serviceRunning) {
            console.log("Service not running. Starting now.");
            serviceController.startService();
        }
        // No version-mismatch restart: libpebble3d versions independently of the UI
        // (the old check bounced the daemon on every app launch), and the daemon RPM
        // already try-restarts on upgrade.
    }
    function stopService() {
        console.log("Request to stop and disable service");
        serviceController.stopService()
    }
    function restartService() {
        console.log("Request to restart service");
        serviceController.restartService()
    }
    function connectWatch(address) {
        pebbles.connectWatch(address)
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
    Component.onCompleted: loadStack()
    function getCurPebble() {
        if(curPebble>=0) return pebbles.get(curPebble);
        return null;
    }
}
