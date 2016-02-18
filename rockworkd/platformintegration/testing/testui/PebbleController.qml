import QtQuick 2.4
import QtQuick.Controls 1.3
import PebbleTest 1.0

Column {
    spacing: 10
    Label {
        text: pebble.name
        width: parent.width
    }

    Button {
        text: "Insert Timeline Pin"
        onClicked: {
           pebble.insertTimelinePin();
        }
    }
    Button {
        text: "Create Reminder"
        onClicked: {
           pebble.insertReminder();
        }
    }
    Button {
        text: "Clear Timeline"
        onClicked: {
           pebble.clearTimeline();
        }
    }
    Button {
        text: "take screenshot"
        onClicked: {
            pebble.requestScreenshot();
        }
    }

    Button {
        text: "dump logs"
        onClicked: {
            pebble.dumpLogs();
        }
    }
}

