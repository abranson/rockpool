import QtQuick 2.6
import Sailfish.Silica 1.0
import RockPool 1.0
import Nemo.DBus 2.0
import "pages"

/*!
    \brief MainView with a Label and Button elements.
*/

ApplicationWindow {
    id: rockPool

    property alias savingAppstore: pebbles.savingAppstore
    property alias preferredAppstore: pebbles.preferredAppstore
    initialPage: Qt.resolvedUrl("pages/LoadingPage.qml")
    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    property int curPebble: -1
    property int knownPebbleCount: 0
    property bool waitingForPebbleIdentity: false
    property string pendingPairAddress: ""
    property var sysProfiles: [ ]

    Connections {
        target: uiActivation
        onFirmwareRequested: stackReloadTimer.restart()
        onAppUpdatesRequested: stackReloadTimer.restart()
        onInstallReviewRequested: installReviewTimer.restart()
        onInstallReviewDaemonChanged: {
            if (activeInstallReview) {
                activeInstallReview.discard()
                activeInstallReview = null
            }
        }
    }

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
            if (uiActivation.firmwareAddress !== "" || uiActivation.appUpdateAddress !== "") {
                stackReloadTimer.restart()
            }
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

    property bool reloadAfterReview: false

    function loadStack() {
        if (activeInstallReview) {
            reloadAfterReview = true
            return
        }
        if (pebbles.connectedToService) {
            pageStack.clear()
            if (pebbles.count === 1) {
                curPebble = 0;
                pageStack.push(Qt.resolvedUrl("pages/MainMenuPage.qml"), {pebble: pebbles.get(curPebble)})
            } else {
                pageStack.push(Qt.resolvedUrl("pages/PebblesPage.qml"))
            }
            openRequestedFirmware()
            openRequestedAppUpdates()
        } else {
            console.log("Waiting for service")
            if(curPebble>=0) {
                pageStack.clear();
                pageStack.push(initialPage);
                curPebble = -1;
            }
        }
    }
    function openRequestedFirmware() {
        var address = uiActivation.firmwareAddress
        if (!address || !pebbles.connectedToService) {
            return
        }
        for (var i = 0; i < pebbles.count; ++i) {
            var watch = pebbles.get(i)
            if (watch && watch.address.toUpperCase() === address) {
                curPebble = i
                pageStack.clear()
                pageStack.push(Qt.resolvedUrl("pages/MainMenuPage.qml"), {pebble: watch})
                pageStack.push(Qt.resolvedUrl("pages/FirmwareUpgradePage.qml"), {pebble: watch})
                uiActivation.consumeFirmwareRequest()
                return
            }
        }
    }
    Component.onCompleted: {
        loadStack()
        openRequestedInstallReview()
    }

    property var activeInstallReview: null

    Component {
        id: installReviewDialog
        InstallReviewDialog {}
    }

    Timer {
        id: installReviewTimer
        interval: 0
        onTriggered: openRequestedInstallReview()
    }

    Connections {
        target: pageStack
        onBusyChanged: if (!pageStack.busy && uiActivation.installReviewToken)
                           installReviewTimer.restart()
    }

    function openRequestedInstallReview() {
        var token = uiActivation.installReviewToken
        var json = uiActivation.installReviewJson
        var owner = uiActivation.installReviewOwner
        if (!token || !json || pageStack.busy)
            return
        var review
        try {
            review = JSON.parse(json)
        } catch (error) {
            uiActivation.consumeInstallReview()
            return
        }
        if (!review || typeof review.appName !== "string" || !Array.isArray(review.permissions)) {
            uiActivation.consumeInstallReview()
            return
        }
        if (activeInstallReview) {
            if (activeInstallReview.token)
                activeInstallReview.discard()
            return
        }
        var dialog = pageStack.push(installReviewDialog, {
            token: token,
            owner: owner,
            host: rockPool,
            review: review
        })
        if (dialog) {
            activeInstallReview = dialog
            uiActivation.consumeInstallReview()
        }
    }

    function installReviewClosed(dialog) {
        if (activeInstallReview === dialog)
            activeInstallReview = null
        if (uiActivation.installReviewToken)
            installReviewTimer.restart()
        if (reloadAfterReview && !activeInstallReview) {
            reloadAfterReview = false
            stackReloadTimer.restart()
        }
    }

    function openRequestedAppUpdates() {
        var address = uiActivation.appUpdateAddress
        if (!address || !pebbles.connectedToService) {
            return
        }
        for (var i = 0; i < pebbles.count; ++i) {
            var watch = pebbles.get(i)
            if (watch && watch.address.toUpperCase() === address) {
                curPebble = i
                pageStack.clear()
                pageStack.push(Qt.resolvedUrl("pages/MainMenuPage.qml"), {pebble: watch})
                if (uiActivation.appUpdateUuid !== "") {
                    pageStack.push(Qt.resolvedUrl("pages/AppUpgradePage.qml"), {
                        pebble: watch,
                        app_uuid: uiActivation.appUpdateUuid
                    })
                } else {
                    pageStack.push(Qt.resolvedUrl("pages/InstalledAppsPage.qml"), {
                        pebble: watch,
                        showWatchApps: !uiActivation.watchfaceUpdate,
                        showWatchFaces: uiActivation.watchfaceUpdate
                    })
                }
                uiActivation.consumeAppUpdateRequest()
                return
            }
        }
    }
    function getCurPebble() {
        if(curPebble>=0) return pebbles.get(curPebble);
        return null;
    }
}
