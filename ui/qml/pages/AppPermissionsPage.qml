import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.DBus 2.0
import Sailfish.Pickers 1.0
import "PermissionLabels.js" as PermissionLabels

Page {
    id: root

    property string appUuid
    property string appName
    property string viewState: "loading"
    property var permissions: []
    property bool saving
    property int requestNumber
    property string changeError

    onStatusChanged: {
        if (status === PageStatus.Active)
            loadPermissions()
    }

    onAppUuidChanged: {
        ++requestNumber
        saving = false
        changeError = ""
        loadPermissions()
    }

    DBusInterface {
        id: manager

        service: "org.rockpool"
        path: "/org/rockpool/Manager"
        iface: "org.rockpool.Manager"
        propertiesEnabled: false
        signalsEnabled: false
    }

    function loadPermissions() {
        if (status !== PageStatus.Active || saving)
            return

        var request = ++requestNumber
        var uuid = appUuid
        permissions = []
        if (!uuid) {
            viewState = "unknown"
            return
        }
        viewState = "loading"

        function current() {
            return request === root.requestNumber && uuid === root.appUuid
                && root.status === PageStatus.Active && root.viewState === "loading"
        }

        function failed() {
            if (current())
                root.viewState = "error"
        }

        var sent = manager.typedCall("GetAppPermissions", [{type: "s", value: uuid}], function(reply) {
            if (!current())
                return
            var result
            try {
                result = JSON.parse(reply)
            } catch (error) {
                failed()
                return
            }
            if (!result || typeof result.known !== "boolean" || !Array.isArray(result.permissions)
                    || !result.permissions.every(function(row) {
                        return row && typeof row.key === "string" && row.key.length > 0
                            && typeof row.status === "string" && typeof row.editable === "boolean"
                    })) {
                failed()
                return
            }
            root.permissions = result.known ? result.permissions : []
            root.viewState = !result.known ? "unknown" : (root.permissions.length ? "ready" : "empty")
        }, failed)
        if (!sent)
            failed()
    }

    function changePermission(entry, granted) {
        if (status !== PageStatus.Active || viewState !== "ready" || saving || entry.editable !== true)
            return

        saveChange("SetAppPermission", [{type: "s", value: appUuid},
                   {type: "s", value: entry.key}, {type: "b", value: granted}])
    }

    function chooseFolder(entry) {
        if (saving || status !== PageStatus.Active)
            return
        pageStack.push(folderPicker, {targetUuid: appUuid, writable: entry.key === "WriteFiles"})
    }

    Component {
        id: folderPicker

        FolderPickerPage {
            property string targetUuid
            property bool writable

            dialogTitle: writable ? qsTr("Choose a folder for reading and writing") : qsTr("Choose a folder for reading")
            onSelectedPathChanged: {
                if (selectedPath && targetUuid === root.appUuid && !root.saving) {
                    root.saveChange("GrantAppFolder", [{type: "s", value: targetUuid},
                                    {type: "s", value: selectedPath}, {type: "b", value: writable}])
                }
            }
        }
    }

    function revokeFolder(entry) {
        saveChange("RevokeAppFolder", [{type: "s", value: appUuid},
                   {type: "s", value: permissionScope(entry.key)}])
    }

    function saveChange(method, args) {
        if (saving)
            return
        var request = ++requestNumber
        var uuid = appUuid
        saving = true
        changeError = ""

        function completed(success) {
            if (request !== root.requestNumber || uuid !== root.appUuid || !root.saving)
                return
            root.saving = false
            if (success !== true)
                root.changeError = qsTr("Could not change the permission.")
            root.loadPermissions()
        }

        var sent = manager.typedCall(method, args, completed, function() { completed(false) })
        if (!sent)
            completed(false)
    }

    function permissionLabel(entry) {
        return PermissionLabels.label(entry.key, entry.label)
    }

    function permissionScope(key) {
        return PermissionLabels.scope(key)
    }

    function permissionStatus(value) {
        switch (value) {
        case "Granted": return qsTr("Allowed")
        case "Denied": return qsTr("Denied")
        case "AllowedAutomatically": return qsTr("Allowed automatically")
        case "Waiting": return qsTr("Awaiting a decision")
        default: return qsTr("Status unavailable")
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height
        clip: true

        Column {
            id: content

            width: parent.width
            spacing: Theme.paddingLarge
            bottomPadding: Theme.paddingLarge

            PageHeader {
                title: qsTr("App permissions")
            }

            Column {
                width: parent.width - 2 * Theme.horizontalPageMargin
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                Label {
                    width: parent.width
                    text: root.appName
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                }

                Label {
                    width: parent.width
                    text: qsTr("Permission changes apply to this app on all watches.")
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }

                Label {
                    width: parent.width
                    text: root.changeError
                    color: Theme.highlightColor
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                }

                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    running: root.viewState === "loading" || root.saving
                    visible: running
                    size: BusyIndicatorSize.Medium
                }

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: text.length > 0
                    text: {
                        if (root.viewState === "error")
                            return qsTr("Could not load permissions.")
                        if (root.viewState === "unknown")
                            return qsTr("Permission information is unavailable for this app.")
                        if (root.viewState === "empty")
                            return qsTr("This app has no permissions to review.")
                        return ""
                    }
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Retry")
                    visible: root.viewState === "error" || root.viewState === "unknown"
                    onClicked: root.loadPermissions()
                }
            }

            Repeater {
                model: root.permissions

                delegate: Item {
                    id: permissionRow

                    readonly property bool folderGrant: modelData.folderPath !== undefined
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Math.max(permissionDetails.height, permissionActions.visible ? permissionActions.height : 0)
                    x: Theme.horizontalPageMargin

                    Column {
                        id: permissionDetails

                        anchors.left: parent.left
                        anchors.right: permissionActions.visible ? permissionActions.left : parent.right
                        anchors.rightMargin: permissionActions.visible ? Theme.paddingMedium : 0
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.paddingSmall

                        Label {
                            width: parent.width
                            text: root.permissionLabel(modelData)
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                        }

                        Label {
                            width: parent.width
                            text: permissionRow.folderGrant ? (modelData.folderPath || qsTr("Unavailable folder")) : root.permissionScope(modelData.key)
                            textFormat: Text.PlainText
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }

                        Label {
                            width: parent.width
                            text: root.permissionStatus(modelData.status)
                            color: Theme.highlightColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }

                        Label {
                            width: parent.width
                            text: modelData.required === true ? qsTr("Required by the app")
                                  : (modelData.required === false ? qsTr("Optional") : "")
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }

                        Label {
                            width: parent.width
                            text: modelData.reason || ""
                            textFormat: Text.PlainText
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                            visible: text.length > 0
                        }

                        Button {
                            text: qsTr("Choose folder")
                            width: Math.min(implicitWidth, parent.width)
                            visible: (modelData.key === "ReadFiles" || modelData.key === "WriteFiles")
                                     && (modelData.status === "Granted" || modelData.status === "AllowedAutomatically")
                            enabled: !root.saving && root.status === PageStatus.Active
                            onClicked: root.chooseFolder(modelData)
                        }
                    }

                    Row {
                        id: permissionActions

                        readonly property real buttonWidth: Math.min(Theme.itemSizeSmall, parent.width / 4)
                        readonly property real iconSize: Math.min(Theme.iconSizeMedium, buttonWidth)

                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.paddingSmall
                        visible: modelData.editable === true || permissionRow.folderGrant
                        enabled: !root.saving && root.status === PageStatus.Active

                        IconButton {
                            width: permissionActions.buttonWidth
                            visible: !permissionRow.folderGrant
                            Accessible.name: qsTr("Allow")
                            Accessible.role: Accessible.Button
                            icon.source: "image://theme/icon-m-accept"
                            icon.color: modelData.status === "Granted" || modelData.status === "AllowedAutomatically"
                                        ? Theme.highlightColor : Theme.primaryColor
                            icon.width: permissionActions.iconSize
                            icon.height: permissionActions.iconSize
                            icon.opacity: permissionActions.enabled ? 1.0 : Theme.opacityLow
                            enabled: modelData.status !== "Granted"
                            onClicked: root.changePermission(modelData, true)
                        }

                        IconButton {
                            width: permissionActions.buttonWidth
                            visible: !permissionRow.folderGrant
                            Accessible.name: qsTr("Deny")
                            Accessible.role: Accessible.Button
                            icon.source: "image://theme/icon-m-cancel"
                            icon.color: modelData.status === "Denied" ? Theme.highlightColor : Theme.primaryColor
                            icon.width: permissionActions.iconSize
                            icon.height: permissionActions.iconSize
                            icon.opacity: permissionActions.enabled ? 1.0 : Theme.opacityLow
                            enabled: modelData.status !== "Denied"
                            onClicked: root.changePermission(modelData, false)
                        }

                        IconButton {
                            width: permissionActions.buttonWidth
                            visible: permissionRow.folderGrant
                            Accessible.name: qsTr("Remove folder access")
                            Accessible.role: Accessible.Button
                            icon.source: "image://theme/icon-m-remove"
                            icon.width: permissionActions.iconSize
                            icon.height: permissionActions.iconSize
                            onClicked: root.revokeFolder(modelData)
                        }
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
