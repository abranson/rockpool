import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.DBus 2.0

Column {
    id: root

    property bool active
    property bool installed
    property bool paired
    property bool savingPairing
    property bool backgroundAllowed
    property bool policyReady
    property bool busy
    property int revision
    property string error
    readonly property string packageName: "org.rockpool.droidpool"

    signal uninstalled()
    signal forgetPairing()

    onActiveChanged: if (active) refresh()

    function refresh() {
        if (busy) return
        var request = ++revision
        policyReady = false
        apkd.call("getAppSize", [packageName], function(sizes) {
            if (request !== revision) return
            installed = sizes[0] > 0
            if (installed) {
                apkd.call("getAppStartOnBootup", [packageName], function(allowed) {
                    if (request !== revision) return
                    backgroundAllowed = allowed
                    policyReady = true
                }, function() {
                    if (request === revision) error = qsTr("Could not read the AppSupport background setting.")
                })
            }
        }, function() {
            if (request === revision) error = qsTr("Could not check whether Droidpool is installed.")
        })
    }

    function activity(action, name) {
        return [action, "", "", packageName, packageName + "." + name, "", {}]
    }

    function setEnabled(allow) {
        busy = true
        ++revision
        error = ""
        var intent = activity(packageName + (allow ? ".ENABLE" : ".DISABLE"), "ControlActivity")
        function failed() {
            error = qsTr("Could not change the Droidpool setting.")
            busy = false
            refresh()
        }
        apkd.call("queryIntent", intent, function(matches) {
            if (!matches.length) {
                busy = false
                error = qsTr("Install the bundled Droidpool update to use this switch.")
                return
            }
            apkd.call("setAppStartOnBootup", [packageName, allow], function() {
                apkd.call("launchIntent", intent, function() {
                    busy = false
                    refresh()
                }, function() {
                    apkd.call("setAppStartOnBootup", [packageName, !allow], failed, failed)
                })
            }, failed)
        }, failed)
    }

    DBusInterface {
        id: apkd

        service: "com.jolla.apkd"
        path: "/com/jolla/apkd"
        iface: "com.jolla.apkd"
        signalsEnabled: root.visible
        propertiesEnabled: false

        function appStartOnBootupChanged(name, allowed) {
            if (name === root.packageName) root.backgroundAllowed = allowed
        }
        function installFileCompleted(file, name, result) {
            if (name === root.packageName) root.refresh()
        }
        function removeFileCompleted(name, result) {
            if (name !== root.packageName) return
            removalTimeout.stop()
            root.busy = false
            if (result === 0) {
                root.installed = false
                root.uninstalled()
            } else {
                root.error = qsTr("Droidpool was not uninstalled.")
            }
            root.refresh()
        }
    }

    Timer {
        id: removalTimeout

        interval: 30000
        onTriggered: {
            root.busy = false
            root.refresh()
        }
    }

    SectionHeader { text: qsTr("Android companion apps") }

    Button {
        anchors.horizontalCenter: parent.horizontalCenter
        visible: !root.installed
        enabled: !root.busy
        text: qsTr("Install Droidpool in AppSupport")
        onClicked: Qt.openUrlExternally("file:///usr/share/rockpool/Droidpool.apk")
    }

    TextSwitch {
        visible: root.installed
        enabled: root.policyReady && !root.busy
        checked: root.backgroundAllowed
        automaticCheck: false
        text: qsTr("Enable Droidpool")
        description: qsTr("Run the companion bridge now and when AppSupport starts.")
        onClicked: root.setEnabled(!checked)
    }

    ListItem {
        id: openItem

        width: parent.width
        visible: root.installed
        enabled: !root.busy
        contentHeight: root.paired ? Theme.itemSizeMedium : Theme.itemSizeSmall
        onClicked: apkd.call("launchIntent", root.activity("android.intent.action.MAIN", "MainActivity"),
                             function() {}, function() { root.error = qsTr("Could not open Droidpool.") })

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Theme.horizontalPageMargin
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter

            Label {
                width: parent.width
                text: qsTr("Open Droidpool")
                color: openItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                truncationMode: TruncationMode.Fade
            }
            Label {
                width: parent.width
                visible: root.paired
                text: qsTr("Paired with this watch")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                truncationMode: TruncationMode.Fade
            }
        }

        IconButton {
            id: actions

            anchors.right: parent.right
            anchors.rightMargin: Theme.paddingMedium
            anchors.verticalCenter: parent.verticalCenter
            icon.source: "image://theme/icon-m-menu"
            Accessible.name: qsTr("Droidpool options")
            onClicked: openItem.menuOpen ? openItem.closeMenu() : openItem.openMenu()
        }

        menu: ContextMenu {
            MenuItem {
                text: qsTr("Install Droidpool update")
                onClicked: Qt.openUrlExternally("file:///usr/share/rockpool/Droidpool.apk")
            }
            MenuItem {
                visible: root.paired
                enabled: !root.savingPairing
                text: qsTr("Forget Droidpool pairing")
                onClicked: root.forgetPairing()
            }
            MenuItem {
                text: qsTr("Uninstall Droidpool")
                onClicked: pageStack.push(uninstallDialog)
            }
        }
    }

    Component {
        id: uninstallDialog

        Dialog {
            Column {
                width: parent.width
                DialogHeader { acceptText: qsTr("Uninstall Droidpool") }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * x
                    wrapMode: Text.Wrap
                    text: qsTr("Removing Droidpool deletes its pairing code. You will need to pair again if you reinstall it.")
                }
            }
            onAccepted: {
                root.error = ""
                root.busy = true
                ++root.revision
                removalTimeout.start()
                function failed() {
                    removalTimeout.stop()
                    root.busy = false
                    root.error = qsTr("Could not uninstall Droidpool.")
                    root.refresh()
                }
                apkd.call("removePackage", [root.packageName], function(accepted) {
                    if (!accepted) failed()
                }, failed)
            }
        }
    }

    Label {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * x
        visible: text.length > 0
        text: root.error
        wrapMode: Text.Wrap
        color: Theme.errorColor
    }
}
