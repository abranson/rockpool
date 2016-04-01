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
        anchors.fill: parent
        visible: true
        clip: false
        focus: true
        active: true

        url: appSettings.url
        onViewInitialized: {
            webview.loadFrameScript("chrome://embedlite/content/SelectAsyncHelper.js");
            webview.addMessageListeners(
                        [
                            "embed:selectasync",
                            "embed:select",
                            "embed:alert",
                            "embed:confirm",
                            "embed:filepicker",
                            "embed:prompt",
                            "embed:auth",
                            "embed:pebble"]);
            console.log("Ready, Steady, Go!");
        }
        onRecvAsyncMessage: {
            console.log("Message",message);
            switch(message) {
            case "embed:selectasync": {
                selectorLoader.show(data);
                break;
            }
            case "embed:pebble": {
                if(data.action === "close") {
                    pebble.configurationClosed(appSettings.uuid, data.uri);
                    pageStack.pop();
                }
                break;
            }
            default:
                console.log("Data",JSON.stringify(data));
            }
        }
        onLoadingChanged: {busyId.running = webview.loading}
    }
    BusyIndicator {
        id: busyId
        anchors.centerIn: parent
        running: true
        visible: running
        size: BusyIndicatorSize.Large
    }
    Loader {
        id: selectorLoader
        focus: true
        anchors.fill: parent
        ListModel {
            id: selectorModel
            property bool multi: false
            function reject() {
                webview.sendAsyncMessage("embedui:selectresponse", {"result": -1})
                selectorLoader.sourceComponent=null;
            }
            function accept() {
                var res = [];
                for(var i=0;i<count;i++) {
                    var item=get(i);
                    res.push({"selected":item.selected,"index":item.index});
                }
                //console.log("Responding with",JSON.stringify(res));
                webview.sendAsyncMessage("embedui:selectresponse", {"result": res});
                selectorLoader.sourceComponent=null;
            }
        }

        function show(data) {
            selectorModel.multi=data.multiple;
            sourceComponent=selBox;
            selectorModel.clear();
            for(var i=0;i<data.options.length;i++) {
                selectorModel.append(data.options[i]);
            }
        }
    }
    Component {
        id: selBox
        Rectangle {
            //property QtObject selectorModel: model
            //anchors.fill: webview
            color: Theme.rgba(Theme.highlightDimmerColor, 0.75)
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingSmall
                Button {
                    text: qsTr("Cancel")
                    onClicked: selectorModel.reject()
                }
                Button {
                    text: qsTr("Select")
                    onClicked: selectorModel.accept()
                    enabled: selectorModel.multi
                    visible: enabled
                }
            }

            SilicaListView {
                width: parent.width
                height:parent.height-Theme.itemSizeSmall
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                model: selectorModel
                delegate: ListItem {
                    enabled: !model.disable
                    contentHeight: Theme.itemSizeSmall
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: model.label
                        color: !parent.enabled ? Theme.secondaryHighlightColor : Theme.primaryColor
                    }
                    highlighted: model.selected
                    onClicked: {
                        if(selectorModel.multi)
                            selectorModel.setProperty(index,"selected",!model.selected);
                        else {
                            for(var i=0;i<selectorModel.count;i++)
                                selectorModel.setProperty(i,"selected",false);
                            selectorModel.setProperty(index,"selected",true);
                            selectorModel.accept();
                        }
                    }
                }
            }
        }
    }
}
