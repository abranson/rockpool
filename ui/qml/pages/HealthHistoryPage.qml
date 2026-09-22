import QtQuick 2.6
import Sailfish.Silica 1.0
import "HealthHistory.js" as History

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

    property bool weeklyView
    property var chartEntries: []
    property real historyPosition
    property int recordedDays
    readonly property int visibleBars: 7

    allowedOrientations: Orientation.All

    function rebuildHistory() {
        var atLatest = historyPosition >= chartEntries.length - visibleBars - 0.5
        var lastVisible = chartEntries[Math.min(chartEntries.length - 1,
                                               Math.floor(historyPosition) + visibleBars - 1)]
        var days = History.dailyRecords(overview)
        var count = 0
        for (var i = 0; i < days.length; ++i) {
            if (days[i].movementDays || days[i].sleepDays)
                count++
        }
        recordedDays = count
        chartEntries = weeklyView ? History.weeklyRecords(days) : days
        historyPosition = atLatest || !lastVisible
                ? Math.max(0, chartEntries.length - visibleBars)
                : History.positionForDate(chartEntries, lastVisible.endDate, visibleBars)
    }

    function visibleDateRange() {
        if (!chartEntries.length)
            return ""
        var first = chartEntries[Math.max(0, Math.min(chartEntries.length - 1,
                                                      Math.floor(historyPosition)))]
        var last = chartEntries[Math.min(chartEntries.length - 1,
                                         Math.ceil(historyPosition) + visibleBars - 1)]
        return Qt.formatDate(History.localDate(first.date), "d MMM yyyy") + " – "
                + Qt.formatDate(History.localDate(last.endDate), "d MMM yyyy")
    }

    onOverviewChanged: rebuildHistory()
    onWeeklyViewChanged: rebuildHistory()
    Component.onCompleted: rebuildHistory()

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

                ComboBox {
                    width: parent.width
                    label: qsTr("Graph view")
                    currentIndex: root.weeklyView ? 1 : 0
                    onCurrentIndexChanged: root.weeklyView = currentIndex === 1
                    menu: ContextMenu {
                        MenuItem { text: qsTr("Daily") }
                        MenuItem { text: qsTr("Weekly") }
                    }
                }

                Label {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.visibleDateRange()
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Label {
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.weeklyView
                          ? qsTr("Weeks start on Monday. Steps are weekly totals; sleep is the average "
                                 + "per recorded night. Incomplete weeks use available data.")
                          : qsTr("Each bar shows one day. Swipe the graphs to browse history.")
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Latest")
                    enabled: root.historyPosition < root.chartEntries.length - root.visibleBars - 0.5
                    onClicked: root.historyPosition = Math.max(0, root.chartEntries.length - root.visibleBars)
                }

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
                        HealthHistoryChart {
                            width: parent.width
                            entries: root.chartEntries
                            position: root.historyPosition
                            visibleCount: root.visibleBars
                            barColor: root.stepsColor
                            secondaryColor: root.stepsColor
                            onScrolled: root.historyPosition = position
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
                        HealthHistoryChart {
                            width: parent.width
                            entries: root.chartEntries
                            position: root.historyPosition
                            visibleCount: root.visibleBars
                            valueKey: "sleepDuration"
                            secondaryKey: "deepSleepDuration"
                            presenceKey: "sleepDays"
                            hours: true
                            barColor: root.sleepColor
                            secondaryColor: root.deepSleepColor
                            onScrolled: root.historyPosition = position
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
                    visible: root.recordedDays > 0
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: root.overview["history"]
                          ? qsTr("%1 recorded days in the last 90 days. Missing data is shown as —.")
                            .arg(root.recordedDays)
                          : qsTr("%1 days of health data available.").arg(
                                Number(root.overview["daysOfData"]))
                }
            }
        }
    }
}
