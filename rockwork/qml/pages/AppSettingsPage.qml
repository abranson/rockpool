import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.WebView 1.0

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

    RawWebView {
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
                            "embed:permissions",
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
            case "embed:permissions": {
                dialogLoader.permissions(data);
                break;
            }

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

            case "embed:prompt": {
                dialogLoader.prompt("prompt",data);
                break;
            }

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
        property bool promptValue: false
        property string dlgTitle: qsTr("Alert")
        property string dlgText: qsTr("Something going wrong")
        property string acceptStr: qsTr("Accept")
        property string cancelStr: qsTr("Cancel")
        property var dlgData: {}
        property string dlgMsg: ""

        function alert(data) {
            singleChoice=true;
            promptValue = false;
            dlgTitle=data.title;
            dlgText=data.text;
            dlgData=data;
            sourceComponent = cmpDialog;
            dlgMsg="alert";
        }
        function confirm(msg,data) {
            singleChoice = false;
            promptValue = false;
            dlgTitle = data.title;
            dlgText = data.text;
            dlgData = data;
            sourceComponent = cmpDialog;
            dlgMsg = msg;
        }

        function prompt(msg,data) {
            singleChoice = false;
            promptValue = true;
            dlgTitle = data.title;
            dlgText = data.text;
            dlgData = data;
            sourceComponent = cmpDialog;
            dlgMsg = msg;
        }

        function permissions(data) {
            console.log("Data",JSON.stringify(data));
            promptValue = false;
            singleChoice = false;
            dlgTitle = data.title;
            dlgText = qsTr("Host")+" "+data.host+" "+qsTr("requests permission for")+" "+data.title;
            dlgData = data;
            dlgData["checkmsg"] = qsTr("Store permission permanently and don't ask again");
            dlgData["checkval"] = false;
            sourceComponent = cmpDialog;
        }

        function accept(promptval,checkval) {
            sourceComponent = null;
            if(dlgMsg) {
                console.log("Sending accept for",dlgMsg+"response")
                var ret = {"accepted": true,"winid":dlgData.winid};
                if(dlgData.checkmsg)
                    ret["checkval"] = checkval
                if(promptval)
                    ret["promptvalue"] = promptval;
                webview.sendAsyncMessage(dlgMsg+"response", ret)
                dlgMsg = "";
            } else if("host" in dlgData) {
                console.log("Allowing permisssion",dlgData.title,"for",dlgData.host);
                webview.sendAsyncMessage("embedui:premissions", { "allow": true, "checkedDontAsk": checkval, "id": dlgData.id });
                dlgData = {};
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
                    onClicked: dialogLoader.accept(cmpDlgPrompt.enabled ? cmpDlgPrompt.text : "", acptBox.checked)
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
                    TextField {
                        id: cmpDlgPrompt
                        width: parent.width
                        text: dialogLoader.promptValue ? dialogLoader.dlgData.defaultValue : ""
                        enabled: dialogLoader.promptValue
                        visible: enabled
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: "checkmsg" in dialogLoader.dlgData
                        text: visible ? dialogLoader.dlgData.checkmsg : ""
                    }
                    TextSwitch {
                        id: acptBox
                        width: parent.width
                        text: qsTr("Accept")
                        visible: "checkmsg" in dialogLoader.dlgData
                        checked: visible ? dialogLoader.dlgData.checkval : false
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
