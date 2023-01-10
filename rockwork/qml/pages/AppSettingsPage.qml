import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.WebView 1.0
import Sailfish.WebView.Popups 1.0
import Sailfish.WebEngine 1.0

Page {
    id: appSettings

    property string uuid;
    property string url;
    property var pebble;
    allowedOrientations: Orientation.All
    Label {
        id: header
        width: parent.width
        anchors.top: parent.top
        // The generated data URL from pebble-clay does not help the user and make it seem like a bug.
        text: url.substring(0, 5) == "data:" ? "" : url
        height: Theme.itemSizeMedium
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeTiny
        wrapMode: Text.WordWrap
    }

    WebViewFlickable {
        id: webview
        anchors { top: header.bottom; bottom:parent.bottom; left:parent.left; }
        width: parent.width
        webView {
            visible: true
            clip: false
            focus: true
            active: true
    
            url: appSettings.url
            onViewInitialized: {
                WebEngine.addComponentManifest("/usr/share/rockpool/jsm/RockpoolJSComponents.manifest");
                webView.addMessageListener("embed:pebble");
            }
            onRecvAsyncMessage: {
                console.log("Message",message);
                switch(message) {
                case "embed:pebble": {
                    if(data.action === "close") {
                        pebble.configurationClosed(appSettings.uuid, data.uri);
                        pageStack.pop();
                    } else if(data.action === "login") {
                        var params = data.query.split("&");
                        for(var i = 0;i<params.length; i++) {
                            if(params[i].substr(0,13) === "access_token=") {
                                var kv = params[i].split("=");
                                console.log("Found token",kv[1]);
                                pebble.setOAuthToken(kv[1]);
                                break;
                            }
                        }
                        pageStack.pop();
                    }
                    break;
                }
                default:
                    console.log("Data",JSON.stringify(data));
                }
            }
        }
    }
}
