import QtQuick 2.4
import QtQuick.Layouts 1.1
import Ubuntu.Components 1.3
import Ubuntu.Components.ListItems 1.3

Page {
    title: "About RockWork"

    Flickable {
        anchors.fill: parent
        contentHeight: contentColumn.height + units.gu(4)

        ColumnLayout {
            id: contentColumn
            anchors { left: parent.left; top: parent.top; right: parent.right; margins: units.gu(2) }
            spacing: units.gu(2)

            RowLayout {
                Layout.fillWidth: true
                spacing: units.gu(2)
                UbuntuShape {
                    source: Image {
                        anchors.fill: parent
                        source: "artwork/rockwork.svg"
                    }
                    height: units.gu(6)
                    width: height
                }

                Label {
                    text: i18n.tr("Version %1").arg(version)
                    Layout.fillWidth: true
                    fontSize: "large"
                }
            }

            ThinDivider {}

            Label {
                text: i18n.tr("Legal")
                Layout.fillWidth: true
                font.bold: true
            }

            Label {
                text: "This program is free software: you can redistribute it and/or modify" +
                      "it under the terms of the GNU General Public License as published by" +
                      "the Free Software Foundation, version 3 of the License.<br>" +

                      "This program is distributed in the hope that it will be useful," +
                      "but WITHOUT ANY WARRANTY; without even the implied warranty of" +
                      "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the" +
                      "GNU General Public License for more details.<br>" +

                      "You should have received a copy of the GNU General Public License" +
                      "along with this program.  If not, see <http://www.gnu.org/licenses/>."
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Label {
                text: i18n.tr("This application is neither affiliated with nor endorsed by Pebble Technology Corp.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Label {
                text: i18n.tr("Pebble is a trademark of Pebble Technology Corp.")
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
    }
}

