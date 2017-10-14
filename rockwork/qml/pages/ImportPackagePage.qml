import QtQuick 2.2
import Sailfish.Silica 1.0
import RockPool 1.0
import Nemo.FileManager 1.0
import Sailfish.FileManager 1.0


Page {
    id: root

    property var pebble: null
    allowedOrientations: Orientation.Portrait

    property alias path: fileModel.path
    property string homePath: '/home/nemo'
    property string title
    property bool showFormat

    signal formatClicked

    property var callback

    backNavigation: !FileEngine.busy

    FileModel {
        id: fileModel

        path: homePath
        active: root.status === PageStatus.Active
        directorySort: FileModel.SortDirectoriesBeforeFiles
        caseSensitivity: Qt.CaseInsensitive
        onError: {
            console.log("###", fileName, error)
        }
    }
    SilicaListView {
        id: fileList

        opacity: FileEngine.busy ? 0.6 : 1.0
        Behavior on opacity { FadeAnimator {} }

        anchors.fill: parent
        model: fileModel

        header: PageHeader {
            title: path == homePath && root.title.length > 0 ? root.title
                                                             : root.path.split("/").pop()
        }

        delegate: ListItem {
            id: fileItem

            width: ListView.view.width
            contentHeight: Theme.itemSizeMedium
            Row {
                anchors.fill: parent
                spacing: Theme.paddingLarge
                Rectangle {
                    width: height
                    height: parent.height
                    gradient: Gradient {
                        GradientStop { position: 0.0; color: Theme.rgba(Theme.primaryColor, 0.1) }
                        GradientStop { position: 1.0; color: "transparent" }
                    }

                    Image {
                        anchors.centerIn: parent
                        source: {
                            var iconSource
                            if (model.isDir) {
                                iconSource = "image://theme/icon-m-file-folder"
                            } else {
                                var iconType = "other"
                                switch (model.mimeType) {
                                case "application/vnd.android.package-archive":
                                    iconType = "apk"
                                    break
                                case "application/x-rpm":
                                    iconType = "rpm"
                                    break
                                case "text/vcard":
                                    iconType = "vcard"
                                    break
                                case "text/plain":
                                case "text/x-vnote":
                                    iconType = "note"
                                    break
                                case "application/pdf":
                                    iconType = "pdf"
                                    break
                                case "application/vnd.oasis.opendocument.spreadsheet":
                                case "application/x-kspread":
                                case "application/vnd.ms-excel":
                                case "text/csv":
                                case "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet":
                                case "application/vnd.openxmlformats-officedocument.spreadsheetml.template":
                                    iconType = "spreadsheet"
                                    break
                                case "application/vnd.oasis.opendocument.presentation":
                                case "application/vnd.oasis.opendocument.presentation-template":
                                case "application/x-kpresenter":
                                case "application/vnd.ms-powerpoint":
                                case "application/vnd.openxmlformats-officedocument.presentationml.presentation":
                                case "application/vnd.openxmlformats-officedocument.presentationml.template":
                                    iconType = "presentation"
                                    break
                                case "application/vnd.oasis.opendocument.text-master":
                                case "application/vnd.oasis.opendocument.text":
                                case "application/vnd.oasis.opendocument.text-template":
                                case "application/msword":
                                case "application/rtf":
                                case "application/x-mswrite":
                                case "application/vnd.openxmlformats-officedocument.wordprocessingml.document":
                                case "application/vnd.openxmlformats-officedocument.wordprocessingml.template":
                                case "application/vnd.ms-works":
                                    iconType = "formatted"
                                    break
                                default:
                                    if (mimeType.indexOf("audio/") == 0) {
                                        iconType = "audio"
                                    } else if (mimeType.indexOf("image/") == 0) {
                                        iconType = "image"
                                    } else if (mimeType.indexOf("video/") == 0) {
                                        iconType = "video"
                                    }
                                }
                                iconSource = "image://theme/icon-m-file-" + iconType
                            }
                            return iconSource + (highlighted ? "?" + Theme.highlightColor : "")
                        }
                    }
                }
                Column {
                    width: parent.width - parent.height - parent.spacing - Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: -Theme.paddingSmall
                    Label {
                        text: model.fileName
                        width: parent.width
                        font.pixelSize: Theme.fontSizeLarge
                        truncationMode: TruncationMode.Fade
                        color: highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        property string dateString: Format.formatDate(model.modified, Formatter.DateLong)
                        text: model.isDir ? dateString
                                            //: Shows size and modification date, e.g. "15.5MB, 02/03/2016"
                                            //% "%1, %2"
                                          : qsTrId("filemanager-la-file_details").arg(Format.formatFileSize(model.size)).arg(dateString)
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    }
                }
            }

            onClicked: {
                if (model.isDir) {
                    pageStack.push(Qt.resolvedUrl("ImportPackagePage.qml"),
                                   { path: fileModel.appendPath(model.fileName), homePath: root.homePath, callback: root.callback, pebble: root.pebble })
                } else {
                    var filePath = Qt.resolvedUrl(fileModel.path + "/" + model.fileName)
                    remorseAction(qsTr("Sideloading file"),function(){
                        console.log("Sideloading file", filePath);
                        root.pebble.sideloadApp(filePath);
                    });
                }
            }
        }
        ViewPlaceholder {
            enabled: fileModel.count === 0
            //% "No files"
            text: qsTrId("filemanager-la-no_files")
        }
        VerticalScrollDecorator {}
    }
}

