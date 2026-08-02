/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.bt.LinuxPairingRequester
import io.rebble.libpebblecommon.connection.bt.ble.BlePlatformConfig
import io.rebble.libpebblecommon.connection.bt.ble.bluez.LinuxBluezAdapterSelector
import io.rebble.libpebblecommon.connection.bt.classic.transport.LinuxRfcommSocketFactory
import io.rebble.libpebblecommon.calls.LegacyPhoneReceiver
import io.rebble.libpebblecommon.linux.LinuxDeviceActivity
import io.rebble.libpebblecommon.time.TimeChanged
import io.rebble.libpebblecommon.linux.notifications.LinuxNotificationBackend
import io.rebble.libpebblecommon.linux.music.VolumeControl
import io.rebble.libpebblecommon.connection.endpointmanager.blobdb.TimelineWindowProvider
import org.koin.core.module.Module
import org.koin.dsl.bind
import org.koin.dsl.module

/** Linux-only time-change signal; libpebble3 continues to read its portable clock. */
internal class PlatformTimeChanged(
    private val controller: PlatformProviderController,
) : TimeChanged {
    override fun registerForTimeChanges(onChanged: () -> Unit) {
        controller.addTimeChangedListener(onChanged)
    }
}

internal fun platformProviderModule(
    controller: PlatformProviderController,
    notificationBackend: PlatformNotificationBackend,
    deviceActivity: LinuxDeviceActivity,
    timelineWindow: TimelineWindowCoordinator,
    rfcommSocketFactory: SailfishRfcommSocketFactory,
): Module = module {
    single {
        BlePlatformConfig(
            delayBleConnectionsAfterAppStart = false,
            delayBleDisconnections = true,
            supportsBtClassic = rfcommSocketFactory.available,
        )
    }
    single { PlatformTimeChanged(controller) } bind TimeChanged::class
    single { notificationBackend } bind LinuxNotificationBackend::class
    single { deviceActivity } bind LinuxDeviceActivity::class
    single { PlatformCallsBackend(controller) } bind LegacyPhoneReceiver::class
    single { PlatformVolumeControl(controller) } bind VolumeControl::class
    single { SailfishPairingRequester() } bind LinuxPairingRequester::class
    single { SailfishBluezAdapterSelector() } bind LinuxBluezAdapterSelector::class
    single { rfcommSocketFactory } bind LinuxRfcommSocketFactory::class
    single { timelineWindow } bind TimelineWindowProvider::class
}
