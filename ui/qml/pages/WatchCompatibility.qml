import QtQuick 2.6
import Sailfish.Silica 1.0

Column {
    id: root

    property var app
    property var currentWatch
    readonly property var platforms: app ? app.compatibility : ({})
    visible: Object.keys(platforms).length > 0

    // In the same preference order as libpebble3 WatchType.getCompatibleAppVariants().
    function verdict(watch) {
        if (!watch || !watch.hardwarePlatform)
            return qsTr("Compatibility unknown")
        var platform = watch.hardwarePlatform
        var variants = { basalt: ["basalt", "aplite"], diorite: ["diorite", "aplite"],
                         emery: ["emery", "basalt", "diorite", "aplite"],
                         flint: ["flint", "diorite", "aplite"], gabbro: ["gabbro", "chalk"] }
        var candidates = variants[platform] || [platform]
        for (var i = 0; i < candidates.length; i++) {
            var required = root.platforms[candidates[i]]
            if (required === undefined) continue
            var installed = /^v?(\d+)/.exec(watch.softwareVersion)
            if (required > 1 && (!installed || Number(installed[1]) < required))
                return qsTr("Requires firmware %1 or later").arg(required)
            return qsTr("Compatible")
        }
        return qsTr("Incompatible")
    }

    Repeater {
        model: pebbles.count + (root.currentWatch ? 1 : 0)
        delegate: DetailItem {
            property var watch: root.currentWatch && index === 0 ? root.currentWatch
                                : pebbles.get(index - (root.currentWatch ? 1 : 0))
            visible: !!watch && (index === 0 || watch !== root.currentWatch)
            label: watch ? watch.name : ""
            value: root.verdict(watch)
        }
    }
}
