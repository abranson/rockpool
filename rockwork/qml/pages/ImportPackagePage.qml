import QtQuick 2.2
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import QtDocGallery 5.0

PickerPage {
    id: root
    title: qsTr("Import watchapp or watchface")

    property var pebble: null
    property string rootPath: "/home/nemo"
    property string wildcard: "*.pbw"

    SilicaListView {
        id: listView
        anchors.fill: parent
        header: PageHeader {
            width: parent.width
            title: root.title
        }
        model: docModel.model
        delegate: DocumentItem {
            id: docItem
            leftMargin: Theme.horizontalPageMargin
            baseName: Theme.highlightText(docModel.baseName(model.fileName), docModel.filter, Theme.highlightColor)
            extension: Theme.highlightText(docModel.extension(model.fileName), docModel.filter, Theme.highlightColor)

            ListView.onAdd: AddAnimation { target: docItem; duration: _animationDuration }
            ListView.onRemove: RemoveAnimation { target: docItem; duration: _animationDuration }
            onClicked: remorseAction(qsTr("Import File?"),function(){
                console.log("Sideloading file",model.url);
                root.pebble.sideloadApp(model.url);
                pageStack.pop();
            });
        }
    }
    DocumentModel {
        id: docModel
        contentFilter: GalleryFilterIntersection {
            filters: [
                GalleryStartsWithFilter {
                    property: "filePath"
                    value: root.rootPath
                },GalleryWildcardFilter {
                    property: "fileName"
                    value: root.wildcard
                }]
        }
    }
}

