import QtQuick 2.2
import Sailfish.Silica 1.0

Page {
    id: root
    property var list: []
    property var pebble: null
    property string title: ""
    property string source: ""
    property bool changed: false

    SilicaListView {
        id: rspList
        anchors.fill: parent
        header: PageHeader {
            title: root.title
            description: (editor.open && editor.index<0)? qsTr("New item will be inserted before current selection") : qsTr("Edit and order quick-response messages")
        }
        PullDownMenu {
            MenuItem {
                text: qsTr("Add New")
                onClicked: {
                    editor.index = -1;
                    editor.open = true;
                    editor.text = "";
                }
            }
            MenuItem {
                text: qsTr("Save changes")
                onClicked: {
                    var cans = {};
                    cans[source] = list;
                    pebble.cannedResponses=cans;
                    pageStack.pop();
                }
                visible: changed
            }
        }

        model: list
        delegate: ListItem {
            id: li
            contentHeight: Theme.itemSizeSmall
            Label {
                anchors.verticalCenter: parent.verticalCenter
                color: li.highlighted ? Theme.highlightColor : Theme.primaryColor
                wrapMode: Text.WordWrap
                text: modelData
            }
            onClicked: showMenu()
            menu: ContextMenu {
                closeOnActivation: true
                MenuItem {
                    text: qsTr("Move Up")
                    visible: index > 0
                    onClicked: root.move(index,-1)
                }
                MenuItem {
                    text: qsTr("Edit")
                    onClicked: {
                        editor.index = index;
                        editor.text = modelData;
                        editor.open = true;
                    }
                }
                MenuItem {
                    text: qsTr("Delete")
                    onClicked: {
                        li.remorseAction(qsTr("Really Delete?"), function () {
                            root.deleteItem(index)
                        })
                    }
                }
                MenuItem {
                    text: qsTr("Move Down")
                    visible: index < (list.length-1)
                    onClicked: root.move(index,1)
                }
            }
        }
    }
    DockedTextField {
        id: editor
        property int index: -1
        icon: "image://theme/icon-m-edit"
        hint: qsTr("Pre-defined response message")
        onSubmit: {
            if(index >= 0 && list[index] !== text) {
                root.list[index] = text;
                rspList.model = root.list;
                root.changed = true;
            } else if(index < 0) {
                root.list.splice(rspList.currentIndex,0,text)
                rspList.model = root.list;
                root.changed = true;
            }
        }
    }
    function deleteItem(i) {
        list.splice(i,1);
        rspList.model = list;
        changed = true;
    }
    function move(i,d) {
        var item = list[i];
        list.splice(i,1);
        list.splice(i+d,0,item);
        rspList.model = list;
        changed = true;
    }
}
