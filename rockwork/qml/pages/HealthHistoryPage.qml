import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root

    property var pebble: null
    property var overview: pebble ? pebble.healthOverview : ({})
    property bool hasOverview: Object.keys(overview).length > 0
    property bool healthEnabled: pebble && pebble.healthParamsReady
                                 && !!pebble.healthParams["enabled"]
    property bool automaticSyncRequested
    property bool syncFailed
    property color stepsColor: "#52a848"
    property color sleepColor: "#2d79b7"
    property color deepSleepColor: "#14507f"
    property color heartColor: "#d26363"

    allowedOrientations: Orientation.All

    function refreshOverview() {
        if (!pebble) {
            return
        }
        pebble.refreshHealthParams()
        pebble.refreshHealthOverview()
    }

    function requestSync() {
        if (!pebble || !pebble.connected || !healthEnabled
                || pebble.healthSyncing) {
            return
        }
        syncFailed = false
        pebble.fetchHealthData()
    }

    function maybeRequestInitialSync() {
        if (!automaticSyncRequested && pebble && pebble.healthOverviewReady
                && pebble.connected && healthEnabled
                && Number(overview["latestDataTimestamp"]) <= 0) {
            automaticSyncRequested = true
            requestSync()
        }
    }

    function formatCount(value) {
        return value > 0 ? String(value) : "0"
    }

    function formatDuration(seconds) {
        return seconds > 0 ? qsTr("%1 h").arg((seconds / 3600).toFixed(1)) : "--"
    }

    function formatHeartRate(value) {
        return value > 0 ? qsTr("%1 bpm").arg(value) : "--"
    }

    function formatTimestamp(timestamp) {
        if (timestamp <= 0) {
            return qsTr("No health data synced yet.")
        }
        return qsTr("Last updated %1").arg(
                    Qt.formatDateTime(new Date(timestamp * 1000), "ddd hh:mm"))
    }

    function maxValue(list, key) {
        var maximum = 0
        if (!list) {
            return 1
        }
        for (var i = 0; i < list.length; ++i) {
            maximum = Math.max(maximum, Number(list[i][key]))
        }
        return Math.max(maximum, 1)
    }

    function barHeight(value, maximum, availableHeight) {
        if (value <= 0 || maximum <= 0) {
            return 0
        }
        return Math.max(8, Math.round((value / maximum) * availableHeight))
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            refreshOverview()
        }
    }

    Connections {
        target: root.pebble
        onHealthOverviewReadyChanged: root.maybeRequestInitialSync()
        onHealthParamsChanged: root.maybeRequestInitialSync()
        onHealthParamsReadyChanged: root.maybeRequestInitialSync()
        onConnectedChanged: root.maybeRequestInitialSync()
        onHealthSyncCompleted: {
            root.syncFailed = !success
        }
        onHealthDataChanged: root.syncFailed = false
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: contentColumn.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: root.pebble && root.pebble.healthSyncing
                      ? qsTr("Syncing…") : qsTr("Sync now")
                enabled: root.pebble && root.pebble.connected && root.healthEnabled
                         && !root.pebble.healthSyncing
                onClicked: root.requestSync()
            }
            MenuItem {
                text: qsTr("Health settings")
                enabled: root.pebble !== null
                onClicked: pageStack.push(
                               Qt.resolvedUrl("HealthSettingsDialog.qml"),
                               { pebble: root.pebble })
            }
        }

        VerticalScrollDecorator {}

        Column {
            id: contentColumn

            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Health history")
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.secondaryColor
                text: qsTr("Health history is shared across this Rockpool account. "
                           + "It is not assigned to a specific watch.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                running: root.pebble && !root.pebble.healthOverviewReady
                         && !root.hasOverview
                visible: running
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.pebble && root.pebble.healthOverviewReady
                         && !root.hasOverview
                color: Theme.errorColor
                text: qsTr("Health history could not be loaded. Pull down to retry.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.hasOverview
                color: Theme.secondaryColor
                text: root.pebble && root.pebble.healthSyncing
                      ? qsTr("Syncing health data from the watch.")
                      : root.formatTimestamp(Number(root.overview["latestDataTimestamp"]))
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.syncFailed
                color: Theme.errorColor
                text: qsTr("The watch did not accept the health sync request.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Label {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.pebble && !root.pebble.connected
                color: Theme.secondaryColor
                text: qsTr("Connect a Pebble to sync new health data. Existing history "
                           + "remains available while disconnected.")
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }

            Column {
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium
                visible: root.pebble && root.pebble.healthParamsReady
                         && !root.healthEnabled

                Label {
                    width: parent.width
                    text: qsTr("Enable Pebble Health to collect new activity, sleep, and "
                               + "heart-rate statistics.")
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Open health settings")
                    onClicked: pageStack.push(
                                   Qt.resolvedUrl("HealthSettingsDialog.qml"),
                                   { pebble: root.pebble })
                }
            }

            Column {
                width: parent.width
                spacing: Theme.paddingLarge
                visible: root.hasOverview

                Rectangle {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: stepsColumn.height + 2 * Theme.paddingLarge
                    radius: Theme.paddingMedium
                    color: "#143e8f40"

                    Column {
                        id: stepsColumn

                        x: Theme.paddingLarge
                        y: Theme.paddingLarge
                        width: parent.width - 2 * Theme.paddingLarge
                        spacing: Theme.paddingMedium

                        Label {
                            text: qsTr("Today's steps")
                        }
                        Label {
                            text: root.formatCount(Number(root.overview["todaySteps"]))
                            color: root.stepsColor
                            font.pixelSize: Theme.fontSizeExtraLarge
                        }
                        Label {
                            width: parent.width
                            color: Theme.secondaryColor
                            text: qsTr("Average %1 per day").arg(
                                      root.formatCount(Number(
                                          root.overview["averageStepsPerDay"])))
                            wrapMode: Text.Wrap
                        }
                        Row {
                            id: stepsRow

                            width: parent.width
                            spacing: Theme.paddingSmall

                            Repeater {
                                model: root.overview["stepsWeek"] || []

                                delegate: Column {
                                    width: (stepsRow.width - stepsRow.spacing * 6) / 7
                                    spacing: Theme.paddingSmall / 2

                                    Label {
                                        width: parent.width
                                        color: Theme.secondaryColor
                                        font.pixelSize: Theme.fontSizeTiny
                                        horizontalAlignment: Text.AlignHCenter
                                        text: root.formatCount(Number(modelData["steps"]))
                                    }
                                    Item {
                                        width: parent.width
                                        height: Theme.itemSizeMedium

                                        Rectangle {
                                            width: Theme.paddingLarge
                                            height: root.barHeight(
                                                        Number(modelData["steps"]),
                                                        root.maxValue(
                                                            root.overview["stepsWeek"],
                                                            "steps"), parent.height)
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottom: parent.bottom
                                            radius: Theme.paddingSmall
                                            color: root.stepsColor
                                        }
                                    }
                                    Label {
                                        width: parent.width
                                        color: Theme.secondaryColor
                                        font.pixelSize: Theme.fontSizeTiny
                                        horizontalAlignment: Text.AlignHCenter
                                        text: modelData["label"]
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: sleepColumn.height + 2 * Theme.paddingLarge
                    radius: Theme.paddingMedium
                    color: "#14215b8d"

                    Column {
                        id: sleepColumn

                        x: Theme.paddingLarge
                        y: Theme.paddingLarge
                        width: parent.width - 2 * Theme.paddingLarge
                        spacing: Theme.paddingMedium

                        Label {
                            text: qsTr("Last night's sleep")
                        }
                        Row {
                            width: parent.width
                            spacing: Theme.paddingLarge

                            Column {
                                width: (parent.width - Theme.paddingLarge) / 2
                                Label {
                                    text: root.formatDuration(Number(
                                        root.overview["lastNightSleepSeconds"]))
                                    color: root.sleepColor
                                    font.pixelSize: Theme.fontSizeExtraLarge
                                }
                                Label {
                                    width: parent.width
                                    color: Theme.secondaryColor
                                    text: qsTr("Average %1 per night").arg(
                                              root.formatDuration(Number(root.overview[
                                                  "averageSleepSecondsPerDay"])))
                                    wrapMode: Text.Wrap
                                }
                            }
                            Column {
                                width: (parent.width - Theme.paddingLarge) / 2
                                Label {
                                    text: root.formatDuration(Number(
                                        root.overview["lastNightDeepSleepSeconds"]))
                                    color: root.deepSleepColor
                                    font.pixelSize: Theme.fontSizeLarge
                                }
                                Label {
                                    color: Theme.secondaryColor
                                    text: qsTr("Deep sleep")
                                }
                            }
                        }
                        Row {
                            id: sleepRow

                            width: parent.width
                            spacing: Theme.paddingSmall

                            Repeater {
                                model: root.overview["sleepWeek"] || []

                                delegate: Column {
                                    width: (sleepRow.width - sleepRow.spacing * 6) / 7
                                    spacing: Theme.paddingSmall / 2

                                    Label {
                                        width: parent.width
                                        color: Theme.secondaryColor
                                        font.pixelSize: Theme.fontSizeTiny
                                        horizontalAlignment: Text.AlignHCenter
                                        text: Number(modelData["sleepDuration"]) > 0
                                              ? (Number(modelData["sleepDuration"]) / 3600).toFixed(1)
                                              : "0.0"
                                    }
                                    Item {
                                        width: parent.width
                                        height: Theme.itemSizeMedium

                                        Rectangle {
                                            width: Theme.paddingLarge
                                            height: root.barHeight(
                                                        Number(modelData["sleepDuration"]),
                                                        root.maxValue(
                                                            root.overview["sleepWeek"],
                                                            "sleepDuration"), parent.height)
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottom: parent.bottom
                                            radius: Theme.paddingSmall
                                            color: root.sleepColor
                                        }
                                        Rectangle {
                                            width: Theme.paddingLarge
                                            height: root.barHeight(
                                                        Number(modelData[
                                                            "deepSleepDuration"]),
                                                        root.maxValue(
                                                            root.overview["sleepWeek"],
                                                            "sleepDuration"), parent.height)
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            anchors.bottom: parent.bottom
                                            radius: Theme.paddingSmall
                                            color: root.deepSleepColor
                                        }
                                    }
                                    Label {
                                        width: parent.width
                                        color: Theme.secondaryColor
                                        font.pixelSize: Theme.fontSizeTiny
                                        horizontalAlignment: Text.AlignHCenter
                                        text: modelData["label"]
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: heartColumn.height + 2 * Theme.paddingLarge
                    radius: Theme.paddingMedium
                    color: "#14a44c4c"
                    visible: Number(root.overview["latestHeartRate"]) > 0
                             || Number(root.overview["todayAverageHeartRate"]) > 0
                             || Number(root.overview["averageHeartRate30Days"]) > 0

                    Column {
                        id: heartColumn

                        x: Theme.paddingLarge
                        y: Theme.paddingLarge
                        width: parent.width - 2 * Theme.paddingLarge
                        spacing: Theme.paddingMedium

                        Label {
                            text: qsTr("Heart rate")
                        }
                        Row {
                            width: parent.width
                            spacing: Theme.paddingMedium

                            Repeater {
                                model: [
                                    { value: root.overview["latestHeartRate"],
                                      label: qsTr("Latest") },
                                    { value: root.overview["todayAverageHeartRate"],
                                      label: qsTr("Today avg") },
                                    { value: root.overview["averageHeartRate30Days"],
                                      label: qsTr("30 day avg") }
                                ]
                                delegate: Column {
                                    width: (heartColumn.width - 2 * Theme.paddingMedium) / 3
                                    Label {
                                        text: root.formatHeartRate(Number(modelData.value))
                                        color: root.heartColor
                                        font.pixelSize: Theme.fontSizeLarge
                                    }
                                    Label {
                                        width: parent.width
                                        color: Theme.secondaryColor
                                        font.pixelSize: Theme.fontSizeTiny
                                        text: modelData.label
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }
                    }
                }

                Label {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: Theme.secondaryColor
                    visible: Number(root.overview["daysOfData"]) > 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("%1 days of health data available.").arg(
                              Number(root.overview["daysOfData"]))
                }
            }
        }
    }
}
