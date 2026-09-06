/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LIBPEBBLE3D_SAILFISH_PEBBLEBONDREMOVER_H
#define LIBPEBBLE3D_SAILFISH_PEBBLEBONDREMOVER_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include "wire.h"

namespace sailfishplatform {

bool isRecognizedPebbleDevice(const QString &name, const QStringList &uuids,
                              bool hasBluetoothClass,
                              quint32 bluetoothClass);

int removePebbleBond(const lp3wire::PebbleBondRemoveData &request);

} // namespace sailfishplatform

#endif /* LIBPEBBLE3D_SAILFISH_PEBBLEBONDREMOVER_H */
