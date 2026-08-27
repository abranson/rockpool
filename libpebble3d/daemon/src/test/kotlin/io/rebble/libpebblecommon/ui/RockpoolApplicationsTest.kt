/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.locker.AppPlatform
import io.rebble.libpebblecommon.locker.AppProperties
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.locker.SystemApps
import io.rebble.libpebblecommon.locker.wrap
import io.rebble.libpebblecommon.metadata.WatchType
import org.freedesktop.dbus.types.Variant
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertFalse
import kotlin.test.assertSame
import kotlin.test.assertTrue
import kotlin.uuid.Uuid

class RockpoolApplicationsTest {
    @Test
    fun `account-global mutations are allowed only for the sole addressed watch`() {
        assertTrue(rockpoolGlobalAppMutationAllowed("AA:BB", listOf("aa:bb")))
        assertFalse(rockpoolGlobalAppMutationAllowed("AA:BB", emptyList()))
        assertFalse(rockpoolGlobalAppMutationAllowed("AA:BB", listOf("CC:DD")))
        assertFalse(rockpoolGlobalAppMutationAllowed("AA:BB", listOf("AA:BB", "CC:DD")))
    }

    @Test
    fun `compat records are watch scoped and retain the legacy wire shape`() {
        val settings = SystemApps.Settings.wrap(order = -10)
        val synced = normalApp(
            uuid = "11111111-1111-4111-8111-111111111111",
            order = 1,
            platforms = listOf(
                AppPlatform(WatchType.APLITE, iconImageUrl = "apl.png"),
                AppPlatform(WatchType.EMERY, iconImageUrl = "emery.png"),
            ),
        )
        val accountOnly = normalApp(
            uuid = "22222222-2222-4222-8222-222222222222",
            order = 2,
            sync = false,
            platforms = listOf(AppPlatform(WatchType.APLITE)),
        )
        val incompatible = normalApp(
            uuid = "33333333-3333-4333-8333-333333333333",
            order = 3,
            platforms = listOf(AppPlatform(WatchType.CHALK)),
        )

        val records = rockpoolApplicationRecords(
            listOf(incompatible, accountOnly, synced, settings),
            WatchType.EMERY,
        )

        assertEquals(2, records.size)
        assertEquals("a{sv}", records[1].sig)
        val record = records[1].variantMap()
        assertEquals("{11111111-1111-4111-8111-111111111111}", record.value("uuid"))
        assertEquals("emery.png", record.value("icon"))
        assertEquals("store-id", record.value("storeId"))
        assertEquals(true, record.value("hasSettings"))
        assertEquals(false, record.value("systemApp"))
        assertEquals(
            setOf(
                "uuid",
                "storeId",
                "name",
                "vendor",
                "watchface",
                "version",
                "hasSettings",
                "icon",
                "systemApp",
            ),
            record.keys,
        )
    }

    @Test
    fun `compat UUIDs use Qt braces and resolution accepts UUID or store id`() {
        val application = normalApp(
            uuid = "44444444-4444-4444-8444-444444444444",
            order = 1,
        )
        val installed = listOf(application)

        assertEquals("{44444444-4444-4444-8444-444444444444}", formatRockpoolAppUuid(application.properties.id))
        assertSame(
            application,
            resolveRockpoolInstalledApplication(installed, "{44444444-4444-4444-8444-444444444444}"),
        )
        assertSame(application, resolveRockpoolInstalledApplication(installed, "store-id"))
        assertNull(resolveRockpoolInstalledApplication(installed, "another-store-id"))
    }

    @Test
    fun `compat order requires the exact installed set with Settings first`() {
        val settings = SystemApps.Settings.wrap(order = -10)
        val app = normalApp(
            uuid = "55555555-5555-4555-8555-555555555555",
            order = 1,
        )
        val face = normalApp(
            uuid = "66666666-6666-4666-8666-666666666666",
            order = 2,
            type = AppType.Watchface,
        )
        val installed = listOf(settings, app, face)
        val valid = installed.map { formatRockpoolAppUuid(it.properties.id) }

        assertEquals(installed.map { it.properties.id }, validateRockpoolAppOrder(valid, installed))
        assertNull(validateRockpoolAppOrder(valid.dropLast(1), installed))
        assertNull(validateRockpoolAppOrder(listOf(valid[0], valid[1], valid[1]), installed))
        assertNull(validateRockpoolAppOrder(listOf(valid[1], valid[0], valid[2]), installed))
        assertNull(
            validateRockpoolAppOrder(
                listOf(valid[0], valid[1], "{77777777-7777-4777-8777-777777777777}"),
                installed,
            ),
        )
    }

    private fun normalApp(
        uuid: String,
        order: Int,
        type: AppType = AppType.Watchapp,
        sync: Boolean = true,
        platforms: List<AppPlatform> = listOf(AppPlatform(WatchType.EMERY)),
    ): LockerWrapper.NormalApp = LockerWrapper.NormalApp(
        properties = AppProperties(
            id = Uuid.parse(uuid),
            type = type,
            title = "Example",
            developerName = "Developer",
            developerId = "developer-id",
            platforms = platforms,
            version = "1.2.3",
            hearts = null,
            category = null,
            iosCompanion = null,
            androidCompanion = null,
            order = order,
            sourceLink = null,
            storeId = "store-id",
            capabilities = emptyList(),
        ),
        sideloaded = true,
        configurable = true,
        sync = sync,
    )

    @Suppress("UNCHECKED_CAST")
    private fun Variant<*>.variantMap(): Map<String, Variant<*>> = value as Map<String, Variant<*>>

    private fun Map<String, Variant<*>>.value(key: String): Any? = checkNotNull(this[key]).value
}
