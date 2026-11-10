/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.linux.LinuxDeviceActivity
import kotlinx.coroutines.flow.StateFlow

/** Unknown phone activity must not suppress notification forwarding. */
internal class SailfishDeviceActivity(private val active: StateFlow<Boolean?>) : LinuxDeviceActivity {
    override fun isActiveAndUnlocked(): Boolean = active.value == true
}
