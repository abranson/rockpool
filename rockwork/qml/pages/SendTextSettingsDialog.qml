import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.Contacts 1.0
import Sailfish.Telephony 1.0
import org.nemomobile.contacts 1.0

Dialog {
    id: root

    property var pebble: null
    property var contacts: {}
    property var newCtx: {}
    property string msgKey: "com.pebble.sendText"
    property string modem: "/ril_0"
    property string telePhone: "/org/freedesktop/Telepathy/Account/ring/tel"
    canAccept: false

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height
        Column {
            id: content
            width: parent.width

            DialogHeader {
                title: qsTr("Messaging Settings")
                defaultAcceptText: qsTr("OK")
                defaultCancelText: qsTr("Cancel")
            }

            SectionHeader {
                text: qsTr("Contacts")
            }
            ListModel {
                id: oldModel
                property var src: {}
            }

            SilicaListView {
                id: oldContacts
                model: oldModel
                width: parent.width
                height: contentItem.childrenRect.height
                delegate: ListItem {
                    id: liCtx
                    width: oldContacts.width
                    contentHeight: Theme.itemSizeSmall
                    height: contentHeight
                    Row {
                        width: parent.width
                        height: parent.contentHeight
                        Label {
                            id: lblType
                            text: model.type
                            width: Theme.iconSizeMedium+Theme.paddingSmall
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Column {
                            width: parent.width - lblType.width - btnDel.width
                            Label {
                                id: lblName
                                text: model.name
                                anchors.left: parent.left
                            }
                            Label {
                                id: lblNum
                                text: model.uri
                                anchors.right: parent.right
                                font.pixelSize: Theme.fontSizeTiny
                            }
                        }
                        IconButton {
                            id: btnDel
                            //anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            icon.source: "image://theme/icon-m-remove"
                            onClicked: liCtx.remorseAction(qsTr("Really Delete?"),function(){
                                if(oldModel.src[model.name].length>1) {
                                    oldModel.src[model.name].splice(oldModel.src[model.name].indexOf(model.num),1);
                                } else {
                                    delete oldModel.src[model.name];
                                }
                                oldModel.remove(model.index);
                                root.canAccept=true;
                            })
                        }
                    }
                    ListView.onRemove: animateRemoval(liCtx)
                    Separator {
                        width: parent.width
                        color: Theme.highlightColor
                        height: 1
                        anchors.bottom: parent.bottom
                    }
                }
            }

            RecipientField {
                id: newContacts
                width: parent.width
                //multipleAllowed: false
                requiredProperty: (PeopleModel.AccountUriRequired | PeopleModel.PhoneNumberRequired )
                showLabel: false
                onLastFieldExited: {
                    console.log("Last",selectedContacts)
                }
                onSelectionChanged: {
                    console.log("Selection",focus)
                    updateContacts();
                }
                function updateContacts() {
                    root.newCtx = {};
                    for(var i=0;i<selectedContacts.count;i++) {
                        var item = selectedContacts.get(i);
                        if(!item || !item.person) break;
                        var name = item.person.displayLabel;
                        var value = ((item.propertyType==="accountUri")?item.property['path']+":"+item.property['uri']:root.telePhone+root.modem+":"+item.property['number']);
                        if(!(name in root.newCtx)) {
                            root.newCtx[name] = [];
                        }
                        root.newCtx[name].push(value);
                        console.log("Contact",item.person.id,name,item.propertyType,newCtx[name]);
                    }
                    console.log("Updated",typeof root.newCtx,Object.keys(root.newCtx).length);
                    root.canAccept = (root.canAccept || (typeof root.newCtx === 'object' && Object.keys(root.newCtx).length > 0));
                }
            }

            SectionHeader {
                text: qsTr("Pick SIM for new contacts")
                visible: simSelector.visible
            }
            SimSelector {
                id: simSelector
                visible: modemManager.modemSimModel.count>1
                updateSelectedSim: false
                onSimSelected: {
                    console.log("SIM",modemPath);
                    root.modem = modemPath;
                }
            }

            SectionHeader {
                text: qsTr("Messages")
            }
            Button {
                text: qsTr("Edit Messages")
                width: parent.width
                onClicked: {
                    pageStack.push(Qt.resolvedUrl("ResponsesPage.qml"), {
                                       pebble: pebble,
                                       source: msgKey,
                                       title: qsTr("Send Text Messages"),
                                       list: pebble.getCannedResponses([msgKey])[msgKey] || []
                                   });
                }

            }
        }
    }

    Component.onCompleted: {
        oldModel.src = pebble.getCannedContacts([]);
        root.modem = simSelector.activeModem;
        for(var i in oldModel.src) {
            var can = oldModel.src[i];
            console.log("Contact",i,can);
            for(var j=0;j<can.length;j++) {
                var type = "SIM1";
                var uri = can[j];
                var el = uri.split(':');
                console.log("Number",type,uri,el);
                if(el.length>1) {
                    uri=el[1];
                    var pe = el[0].split("/");
                    if(pe.length > 7 && pe[7].substr(0,4) === "ril_") {
                        type="SIM"+(parseInt(pe[7].split('_')[1])+1);
                    } else {
                        type="IM";
                    }
                }
                oldModel.append({"type":type,"name":i,"uri":uri,"num":can[j]});
            }
        }
    }

    onDone: {
        if(result === DialogResult.Accepted) {
            contacts = oldModel.src;
            for(var name in newCtx) {
                if(name in contacts) {
                    contacts[name] = contacts[name].concat(newCtx[name]);
                } else {
                    contacts[name] = newCtx[name];
                }
                console.log("Adding",name,contacts[name]);
            }
        }
    }
}
