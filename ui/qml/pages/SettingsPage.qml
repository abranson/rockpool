import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.DBus 2.0

Page {
    id: root

    property int lockerEpoch
    property string lockerPreview
    property int lockerCount
    property bool lockerBusy
    property string lockerMessage

    function resetLocker() {
        ++lockerEpoch
        lockerPreview = ""
        lockerCount = 0
        lockerMessage = ""
        lockerBusy = false
    }

    onCoreSelectedChanged: resetLocker()
    onAccountAuthenticatedChanged: resetLocker()
    onAccountPendingChanged: resetLocker()
    onAccountEmailChanged: resetLocker()

    DBusInterface {
        id: lockerManager

        service: "org.rockpool"
        path: "/org/rockpool/Manager"
        iface: "org.rockpool.Manager"
        propertiesEnabled: false
        signalsEnabled: false
    }

    function checkOrCopyLocker() {
        var request = ++lockerEpoch
        var copying = lockerPreview.length > 0
        lockerBusy = true
        lockerMessage = ""
        function failed() {
            if (request !== lockerEpoch) return
            resetLocker()
            lockerMessage = qsTr("Could not finish. Check the locker again.")
        }
        function next() {
            if (request !== lockerEpoch) return
            var method = copying ? "CopyLockerApp" : "PreviewLockerCopy"
            var arg = copying ? lockerPreview : rockPool.preferredAppstore
            var sent = lockerManager.typedCall(method, [{type: "s", value: arg}], function(reply) {
                if (request !== lockerEpoch) return
                if (copying) {
                    if (reply > 0) { next(); return }
                    if (reply < 0) { failed(); return }
                    resetLocker()
                    lockerMessage = qsTr("Locker updated.")
                } else {
                    try {
                        var preview = JSON.parse(reply)
                        lockerCount = preview.count
                        lockerPreview = lockerCount > 0 ? preview.key : ""
                        lockerMessage = lockerCount > 0 ? "" : qsTr("No eligible installed apps to add.")
                        lockerBusy = false
                    } catch (e) { failed() }
                }
            }, failed)
            if (!sent) failed()
        }
        next()
    }

    property var pebble: null
    property string connectedProfile: pebble ? pebble.profileWhenConnected : ""
    property string disconnectedProfile: pebble ? pebble.profileWhenDisconnected : ""
    property bool settingsReady: pebble && pebble.settingsPageReady
    readonly property bool coreSelected: rockPool.preferredAppstore === "core"
    readonly property bool accountReady: rockPool.preferredAppstore.length > 0 && !rockPool.savingAppstore
    readonly property bool accountAuthenticated: coreSelected ? rockPool.coreAccount.signedIn
                                                            : !!pebble && pebble.accountAuthenticated
    readonly property bool accountPending: coreSelected ? rockPool.coreAccount.busy
                                                      : !!pebble && pebble.accountTokenPending
    readonly property string accountName: coreSelected ? rockPool.coreAccount.displayName : (pebble ? pebble.accountName : "")
    readonly property string accountEmail: coreSelected ? rockPool.coreAccount.email : (pebble ? pebble.accountEmail : "")
    readonly property string accountError: coreSelected ? rockPool.coreAccount.error : (pebble ? pebble.accountTokenError : "")
    property var cannedResponses: pebble ? pebble.cannedResponses : {}
    property bool cannedResponsesReady: pebble && pebble.cannedResponsesReady
    property var notificationsMap: pebble ? pebble.notificationsFilter:{}
    property var lowBatteryOptions: [0, 5, 10, 15, 20, 25, 30, 40, 50]

    property string droidpoolWatch
    property string pairingError
    property bool savingPairing

    function writePairing(code) {
        if (savingPairing || !pebble) return
        savingPairing = true
        pairingError = ""
        var address = pebble.address
        function failed() {
            savingPairing = false
            pairingError = qsTr("Could not save Droidpool pairing.")
        }
        if (!lockerManager.typedCall("SetDroidpoolPairing",
                [{type: "s", value: address}, {type: "s", value: code}], function(saved) {
            if (!saved) { failed(); return }
            savingPairing = false
            droidpoolWatch = code.length ? address : ""
            pairingCode.text = ""
        }, failed)) failed()
    }

    property var weatherPinsEnabled
    property bool weatherPinsBusy
    property string weatherPinsError

    function loadWeatherPins() {
        if (weatherPinsBusy)
            return
        weatherPinsBusy = true
        function failed() {
            weatherPinsEnabled = undefined
            weatherPinsBusy = false
            if (!weatherPinsError.length)
                weatherPinsError = qsTr("Could not read the weather pin setting.")
        }
        if (!lockerManager.typedCall("GetWeatherPinsEnabled", [], function(enabled) {
            if (typeof enabled !== "boolean") { failed(); return }
            weatherPinsEnabled = enabled
            weatherPinsBusy = false
        }, failed)) failed()
    }

    function toggleWeatherPins() {
        if (weatherPinsBusy || weatherPinsEnabled === undefined)
            return
        var enabled = !weatherPinsEnabled
        weatherPinsBusy = true
        weatherPinsError = ""
        function failed() {
            weatherPinsBusy = false
            weatherPinsError = qsTr("Could not change the weather pin setting.")
            loadWeatherPins()
        }
        if (!lockerManager.typedCall("SetWeatherPinsEnabled", [{type: "b", value: enabled}], function(saved) {
            if (saved !== true) { failed(); return }
            weatherPinsEnabled = enabled
            weatherPinsBusy = false
        }, failed)) failed()
    }

    property var pendingLanguage

    function signIn() {
        if (!accountReady || accountPending || accountAuthenticated)
            return
        if (coreSelected) {
            rockPool.coreAccount.login()
        } else if (pebble) {
            pageStack.push(Qt.resolvedUrl("AppSettingsPage.qml"), {
                               url: "https://boot.rebble.io",
                               pebble: root.pebble,
                               oauthBootFlow: true
                           })
        }
    }

    function showLanguageRemorse() {
        if (status !== PageStatus.Active || !pendingLanguage) {
            return
        }
        var selection = pendingLanguage
        pendingLanguage = null
        languageRemorse.execute(qsTr("Changing watch language to %1").arg(selection.name), function() {
            if (root.status === PageStatus.Active && root.pebble === selection.watch
                    && root.pebble.connected && root.pebble.languageVersion !== selection.version) {
                root.pebble.loadLanguagePack(selection.file)
            }
        })
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            weatherPinsError = ""
            loadWeatherPins()
            showLanguageRemorse()
        }
    }

    RemorsePopup {
        id: languageRemorse
    }

    Timer {
        id: languageRemorseTimer

        interval: 0
        onTriggered: root.showLanguageRemorse()
    }

    Component.onCompleted: {
        if (appSupportInstalled)
            lockerManager.typedCall("GetDroidpoolWatch", [], function(address) { root.droidpoolWatch = address })
        if (root.pebble) {
            root.pebble.refreshSettingsPage()
            root.pebble.refreshCannedResponses()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Reset Timeline")
                enabled: root.pebble && root.pebble.connected
                onClicked: root.pebble.resetTimeline()
            }
            MenuItem {
                text: qsTr("Login")
                visible: root.accountReady && !root.accountAuthenticated && !root.accountPending
                onClicked: root.signIn()
            }
        }

        VerticalScrollDecorator {}

        Column {
            id: content

            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader {
                text: qsTr("General")
            }
            ComboBox {
                width: parent.width
                label: qsTr("Distance Units")
                enabled: root.settingsReady
                menu: ContextMenu {
                        MenuItem {
                            text: qsTr("Metric")
                            onClicked: root.pebble.imperialUnits = false
                        }
                        MenuItem {
                            text: qsTr("Imperial")
                            onClicked: root.pebble.imperialUnits = true
                        }
                    }
                currentIndex: root.pebble && root.pebble.imperialUnits ? 1 : 0
            }
            WatchLanguageSelector {
                pebble: root.pebble
                onLanguageSelected: {
                    root.pendingLanguage = {watch: root.pebble, file: file, name: name, version: version}
                    // A full-screen picker returns here before showing remorse.
                    // For an inline menu, defer until its click handler has finished.
                    languageRemorseTimer.restart()
                }
            }
            ComboBox {
                width: parent.width
                label: qsTr("Low battery warning")
                enabled: root.settingsReady
                menu: ContextMenu {
                    Repeater {
                        model: root.lowBatteryOptions
                        delegate: MenuItem {
                            text: modelData === 0 ? qsTr("Off") : qsTr("%1%").arg(modelData)
                            down: root.pebble && root.pebble.lowBatteryThreshold === modelData
                            onClicked: root.pebble.lowBatteryThreshold = modelData
                        }
                    }
                }
                value: {
                    var threshold = root.pebble ? root.pebble.lowBatteryThreshold : 10
                    return threshold === 0 ? qsTr("Off") : qsTr("%1%").arg(threshold)
                }
            }

            BackgroundItem {
                id: quietTimeItem

                width: parent.width
                enabled: root.pebble !== null
                onClicked: pageStack.push(Qt.resolvedUrl("QuietTimePage.qml"), {
                                             pebble: root.pebble
                                         })

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x - quietTimeArrow.width - Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Quiet Time")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }
                Icon {
                    id: quietTimeArrow

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-right"
                    highlighted: quietTimeItem.highlighted
                }
            }

            SectionHeader {
                text: qsTr("Store and Account")
            }

            ComboBox {
                width: parent.width
                label: qsTr("Appstore")
                description: qsTr("Browse and check for updates in this store.")
                enabled: rockPool.preferredAppstore.length > 0 && !rockPool.savingAppstore
                currentIndex: rockPool.preferredAppstore === "core" ? 1 : (rockPool.preferredAppstore === "rebble" ? 0 : -1)
                menu: ContextMenu {
                    MenuItem {
                        text: "Rebble"
                        onClicked: rockPool.preferredAppstore = "rebble"
                    }
                    MenuItem {
                        text: "Core Devices"
                        onClicked: rockPool.preferredAppstore = "core"
                    }
                }
            }
            ListItem {
                id: accountItem

                width: parent.width
                contentHeight: accountDetails.height + 2 * Theme.paddingMedium
                visible: root.accountReady
                enabled: root.accountReady && !root.accountPending
                onClicked: {
                    if (root.accountAuthenticated)
                        showMenu()
                    else
                        root.signIn()
                }
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Logout")
                        visible: root.accountAuthenticated && root.accountReady
                        onClicked: {
                            if (root.coreSelected)
                                rockPool.coreAccount.logout()
                            else if (root.pebble)
                                root.pebble.setOAuthToken("")
                        }
                    }
                }

                Column {
                    id: accountDetails

                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    y: Theme.paddingMedium
                    spacing: Theme.paddingSmall

                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        property string accountValue: root.accountAuthenticated
                                ? (root.accountName || root.accountEmail || qsTr("Signed in"))
                                : (root.coreSelected && root.accountPending
                                   ? qsTr("Finish signing in in your browser.") : qsTr("Not signed in"))
                        text: qsTr("Account: %1").arg(accountValue)
                        color: accountItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        visible: root.accountAuthenticated && root.accountName.length > 0
                                 && root.accountEmail.length > 0 && root.accountEmail !== root.accountName
                        textFormat: Text.PlainText
                        text: root.accountEmail
                        color: accountItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                    }
                }
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: root.accountReady && root.accountPending
                size: BusyIndicatorSize.Small
                visible: running
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: root.accountReady && root.accountError.length > 0
                text: root.accountError
                color: Theme.errorColor
                wrapMode: Text.Wrap
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.coreSelected && rockPool.coreAccount.busy
                text: qsTr("Cancel sign-in")
                onClicked: rockPool.coreAccount.cancelLogin()
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Sync Apps from Cloud")
                enabled: root.settingsReady
                automaticCheck: false
                checked: root.pebble ? root.pebble.syncAppsFromCloud : false
                onClicked: root.pebble.syncAppsFromCloud =
                           !root.pebble.syncAppsFromCloud
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.accountReady && root.accountAuthenticated && rockPool.knownPebbleCount === 1
                enabled: !root.accountPending && !root.lockerBusy
                text: root.lockerBusy ? (root.lockerPreview.length ? qsTr("Adding apps…") : qsTr("Checking locker…"))
                                      : root.lockerPreview.length ? qsTr("Add to %1 (%2)").arg(root.coreSelected ? "Core Devices" : "Rebble").arg(root.lockerCount)
                                                                  : qsTr("Check for missing apps")
                onClicked: root.checkOrCopyLocker()
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: root.lockerMessage.length > 0
                text: root.lockerMessage
                wrapMode: Text.Wrap
            }

            DroidpoolControls {
                id: droidpool
                width: parent.width
                visible: appSupportInstalled
                active: appSupportInstalled && root.status === PageStatus.Active && Qt.application.active
                paired: !!root.pebble && root.droidpoolWatch === root.pebble.address
                savingPairing: root.savingPairing
                onForgetPairing: root.writePairing("")
                onUninstalled: {
                    if (root.droidpoolWatch.length)
                        root.writePairing("")
                }
            }

            TextField {
                id: pairingCode

                width: parent.width
                visible: droidpool.installed && root.pebble && root.droidpoolWatch !== root.pebble.address
                echoMode: TextInput.Password
                label: qsTr("Droidpool pairing code")
                placeholderText: label
                description: root.droidpoolWatch.length
                             ? qsTr("Pairing here moves Droidpool to this watch.")
                             : qsTr("Open Droidpool, copy its pairing code and paste it here.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: droidpool.installed && !!root.pebble && !droidpool.paired
                text: qsTr("Pair this watch")
                enabled: !root.savingPairing && /^[A-Za-z0-9_-]{43}$/.test(pairingCode.text.trim())
                onClicked: root.writePairing(pairingCode.text.trim())
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                visible: root.pairingError.length > 0
                text: root.pairingError
                wrapMode: Text.Wrap
                color: Theme.errorColor
            }

            SectionHeader {
                text: qsTr("Timeline")
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Sync calendar to timeline")
                enabled: root.settingsReady
                automaticCheck: false
                checked: root.pebble ? root.pebble.calendarSyncEnabled : false
                onClicked: root.pebble.calendarSyncEnabled =
                           !root.pebble.calendarSyncEnabled
            }
            TextSwitch {
                width: parent.width
                text: qsTr("Show sunrise and sunset pins")
                description: root.weatherPinsError
                enabled: !root.weatherPinsBusy && root.weatherPinsEnabled !== undefined
                automaticCheck: false
                checked: root.weatherPinsEnabled === true
                onClicked: root.toggleWeatherPins()
            }
            BackgroundItem {
                id: timelineWindowItem

                width: parent.width
                enabled: root.pebble !== null
                onClicked: pageStack.push(Qt.resolvedUrl("TimelineSettingsDialog.qml"), {
                                             pebble: root.pebble
                                         })

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x - timelineArrow.width - Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Timeline window")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                }
                Icon {
                    id: timelineArrow

                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-right"
                    highlighted: timelineWindowItem.highlighted
                }
            }

            SectionHeader {
                text: qsTr("Automatic Profile")
            }
            ComboBox {
                width: parent.width
                label: qsTr("Connected")
                enabled: root.settingsReady
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("no change")
                        down: root.connectedProfile === ""
                        onClicked: root.pebble.profileWhenConnected = ""
                    }
                    Repeater {
                        model: rockPool.sysProfiles
                        delegate: MenuItem {
                            text: modelData
                            down: modelData === root.connectedProfile
                            onClicked: root.pebble.profileWhenConnected = modelData
                        }
                    }
                }
                value: root.connectedProfile === "" ? qsTr("no change") : root.connectedProfile
            }
            ComboBox {
                width: parent.width
                label: qsTr("Disconnected")
                enabled: root.settingsReady
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("no change")
                        down: root.disconnectedProfile === ""
                        onClicked: root.pebble.profileWhenDisconnected = ""
                    }
                    Repeater {
                        model: rockPool.sysProfiles
                        delegate: MenuItem {
                            text: modelData
                            down: modelData === root.disconnectedProfile
                            onClicked: root.pebble.profileWhenDisconnected = modelData
                        }
                    }
                }
                value: root.disconnectedProfile === "" ? qsTr("no change") : root.disconnectedProfile
            }
            SectionHeader {
                text: qsTr("Canned Messages")
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: !root.cannedResponsesReady
                visible: running
            }
            Repeater {
                model: root.cannedResponsesReady ? Object.keys(cannedResponses) : []
                delegate: BackgroundItem {
                    id: responseItem

                    width: content.width

                    Row {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * x
                        anchors.verticalCenter: parent.verticalCenter
                        height: Theme.itemSizeSmall
                        spacing: Theme.paddingMedium
                        Image {
                            id: responseIcon

                            anchors.verticalCenter: parent.verticalCenter
                            height: Theme.iconSizeSmall
                            width: height
                            source: {
                                var icon = (modelData in notificationsMap)?notificationsMap[modelData]["icon"]:"icon-lock-chat";
                                if (icon.indexOf("image://") === 0 || icon.indexOf("file://")  === 0)
                                    return icon;
                                else if (icon.indexOf("/") === 0)
                                    return "file://" + icon
                                else
                                    return "image://theme/"+icon

                            }
                        }
                        Label {
                            width: parent.width - responseIcon.width - parent.spacing
                            truncationMode: TruncationMode.Fade
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData in notificationsMap)?notificationsMap[modelData]["name"]:modelData
                            color: responseItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                    onClicked: {
                        console.log(modelData,cannedResponses[modelData]);
                        pageStack.push(Qt.resolvedUrl("ResponsesPage.qml"), {
                                           pebble: root.pebble,
                                           source: modelData,
                                           title: (modelData in notificationsMap)?notificationsMap[modelData]["name"]:modelData,
                                           list: cannedResponses[modelData].slice(0)})
                    }
                }
            }
        }
        Component.onCompleted: rockPool.getProfiles()
    }
}
