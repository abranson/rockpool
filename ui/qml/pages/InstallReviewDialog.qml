import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.DBus 2.0
import "PermissionLabels.js" as PermissionLabels

Dialog {
    id: root

    property string token
    property string owner
    property var host
    property var review: ({})
    property var decisions: ({})
    property bool suppressReply
    property bool readDecisionSaved
    property var readDecisionBefore

    canAccept: allRequiredDecided()

    DBusInterface {
        id: manager
        service: root.owner || "org.rockpool"
        path: "/org/rockpool/Manager"
        iface: "org.rockpool.Manager"
        propertiesEnabled: false
        signalsEnabled: false
    }

    function allRequiredDecided() {
        var rows = review.permissions || []
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].needsAnswer === true && decisions[rows[i].key] === undefined)
                return false
        }
        return true
    }

    function setDecision(key, granted) {
        var updated = {}
        for (var existing in decisions)
            updated[existing] = decisions[existing]
        updated[key] = granted
        if (key === "WriteFiles" && granted) {
            var rows = review.permissions || []
            for (var i = 0; i < rows.length; ++i) {
                if (rows[i].key === "ReadFiles" && !readDecisionSaved) {
                    readDecisionBefore = decisions.ReadFiles
                    readDecisionSaved = true
                }
                if (rows[i].key === "ReadFiles")
                    updated.ReadFiles = true
            }
        } else if (key === "WriteFiles" && !granted && readDecisionSaved) {
            if (readDecisionBefore === undefined)
                delete updated.ReadFiles
            else
                updated.ReadFiles = readDecisionBefore
            readDecisionSaved = false
            readDecisionBefore = undefined
        }
        decisions = updated
    }

    function submit(value) {
        if (!token)
            return
        var requestToken = token
        root.token = ""
        manager.typedCall("AnswerInstallReview", [
            {type: "s", value: requestToken},
            {type: "s", value: value === null ? "null" : JSON.stringify(value)}
        ], function() {}, function() {})
    }

    function discard() {
        suppressReply = true
        reject()
    }

    Component.onDestruction: {
        if (!suppressReply && token)
            submit(null)
        if (host)
            host.installReviewClosed(root)
    }

    onAccepted: {
        var result = {}
        var rows = review.permissions || []
        for (var i = 0; i < rows.length; ++i) {
            if (decisions[rows[i].key] !== undefined)
                result[rows[i].key] = decisions[rows[i].key]
        }
        submit(result)
    }

    onRejected: if (!suppressReply) submit(null)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge
        clip: true

        VerticalScrollDecorator {}

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingSmall

            DialogHeader {
                title: qsTr("Install %1?").arg(root.review.appName || qsTr("app"))
                acceptText: qsTr("Install")
                cancelText: qsTr("Cancel")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * x
                text: qsTr("Review what this app can access on your phone.")
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
            }

            Repeater {
                model: root.review.permissions || []

                delegate: Item {
                    id: permissionRow
                    property bool decided: root.decisions[modelData.key] !== undefined
                    property bool granted: decided ? root.decisions[modelData.key] === true : modelData.automatic === true
                    width: content.width - 2 * Theme.horizontalPageMargin
                    x: Theme.horizontalPageMargin
                    height: Math.max(details.height, actions.height)

                    Column {
                        id: details
                        anchors.left: parent.left
                        anchors.right: actions.left
                        anchors.rightMargin: Theme.paddingMedium
                        spacing: Theme.paddingSmall

                        Label {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: PermissionLabels.label(modelData.key, modelData.label)
                            wrapMode: Text.Wrap
                        }
                        Label {
                            width: parent.width
                            textFormat: Text.PlainText
                            text: (modelData.reason ? modelData.reason + "\n" : "")
                                  + (modelData.required === true ? qsTr("Required by the app") : qsTr("Optional"))
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }
                        Label {
                            width: parent.width
                            text: permissionRow.decided ? (permissionRow.granted ? qsTranslate("AppPermissionsPage", "Allowed")
                                                                                : qsTranslate("AppPermissionsPage", "Denied"))
                                  : modelData.automatic === true ? qsTr("Allowed automatically")
                                  : (modelData.needsAnswer === true ? qsTr("Choose whether to allow this") : qsTr("Optional decision"))
                            color: permissionRow.decided ? Theme.highlightColor : Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }
                        Label {
                            width: parent.width
                            visible: PermissionLabels.scope(modelData.key) !== ""
                            textFormat: Text.PlainText
                            text: qsTr("Scope: %1").arg(PermissionLabels.scope(modelData.key))
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }
                    }

                    Row {
                        id: actions
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.paddingSmall

                        IconButton {
                            Accessible.name: qsTr("Allow")
                            Accessible.role: Accessible.Button
                            icon.source: "image://theme/icon-m-accept"
                            icon.color: permissionRow.granted ? Theme.highlightColor : Theme.primaryColor
                            icon.opacity: permissionRow.granted ? 1.0 : Theme.opacityHigh
                            onClicked: root.setDecision(modelData.key, true)
                        }
                        IconButton {
                            Accessible.name: qsTr("Deny")
                            Accessible.role: Accessible.Button
                            icon.source: "image://theme/icon-m-cancel"
                            icon.color: permissionRow.decided && !permissionRow.granted ? Theme.highlightColor : Theme.primaryColor
                            icon.opacity: permissionRow.decided && !permissionRow.granted ? 1.0 : Theme.opacityHigh
                            enabled: modelData.key !== "ReadFiles" || root.decisions.WriteFiles !== true
                            onClicked: root.setDecision(modelData.key, false)
                        }
                    }
                }
            }
        }
    }
}
