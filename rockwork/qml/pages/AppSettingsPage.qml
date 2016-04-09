import QtQuick 2.2
import Sailfish.Silica 1.0
import Qt5Mozilla 1.0

Page {
    id: appSettings

    property string uuid;
    property string url;
    property var pebble;
    allowedOrientations: Orientation.All
    backNavigation: webview.contentRect.x===0
    Label {
        id: header
        width: parent.width
        anchors.top: parent.top
        text: url
        height: Theme.itemSizeSmall
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeTiny
        wrapMode: Text.WordWrap
    }

    QmlMozView {
        id: webview
        anchors { top: header.bottom;bottom:parent.bottom }
        width: parent.width
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
            case "embed:alert": {
                dialogLoader.alert(data);
                break;
            }
            case "embed:confirm": {
                dialogLoader.confirm("confirm",data);
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
    // Alert/Prompt dialog
    Loader {
        id: dialogLoader
        focus: true
        anchors.fill: parent
        property bool singleChoice: true
        property string dlgTitle: qsTr("Alert")
        property string dlgText: qsTr("Something going wrong")
        property string acceptStr: qsTr("Accept")
        property string cancelStr: qsTr("Cancel")
        property var dlgData: {}
        property string dlgMsg: ""

        function alert(data) {
            sourceComponent = cmpDialog;
            singleChoice=true;
            dlgTitle=data.title;
            dlgText=data.text;
            dlgData=data;
            dlgMsg="alert";
        }
        function confirm(msg,data) {
            sourceComponent = cmpDialog;
            singleChoice = false;
            dlgTitle = data.title;
            dlgText = data.text;
            dlgMsg = msg;
            dlgData = data;
        }

        function accept() {
            sourceComponent = null;
            if(dlgMsg) {
                console.log("Sending accept for",dlgMsg+"response")
                webview.sendAsyncMessage(dlgMsg+"response", {"accepted": true,"winid":dlgData.winid})
                dlgMsg = "";
            }
        }
        function reject() {
            sourceComponent = null;
            if(dlgMsg) {
                console.log("Sending reject for",dlgMsg+"response")
                webview.sendAsyncMessage(dlgMsg+"response", {"accepted": false, "winid":dlgData.winid})
                dlgMsg = "";
            }
        }
    }
    Component {
        id: cmpDialog
        Rectangle {
            color: Theme.rgba(Theme.highlightDimmerColor, 0.75)
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingSmall
                Button {
                    text: dialogLoader.singleChoice ? dialogLoader.acceptStr : dialogLoader.cancelStr
                    onClicked: dialogLoader.reject()
                }
                Button {
                    text: dialogLoader.acceptStr
                    onClicked: dialogLoader.accept()
                    enabled: !dialogLoader.singleChoice
                    visible: enabled
                }
            }
            SilicaFlickable {
                width: parent.width
                height:parent.height-Theme.itemSizeSmall
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                Column {
                    width: parent.width
                    spacing: Theme.paddingSmall
                    Label {
                        horizontalAlignment: Text.AlignRight
                        color: Theme.highlightColor
                        font.pixelSize: Theme.fontSizeLarge
                        wrapMode: Text.WordWrap
                        text: dialogLoader.dlgTitle
                        width: parent.width
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: dialogLoader.dlgText
                    }
                }
            }
        }
    }
    // Select control
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
