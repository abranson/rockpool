import QtQuick 2.2
import Sailfish.Silica 1.0
import QtWebKit 3.0

Page {
    id: settings

    property string uuid;
    property string url;
    property var pebble;


    SilicaWebView {
        id: webview
        anchors.fill: parent

        url: settings.url
        header: PageHeader {
            title: qsTr("App Settings")
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
    }
}
