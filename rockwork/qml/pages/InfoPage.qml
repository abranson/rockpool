import QtQuick 2.2
import Sailfish.Silica 1.0

Page {

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: contentColumn.height + Theme.paddingSmall

        Column {
            id: contentColumn
            width: parent.width
            height: childrenRect.height
            spacing: Theme.paddingSmall
            PageHeader {
                title: "About RockPool"
            }

            Row {
                width: parent.width
                spacing: Theme.paddingSmall
                Image {
                    source: "qrc:///rockpool.png"
                    height: Theme.iconSizeLarge
                    width: height
                }

                Label {
                    text: qsTr("Version %1").arg(version)
                    font.pixelSize: Theme.fontSizeLarge
                }
            }

            Separator {
                width: parent.width
                height: Theme.paddingSmall
                color: Theme.secondaryHighlightColor
            }

            Label {
                width: parent.width
                text: qsTr("Legal")
                font.bold: true
            }

            Label {
                width: parent.width
                font.pixelSize: Theme.fontSizeSmall
                text: "This program is free software: you can redistribute it and/or modify "
                      + "it under the terms of the GNU General Public License as published "
                      + "by the Free Software Foundation, version 3 of the License.<br>"
                      + "This program is distributed in the hope that it will be useful, "
                      + "but WITHOUT ANY WARRANTY; without even the implied warranty of "
                      + "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
                      + "GNU General Public License for more details.<br>"
                      + "You should have received a copy of the GNU General Public License "
                      + "along with this program.  If not, see <a href=\"http://www.gnu.org/"
                      + "licenses/\">http://www.gnu.org/licenses</a>."
                wrapMode: Text.WordWrap
            }

            Separator {
                width: parent.width
                height: Theme.paddingSmall
                color: Theme.secondaryHighlightColor
            }
            Label {
                width: parent.width
                text: qsTr("This application is neither affiliated with nor endorsed by Pebble Technology Corp.")
                wrapMode: Text.WordWrap
            }
            Separator {
                width: parent.width
                height: Theme.paddingSmall
                color: Theme.secondaryHighlightColor
            }
            Label {
                width: parent.width
                text: qsTr("Pebble is a trademark of Pebble Technology Corp.")
                wrapMode: Text.WordWrap
            }
        }
    }
}
