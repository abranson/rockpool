import QtQuick 2.2
import Sailfish.Silica 1.0
import QtWebKit 3.0
//import QtWebKit.experimental 1.0

Page {
    id: settings

    property string uuid;
    property string url;
    property var pebble;
    SilicaWebView {
        id: webview
        anchors.fill: parent

        url: settings.url
        /*header: Label {
            text: qsTr("App Settings")
            width: parent.width
            horizontalAlignment: Text.AlignRight
            color: Theme.highlightColor
        }*/
        VerticalScrollDecorator {
            flickable: parent
        }

        onNavigationRequested: {
            //The pebblejs:// protocol is handeled by the urihandler, as it appears we can't intercept it
            var url = request.url.toString();
            console.log(url, url.substring(0, 16));
            if (url.substring(0, 16) == 'pebblejs://close') {
                pebble.configurationClosed(settings.uuid, url);
                request.action = WebView.IgnoreRequest;
                pageStack.pop();
            }
        }
        experimental.preferences.webGLEnabled: true
        //experimental.preferences.webAudioEnabled: true
        //experimental.preferredMinimumContentsWidth: 980
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

    }
}
