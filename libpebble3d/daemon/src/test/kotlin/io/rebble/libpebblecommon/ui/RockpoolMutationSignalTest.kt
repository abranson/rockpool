/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.ConnectedPebbleDevice
import io.rebble.libpebblecommon.connection.FakeConnectedDevice
import io.rebble.libpebblecommon.connection.FakeLibPebble
import io.rebble.libpebblecommon.connection.KnownPebbleDevice
import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.connection.PebbleDevice
import io.rebble.libpebblecommon.connection.fakeWatch
import io.rebble.libpebblecommon.locker.AppPlatform
import io.rebble.libpebblecommon.locker.AppProperties
import io.rebble.libpebblecommon.locker.AppType
import io.rebble.libpebblecommon.locker.LockerWrapper
import io.rebble.libpebblecommon.locker.SystemApps
import io.rebble.libpebblecommon.locker.wrap
import io.rebble.libpebblecommon.rockpool.NotificationFilterCoordinator
import io.rebble.libpebblecommon.rockpool.AccountSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.ProfileSettingsCoordinator
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import io.rebble.libpebblecommon.rockpool.TimelineWindowCoordinator
import io.rebble.libpebblecommon.rockpool.LibPebbleConfigMutationCoordinator
import io.rebble.libpebblecommon.health.HealthSettings
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.freedesktop.dbus.exceptions.DBusExecutionException
import org.freedesktop.dbus.messages.DBusSignal
import org.freedesktop.dbus.types.Variant
import java.nio.file.Files
import java.nio.file.Path
import kotlin.test.AfterTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertFailsWith
import kotlin.test.assertIs
import kotlin.test.assertSame
import kotlin.test.assertTrue
import kotlin.uuid.Uuid

class RockpoolMutationSignalTest {
    private val temporarySettingsDirectories = mutableListOf<Path>()

    @AfterTest
    fun cleanUpTemporarySettings() {
        temporarySettingsDirectories.forEach { it.toFile().deleteRecursively() }
        temporarySettingsDirectories.clear()
    }

    @Test
    fun `timeline window reaches BlobDB provider and survives object recreation`() {
        val directory = Files.createTempDirectory("rockpool-timeline-window-")
        try {
            val settingsPath = directory.resolve("rockpool.properties")
            val libPebble = FakeLibPebble()
            val obj = rockpoolObject(
                libPebble = libPebble,
                settings = RockpoolSettings(settingsPath),
                emit = {},
            )

            assertEquals(-2, obj.timelineWindowStart())
            assertEquals(-3600, obj.timelineWindowFade())
            assertEquals(7, obj.timelineWindowEnd())

            obj.setTimelineWindow(-4, 600, 10)
            assertEquals(-4, obj.timelineWindowStart())
            assertEquals(-600, obj.timelineWindowFade())
            assertEquals(10, obj.timelineWindowEnd())

            val reloaded = rockpoolObject(
                libPebble = libPebble,
                settings = RockpoolSettings(settingsPath),
                emit = {},
            )
            assertEquals(-4, reloaded.timelineWindowStart())
            assertEquals(-600, reloaded.timelineWindowFade())
            assertEquals(10, reloaded.timelineWindowEnd())
        } finally {
            directory.toFile().deleteRecursively()
        }
    }

    @Test
    fun `saving one canned response group preserves every other group`() {
        val directory = Files.createTempDirectory("rockpool-canned-test-")
        try {
            val settingsPath = directory.resolve("rockpool.properties")
            val settings = RockpoolSettings(settingsPath)
            val libPebble = FakeLibPebble()
            val originalNotificationConfig = libPebble.config.value.notificationConfig
            val obj = rockpoolObject(
                libPebble = libPebble,
                settings = settings,
                emit = {},
            )
            obj.setCannedResponses(
                linkedMapOf(
                    "x-nemo.messaging.sms" to Variant(listOf("Yes", "No"), "as"),
                    "x-nemo.messaging.im" to Variant(listOf("Ping"), "as"),
                ),
            )

            obj.setCannedResponses(
                mapOf("x-nemo.messaging.sms" to Variant(listOf("Later"), "as")),
            )

            val stored = rockpoolObject(
                libPebble = FakeLibPebble(),
                settings = RockpoolSettings(settingsPath),
                emit = {},
            ).cannedResponses()
            assertEquals(
                listOf("Later"),
                (stored.getValue("x-nemo.messaging.sms").value as Collection<*>).toList(),
            )
            assertEquals(
                listOf("Ping"),
                (stored.getValue("x-nemo.messaging.im").value as Collection<*>).toList(),
            )

            obj.setCannedResponses(
                mapOf("x-nemo.messaging.sms" to Variant(emptyList<String>(), "as")),
            )
            val cleared = rockpoolObject(
                libPebble = FakeLibPebble(),
                settings = RockpoolSettings(settingsPath),
                emit = {},
            ).cannedResponses()
            assertEquals(
                emptyList(),
                (cleared.getValue("x-nemo.messaging.sms").value as Collection<*>).toList(),
            )
            assertEquals(
                listOf("Ping"),
                (cleared.getValue("x-nemo.messaging.im").value as Collection<*>).toList(),
            )
            // Compatibility groups are source-specific. They must not be flattened into
            // libpebble3's global list, which would expose one source's replies on another.
            assertEquals(originalNotificationConfig, libPebble.config.value.notificationConfig)
        } finally {
            directory.toFile().deleteRecursively()
        }
    }

    @Test
    fun `canned responses and contacts reject records that cannot round trip`() {
        val settings = temporarySettings()
        val obj = rockpoolObject(
            libPebble = FakeLibPebble(),
            settings = settings,
            emit = {},
        )
        val invalid = listOf(
            mapOf("keys" to Variant(listOf("reserved"), "as")),
            mapOf(" " to Variant(listOf("blank name"), "as")),
            mapOf("bad\u001fname" to Variant(listOf("value"), "as")),
            mapOf("source" to Variant(listOf("bad\u001fvalue"), "as")),
            mapOf("source" to Variant("not a string array")),
            mapOf("source" to Variant(listOf(42), "ai")),
            mapOf("source" to Variant(List(65) { "value-$it" }, "as")),
            mapOf("source" to Variant(listOf("x".repeat(513)), "as")),
            (0..64).associate { "source-$it" to Variant(listOf("value"), "as") },
        )

        invalid.forEach { record ->
            assertFailsWith<DBusExecutionException> { obj.setCannedResponses(record) }
            assertEquals(emptyMap(), obj.cannedResponses())
        }
        assertFailsWith<DBusExecutionException> {
            obj.setFavoriteContacts(mapOf("keys" to Variant(listOf("tel:+331"), "as")))
        }
        assertEquals(emptyMap(), obj.getFavoriteContacts(emptyList()))
    }

    @Test
    fun `valid canned update repairs malformed persisted groups`() {
        val settings = temporarySettings()
        assertTrue(
            settings.setAllChecked(
                mapOf(
                    "canned.keys" to "keys\u001fgood\u001fbad",
                    "canned.good" to "Ping",
                    "canned.bad" to "x".repeat(513),
                ),
            ),
        )
        val obj = rockpoolObject(
            libPebble = FakeLibPebble(),
            settings = settings,
            emit = {},
        )

        assertEquals(setOf("good"), obj.cannedResponses().keys)
        obj.setCannedResponses(mapOf("new" to Variant(listOf("Later"), "as")))

        assertEquals(setOf("good", "new"), obj.cannedResponses().keys)
        assertEquals("good\u001fnew", settings.get("canned.keys"))
        assertFalse(settings.contains("canned.bad"))
    }

    @Test
    fun `favorite contacts replace the complete durable snapshot`() {
        val directory = Files.createTempDirectory("rockpool-contacts-test-")
        try {
            val settingsPath = directory.resolve("rockpool.properties")
            val obj = rockpoolObject(
                libPebble = FakeLibPebble(),
                settings = RockpoolSettings(settingsPath),
                emit = {},
            )
            obj.setFavoriteContacts(
                linkedMapOf(
                    "Alice" to Variant(listOf("tel:+331"), "as"),
                    "Bob" to Variant(listOf("tel:+332"), "as"),
                ),
            )

            obj.setFavoriteContacts(
                mapOf("Bob" to Variant(emptyList<String>(), "as")),
            )

            val replaced = rockpoolObject(
                libPebble = FakeLibPebble(),
                settings = RockpoolSettings(settingsPath),
                emit = {},
            ).getFavoriteContacts(emptyList())
            assertEquals(setOf("Bob"), replaced.keys)
            assertEquals(
                emptyList(),
                (replaced.getValue("Bob").value as Collection<*>).toList(),
            )

            obj.setFavoriteContacts(emptyMap())
            val cleared = rockpoolObject(
                libPebble = FakeLibPebble(),
                settings = RockpoolSettings(settingsPath),
                emit = {},
            ).getFavoriteContacts(emptyList())
            assertEquals(emptyMap(), cleared)
        } finally {
            directory.toFile().deleteRecursively()
        }
    }

    @Test
    fun `weather locations signal follows only a successful durable update`() {
        var permitWrite = true
        val weather = RockpoolWeatherCoordinator(
            loadSettings = { emptyMap() },
            replaceSettings = { permitWrite },
            updateWeatherData = {},
        )
        val signals = mutableListOf<DBusSignal>()
        val paths = listOf(WATCH_PATH, "/org/rockpool/11_22_33_44_55_66")
        val obj = rockpoolObject(
            libPebble = FakeLibPebble(),
            settings = temporarySettings(),
            weather = weather,
            emit = signals::add,
            broadcast = { signalForPath -> paths.forEach { signals += signalForPath(it) } },
        )
        val london = Variant(listOf("London", "51.5", "-0.1"), "as")

        obj.SetWeatherLocations(listOf(london))

        assertEquals(paths, signals.map { it.path })
        signals.forEach { signal ->
            val changed = assertIs<RockpoolPebble.WeatherLocationsChanged>(signal)
            assertEquals("av", changed.sig)
            assertEquals(listOf(london), changed.parameters.single())
        }

        permitWrite = false
        assertFailsWith<DBusExecutionException> {
            obj.SetWeatherLocations(listOf(Variant(listOf("Paris", "48.8", "2.3"), "as")))
        }
        assertEquals(2, signals.size)
    }

    @Test
    fun `app order signal follows only a successful atomic update`() = runBlocking {
        val watch = fakeWatch(connected = true) as KnownPebbleDevice
        val settings = SystemApps.Settings.wrap(order = -10)
        val application = LockerWrapper.NormalApp(
            properties = AppProperties(
                id = Uuid.parse("11111111-1111-4111-8111-111111111111"),
                type = AppType.Watchapp,
                title = "Example",
                developerName = "Developer",
                developerId = "developer-id",
                platforms = listOf(AppPlatform(watch.watchType.watchType)),
                version = "1.0",
                hearts = null,
                category = null,
                iosCompanion = null,
                androidCompanion = null,
                order = 1,
                sourceLink = null,
                storeId = "store-id",
                capabilities = emptyList(),
            ),
            sideloaded = true,
            configurable = false,
            sync = true,
        )
        val installed = listOf(settings, application)
        var acceptOrder = true
        val applied = mutableListOf<List<Uuid>>()
        val base = FakeLibPebble()
        val libPebble = object : LibPebble by base {
            override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(watch))

            override fun getLocker(
                type: AppType,
                searchQuery: String?,
                limit: Int,
            ) = flowOf(installed.filter { it.properties.type == type })

            override suspend fun setAppOrder(orderedIds: List<Uuid>): Boolean {
                applied += orderedIds
                return acceptOrder
            }
        }
        val signals = mutableListOf<DBusSignal>()
        val mutationScope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val obj = rockpoolObject(
            libPebble = libPebble,
            settings = temporarySettings(),
            emit = signals::add,
            scope = mutationScope,
        )
        val order = installed.map { formatRockpoolAppUuid(it.properties.id) }

        try {
            obj.SetAppOrder(order)
            yield()

            assertEquals(listOf(installed.map { it.properties.id }), applied)
            assertIs<RockpoolPebble.InstalledAppsChanged>(signals.single())

            acceptOrder = false
            obj.SetAppOrder(order)
            yield()

            assertEquals(2, applied.size)
            assertEquals(1, signals.size)
        } finally {
            mutationScope.cancel()
        }
    }

    @Test
    fun `global calendar setting notifies every exported watch`() {
        val base = FakeLibPebble()
        val libPebble = object : LibPebble by base {
            override val config = MutableStateFlow(base.config.value)

            override fun updateConfig(config: io.rebble.libpebblecommon.LibPebbleConfig) {
                this.config.value = config
            }
        }
        val signals = mutableListOf<DBusSignal>()
        val paths = listOf(WATCH_PATH, "/org/rockpool/11_22_33_44_55_66")
        LibPebbleConfigMutationCoordinator.forLibPebble(libPebble).addListener { change ->
            if (change.previous.watchConfig.calendarPins != change.current.watchConfig.calendarPins) {
                paths.forEach { path ->
                    signals += RockpoolPebble.CalendarSyncEnabledChanged(path)
                }
            }
        }
        val obj = rockpoolObject(
            libPebble = libPebble,
            settings = temporarySettings(),
            emit = signals::add,
            broadcast = { signalForPath -> paths.forEach { signals += signalForPath(it) } },
        )

        obj.SetCalendarSyncEnabled(false)

        assertEquals(false, libPebble.config.value.watchConfig.calendarPins)
        assertEquals(paths, signals.map { it.path })
        signals.forEach { assertIs<RockpoolPebble.CalendarSyncEnabledChanged>(it) }
    }

    @Test
    fun `calendar storage rejection changes neither canonical record nor active config`() {
        val base = FakeLibPebble()
        val libPebble = object : LibPebble by base {
            override val config = MutableStateFlow(base.config.value)

            override fun updateConfig(config: io.rebble.libpebblecommon.LibPebbleConfig) {
                this.config.value = config
            }
        }
        val settings = temporarySettings()
        val obj = rockpoolObject(
            libPebble = libPebble,
            settings = settings,
            emit = {},
            configFitsStorage = { false },
        )

        assertFailsWith<DBusExecutionException> {
            obj.SetCalendarSyncEnabled(false)
        }

        assertTrue(libPebble.config.value.watchConfig.calendarPins)
        assertTrue(settings.entries("primary.timeline.calendar.").isEmpty())
    }

    @Test
    fun `weather and watch selectors share the durable unit preference`() = runBlocking {
        val directory = Files.createTempDirectory("rockpool-weather-units-")
        temporarySettingsDirectories.add(directory)
        val settings = RockpoolSettings(directory.resolve("rockpool.properties"))
        settings.set("weather.units", "e") // Stale preference from an older installation.
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first().copy(imperialUnits = false))
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val coordinator = RockpoolHealthCoordinator(libPebble) { state.value = it }
        val obj = rockpoolObject(
            libPebble = libPebble, settings = settings, healthCoordinator = coordinator, emit = {},
        )
        assertEquals("m", obj.WeatherUnits())
        obj.setWeatherUnits("e")
        assertTrue(state.value.imperialUnits)
        assertTrue(obj.ImperialUnits())
        assertEquals("e", obj.WeatherUnits())
        obj.SetImperialUnits(false)
        assertEquals("m", obj.WeatherUnits())
        obj.setWeatherUnits("h")
        assertFalse(state.value.imperialUnits)
        assertEquals("h", obj.WeatherUnits())
        obj.SetImperialUnits(true)
        assertEquals("e", obj.WeatherUnits())
    }

    @Test
    fun `global health settings notify every exported watch`() = runBlocking {
        val directory = Files.createTempDirectory("rockpool-health-signal-")
        val base = FakeLibPebble()
        val state = MutableStateFlow(base.healthSettings.first())
        val libPebble = object : LibPebble by base {
            override val healthSettings: Flow<HealthSettings> = state
        }
        val paths = listOf(WATCH_PATH, "/org/rockpool/11_22_33_44_55_66")
        val signals = mutableListOf<DBusSignal>()
        val healthCoordinator = RockpoolHealthCoordinator(libPebble) { state.value = it }
        healthCoordinator.addListener { change ->
            if (rockpoolHealthParams(change.previous) != rockpoolHealthParams(change.current)) {
                paths.forEach { signals += RockpoolPebble.HealthParamsChanged(it) }
            }
            if (change.previous.imperialUnits != change.current.imperialUnits) {
                paths.forEach { signals += RockpoolPebble.ImperialUnitsChanged(it) }
            }
        }
        try {
            val obj = rockpoolObject(
                libPebble = libPebble,
                settings = RockpoolSettings(directory.resolve("rockpool.properties")),
                healthCoordinator = healthCoordinator,
                emit = signals::add,
                broadcast = { signalForPath -> paths.forEach { signals += signalForPath(it) } },
            )

            obj.SetImperialUnits(!state.value.imperialUnits)

            assertEquals(paths, signals.map { it.path })
            signals.forEach { assertIs<RockpoolPebble.ImperialUnitsChanged>(it) }

            signals.clear()
            obj.SetHealthParams(mapOf("age" to Variant(42)))

            assertEquals(paths, signals.map { it.path })
            signals.forEach { assertIs<RockpoolPebble.HealthParamsChanged>(it) }
        } finally {
            directory.toFile().deleteRecursively()
        }
    }

    @Test
    fun `health history is account global and sync targets the addressed watch`() {
        val first = fakeWatch(connected = true) as FakeConnectedDevice
        val second = fakeWatch(connected = true) as FakeConnectedDevice
        val base = FakeLibPebble()
        val libPebble = object : LibPebble by base {
            override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(first, second))
        }
        var requested: ConnectedPebbleDevice? = null
        val obj = rockpoolObject(
            libPebble = libPebble,
            settings = temporarySettings(),
            emit = {},
            requestHealthData = { watch ->
                requested = watch
                true
            },
        )

        val overview = obj.HealthOverview()
        assertEquals("av", overview.getValue("stepsWeek").sig)
        assertEquals("av", overview.getValue("sleepWeek").sig)

        obj.FetchHealthData()

        assertSame(first, requested)
    }

    @Test
    fun `health sync rejects disconnected and unacknowledged requests`() {
        val disconnected = fakeWatch(connected = false)
        val disconnectedBase = FakeLibPebble()
        val disconnectedLib = object : LibPebble by disconnectedBase {
            override val watches = MutableStateFlow(listOf(disconnected))
        }
        val disconnectedObject = rockpoolObject(
            libPebble = disconnectedLib,
            settings = temporarySettings(),
            emit = {},
        )
        assertFailsWith<DBusExecutionException> { disconnectedObject.FetchHealthData() }

        val connected = fakeWatch(connected = true) as FakeConnectedDevice
        val connectedBase = FakeLibPebble()
        val connectedLib = object : LibPebble by connectedBase {
            override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(connected))
        }
        val rejectedObject = rockpoolObject(
            libPebble = connectedLib,
            settings = temporarySettings(),
            emit = {},
            requestHealthData = { false },
        )
        assertFailsWith<DBusExecutionException> { rejectedObject.FetchHealthData() }
    }

    @Test
    fun `timeline reset targets only the addressed connected watch`() = runBlocking {
        val first = fakeWatch(connected = true) as FakeConnectedDevice
        val second = fakeWatch(connected = true) as FakeConnectedDevice
        val base = FakeLibPebble()
        val libPebble = object : LibPebble by base {
            override val watches = MutableStateFlow<List<PebbleDevice>>(listOf(first, second))
        }
        val mutationScope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val obj = rockpoolObject(
            libPebble = libPebble,
            settings = temporarySettings(),
            emit = {},
            scope = mutationScope,
        )

        try {
            obj.resetTimeline()
            yield()

            assertEquals(1, first.timelineResetCount)
            assertEquals(0, second.timelineResetCount)
        } finally {
            mutationScope.cancel()
        }
    }

    private fun temporarySettings(): RockpoolSettings {
        val directory = Files.createTempDirectory("rockpool-mutation-settings-")
        temporarySettingsDirectories.add(directory)
        return RockpoolSettings(directory.resolve("rockpool.properties"))
    }

    private fun rockpoolObject(
        libPebble: LibPebble,
        settings: RockpoolSettings,
        notificationFilters: NotificationFilterCoordinator = NotificationFilterCoordinator(
            loadEntries = { emptyMap() },
            replacePrefixes = { true },
            replaceRuntimeFilters = { true },
            updateMuteState = { _, _ -> },
            forgetApplication = {},
            reconcileMuteStates = {},
        ),
        weather: RockpoolWeatherCoordinator = RockpoolWeatherCoordinator(
            loadSettings = { emptyMap() },
            replaceSettings = { true },
            updateWeatherData = {},
        ),
        healthCoordinator: RockpoolHealthCoordinator = RockpoolHealthCoordinator(libPebble) {
            libPebble.updateHealthSettings(it)
        },
        emit: (DBusSignal) -> Unit,
        broadcast: ((String) -> DBusSignal) -> Unit = { signalForPath ->
            emit(signalForPath(WATCH_PATH))
        },
        configFitsStorage: (io.rebble.libpebblecommon.LibPebbleConfig) -> Boolean = { true },
        requestHealthData: suspend (ConnectedPebbleDevice) -> Boolean = { watch ->
            watch.requestHealthData(fullSync = false)
        },
        scope: kotlinx.coroutines.CoroutineScope = kotlinx.coroutines.CoroutineScope(
            kotlinx.coroutines.Dispatchers.Unconfined,
        ),
    ) = RockpoolPebbleObject(
        address = libPebble.watches.value.firstOrNull()?.identifier?.asString.orEmpty(),
        path = WATCH_PATH,
        libPebble = libPebble,
        settings = settings,
        notificationFilterMutations = RockpoolNotificationFilterMutations(
            scope = scope,
            notificationFilters = notificationFilters,
            applicationName = { null },
        ),
        accountSettings = AccountSettingsCoordinator(
            currentToken = { "" },
            persist = { true },
        ),
        profileSettings = ProfileSettingsCoordinator(
            objectIdForWatch = { _, _ -> "test" },
            persist = { true },
        ),
        timelineWindow = TimelineWindowCoordinator(settings),
        healthCoordinator = healthCoordinator,
        weatherCoordinator = weather,
        notificationAppearance = RockpoolNotificationAppearanceCoordinator(libPebble),
        scope = scope,
        emit = emit,
        broadcast = broadcast,
        configFitsStorage = configFitsStorage,
        requestHealthData = requestHealthData,
    )

    private companion object {
        const val WATCH_PATH = "/org/rockpool/AA_BB_CC_DD_EE_FF"
    }
}
