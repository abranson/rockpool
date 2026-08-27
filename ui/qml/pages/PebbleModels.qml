import QtQuick 2.2

ListModel {
    id: modelModel

    function getOrFallback(modelId) {
        return modelId >= 0 && modelId < count ? get(modelId) : get(0)
    }

    ListElement { modelName: "Unknown Pebble"; image: 'qrc:///artwork/tintin-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 } // Fallback for Unknown
    ListElement { modelName: "Pebble Classic - Black"; image: 'qrc:///artwork/tintin-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - White"; image: 'qrc:///artwork/tintin-white.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - Red"; image: 'qrc:///artwork/tintin-red.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - Orange"; image: 'qrc:///artwork/tintin-orange.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - Pink"; image: 'qrc:///artwork/tintin-pink.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Steel - Silver"; image: 'qrc:///artwork/bianca-silver.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Steel - Gunmetal"; image: 'qrc:///artwork/bianca-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - Fly Blue"; image: 'qrc:///artwork/tintin-blue.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - Fresh Green"; image: 'qrc:///artwork/tintin-green.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Classic - Hot Pink"; image: 'qrc:///artwork/tintin-pink.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time - White"; image: 'qrc:///artwork/snowy-white.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time - Black"; image: 'qrc:///artwork/snowy-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time - Red"; image: 'qrc:///artwork/snowy-red.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time Steel - Silver"; image: 'qrc:///artwork/bobby-silver.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time Steel - Gunmetal"; image: 'qrc:///artwork/bobby-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time Steel - Gold"; image: 'qrc:///artwork/bobby-gold.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time Round - Silver 14 mm"; image: 'qrc:///artwork/spalding-14mm-silver.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble Time Round - Black 14 mm"; image: 'qrc:///artwork/spalding-14mm-black.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble Time Round - Silver 20 mm"; image: 'qrc:///artwork/spalding-20mm-silver.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble Time Round - Black 20 mm"; image: 'qrc:///artwork/spalding-20mm-black.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble Time Round - Rose Gold 14 mm"; image: 'qrc:///artwork/spalding-14mm-rose-gold.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble Time Round - Silver Rainbow 14 mm"; image: 'qrc:///artwork/spalding-14mm-silver.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble Time Round - Black Rainbow 20 mm"; image: 'qrc:///artwork/spalding-20mm-black.png'; shape: "round"; screenWidth: 180; screenHeight: 180 }
    ListElement { modelName: "Pebble 2 SE - Black/Charcoal"; image: 'qrc:///artwork/tintin-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 HR - Black/Charcoal"; image: 'qrc:///artwork/tintin-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 SE - White/Gray"; image: 'qrc:///artwork/tintin-white.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 HR - Charcoal/Sorbet Green"; image: 'qrc:///artwork/tintin-green.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 HR - Charcoal/Red"; image: 'qrc:///artwork/tintin-red.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 HR - White/Gray"; image: 'qrc:///artwork/tintin-white.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 HR - White/Turquoise"; image: 'qrc:///artwork/tintin-blue.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time 2 - Black"; image: 'qrc:///artwork/bobby-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time 2 - Silver"; image: 'qrc:///artwork/bobby-silver.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time 2 - Gold"; image: 'qrc:///artwork/bobby-gold.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 Duo - Black"; image: 'qrc:///artwork/pebble-2-duo-black.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble 2 Duo - White"; image: 'qrc:///artwork/pebble-2-duo-white.png'; shape: "rectangle"; screenWidth: 144; screenHeight: 168 }
    ListElement { modelName: "Pebble Time 2 - Black/Gray"; image: 'qrc:///artwork/pebble-time-2-black-gray.png'; shape: "rectangle"; screenWidth: 200; screenHeight: 228 }
    ListElement { modelName: "Pebble Time 2 - Black/Red"; image: 'qrc:///artwork/pebble-time-2-black-red.png'; shape: "rectangle"; screenWidth: 200; screenHeight: 228 }
    ListElement { modelName: "Pebble Time 2 - Silver/Blue"; image: 'qrc:///artwork/pebble-time-2-silver-blue.png'; shape: "rectangle"; screenWidth: 200; screenHeight: 228 }
    ListElement { modelName: "Pebble Time 2 - Silver/Gray"; image: 'qrc:///artwork/pebble-time-2-silver-gray.png'; shape: "rectangle"; screenWidth: 200; screenHeight: 228 }
    ListElement { modelName: "Pebble Round 2 - Black 20 mm"; image: 'qrc:///artwork/pebble-round-2-black.png'; shape: "round"; screenWidth: 260; screenHeight: 260 }
    ListElement { modelName: "Pebble Round 2 - Silver 20 mm"; image: 'qrc:///artwork/pebble-round-2-silver.png'; shape: "round"; screenWidth: 260; screenHeight: 260 }
    ListElement { modelName: "Pebble Round 2 - Gold 14 mm"; image: 'qrc:///artwork/pebble-round-2-gold.png'; shape: "round"; screenWidth: 260; screenHeight: 260 }
    ListElement { modelName: "Pebble Round 2 - Silver 14 mm"; image: 'qrc:///artwork/pebble-round-2-silver-14mm.png'; shape: "round"; screenWidth: 260; screenHeight: 260 }
}
