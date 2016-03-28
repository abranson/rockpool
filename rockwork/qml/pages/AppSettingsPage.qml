import QtQuick 2.2
import Sailfish.Silica 1.0
import Qt5Mozilla 1.0

Page {
    id: appSettings

    property string uuid;
    property string url;
    property var pebble;
    QmlMozView {
        id: webview
        //objectName: "webview"
        anchors.fill: parent
        parentid: 0
        visible: true
        clip: false
        focus: true
        active: true

        url: appSettings.url
        /*
        onNavigationRequested: {
            //The pebblejs:// protocol is handeled by the urihandler, as it appears we can't intercept it
            var url = request.url.toString();
            console.log(url, url.substring(0, 16));
            if (url.substring(0, 16) == 'pebblejs://close') {
                pebble.configurationClosed(appSettings.uuid, url);
                request.action = WebView.IgnoreRequest;
                pageStack.pop();
            }
        }
        //experimental.deviceHeight:appSettings.height
        experimental.transparentBackground: true
        experimental.itemSelector: Component {
            id: selBox
            Rectangle {
                property QtObject selectorModel: model
                anchors.fill: webview
                color: Theme.rgba(Theme.highlightDimmerColor, 0.75)
                Button {
                    text: qsTr("Cancel")
                    onClicked: selectorModel.reject()
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                SilicaListView {
                    width: parent.width
                    height: parent.height-Theme.itemSizeSmall
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    model: selectorModel.items
                    delegate: ListItem {
                        enabled: model.enabled
                        contentHeight: Theme.itemSizeSmall
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: model.text
                            color: model.enabled ? Theme.primaryColor : Theme.secondaryHighlightColor
                        }
                        onClicked: selectorModel.accept(index);
                    }
                }
            }
        }

        //experimental.alertDialog: AlertDialog { }
        //experimental.confirmDialog: ConfirmDialog { }
        //experimental.promptDialog: PromptDialog { }
        experimental.colorChooser: Component {
            id: colBox
            ColorPicker {
                anchors.fill: webview
                onColorChanged: { console.log("The color is",color); model.reject() }
            }
        }
        */
        onLoadingChanged: {busyId.running = webview.loading}
    }
    BusyIndicator {
        id: busyId
        anchors.centerIn: parent
        running: true
        visible: running
        size: BusyIndicatorSize.Large
    }
}
