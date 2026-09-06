/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "pebblebondremover.h"

int main() {
    const QString classicUuid =
        QStringLiteral("00000000-DECA-FADE-DECA-DEAFDECACAFF");
    const QString leUuid =
        QStringLiteral("0000fed9-0000-1000-8000-00805f9b34fb");

    assert(sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Pebble 1234"), QStringList() << leUuid, false, 0));
    assert(sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Pebble Time 1234"),
        QStringList() << classicUuid, false, 0));
    assert(sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Pebble Time 1234"), QStringList(), true, 0x240404));
    assert(sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Pebble Time Le 1234"), QStringList(), false, 0));

    assert(!sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Pebble 1234"), QStringList(), false, 0));
    assert(!sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Pebble Index 1234"), QStringList() << leUuid,
        false, 0));
    assert(!sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("Headphones 1234"), QStringList() << leUuid,
        true, 0x240404));
    assert(!sailfishplatform::isRecognizedPebbleDevice(
        QStringLiteral("pebble 1234"), QStringList() << leUuid, false, 0));
    return 0;
}
