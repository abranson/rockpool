/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.messaging.SendMessageResult
import io.rebble.libpebblecommon.messaging.SystemMessaging

internal class PlatformSystemMessaging(
    private val controller: PlatformProviderController,
) : SystemMessaging {
    override suspend fun sendMessage(
        accountId: String,
        recipient: String,
        text: String,
    ): SendMessageResult = when (controller.sendMessage(accountId, recipient, text)) {
        PlatformProviderController.STATUS_OK -> SendMessageResult.Sent
        PlatformProviderController.STATUS_UNAVAILABLE,
        PlatformProviderController.STATUS_NOT_SUPPORTED -> SendMessageResult.Unavailable
        else -> SendMessageResult.Failed
    }
}
