import QtQuick 2.4
import QtQuick.Controls 1.3
import PebbleTest 1.0

Row {
    Column {
        spacing: 10
        Button {
            text: "Generic Notification"
            onClicked: {
                handler.sendNotification(0, "Bro Coly", "TestSubject", "TestText")
            }
        }
        Button {
            text: "Email Notification"
            onClicked: {
                handler.sendNotification(1, "Tom Ato", "TestSubject", "TestText")
            }
        }
        Button {
            text: "SMS with no subject"
            onClicked: {
                handler.sendNotification(2, "Tom Ato", "", "TestText")
            }
        }

        Button {
            text: "Facebook Notification"
            onClicked: {
                handler.sendNotification(3, "Cole Raby", "TestSubject", "TestText")
            }
        }
        Button {
            text: "Twitter Notification"
            onClicked: {
                handler.sendNotification(4, "Horse Reddish", "TestSubject", "TestText")
            }
        }
        Button {
            text: "Telegram Notification"
            onClicked: {
                handler.sendNotification(5, "Horse Reddish", "TestSubject", "TestText")
            }
        }
        Button {
            text: "WhatsApp Notification"
            onClicked: {
                handler.sendNotification(6, "Horse Reddish", "TestSubject", "TestText")
            }
        }
        Button {
            text: "Hangout Notification"
            onClicked: {
                handler.sendNotification(7, "Horse Reddish", "TestSubject", "TestText")
            }
        }

    }

    Column {
        spacing: 10
        Button {
            text: "Fake incoming phone call"
            onClicked: {
                handler.fakeIncomingCall(1, "123456789", "TestCaller")
            }
        }
        Button {
            text: "pick up incoming phone call"
            onClicked: {
                handler.callStarted(1)
            }
        }
        Button {
            text: "hang up incoming phone call"
            onClicked: {
                handler.endCall(1, false)
            }
        }
        Button {
            text: "miss incoming phone call"
            onClicked: {
                handler.endCall(1, true)
            }
        }
    }
}
