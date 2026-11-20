/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.ui

import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.get
import io.ktor.client.request.parameter
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpStatusCode
import io.ktor.http.isSuccess
import io.rebble.libpebblecommon.disk.pbw.StorePbwMetadata
import io.rebble.libpebblecommon.linux.web.AppstoreListingId
import io.rebble.libpebblecommon.linux.web.downloadPbwFile
import io.rebble.libpebblecommon.linux.web.CoreAppstore
import io.rebble.libpebblecommon.linux.web.RebbleAppstore
import io.rebble.libpebblecommon.metadata.WatchType
import io.rebble.libpebblecommon.rockpool.RockpoolSettings
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull
import java.io.File
import java.net.URI
import kotlin.uuid.Uuid

internal enum class RockpoolAppStoreSource(val value: String, val core: Boolean) {
    Rebble("rebble", false),
    Core("core", true);

    companion object {
        fun parse(value: String): RockpoolAppStoreSource? = entries.firstOrNull { it.value == value }
    }
}

internal class RockpoolAppStore(private val settings: RockpoolSettings) {
    // Each selection gets a distinct identity so an A -> B -> A switch also retires old requests.
    class Selection(val source: RockpoolAppStoreSource)

    private val selected = MutableStateFlow(Selection(
        RockpoolAppStoreSource.parse(settings.get(PREFERENCE_KEY)) ?: RockpoolAppStoreSource.Rebble,
    ))
    val selection = selected.asStateFlow()

    fun isCurrent(selection: Selection): Boolean = selected.value === selection

    @Synchronized
    fun select(source: RockpoolAppStoreSource, changed: () -> Unit): Boolean {
        if (selected.value.source == source) return true
        if (!settings.setChecked(PREFERENCE_KEY, source.value)) return false
        selected.value = Selection(source)
        changed()
        return true
    }

    @Synchronized
    fun rememberUpdate(selection: Selection, key: String, version: String): Boolean =
        isCurrent(selection) && settings.setChecked(key, version)

    suspend fun lookup(selection: Selection, target: RockpoolAppUpdateTarget): RockpoolStoreListing? {
        if (!isCurrent(selection)) return null
        val installedId = AppstoreListingId.parse(target.storeId) ?: return null
        val sameProvider = installedId.core == selection.source.core
        val expectedId = when {
            sameProvider -> installedId
            selection.source.core -> null // The Core endpoint can resolve the UUID directly.
            else -> RebbleAppstore.findListingId(target.uuid) ?: return null
        }
        if (!isCurrent(selection)) return null
        val url = expectedId?.lookupUrl ?: CoreAppstore.lookupUrl(target.uuid)
        val response = appStoreClient.get(url) { parameter("hardware", target.hardware) }
        if (response.status == HttpStatusCode.NotFound) return null
        check(response.status.isSuccess()) { "Store lookup returned ${response.status}" }
        val listing = rockpoolStoreListing(response.bodyAsText(), target, selection.source, expectedId)
        return listing?.takeIf { isCurrent(selection) }
    }

    suspend fun download(listing: RockpoolStoreListing, destination: File): StorePbwMetadata? {
        val body = JsonObject(mapOf("data" to JsonArray(listOf(listing.app)))).toString()
        val metadata = if (listing.id.core) {
            CoreAppstore.decodeMetadata(body, listing.id)
        } else {
            RebbleAppstore.decodeMetadata(body, listing.id.appId)
        } ?: return null
        check(metadata.uuid == listing.uuid && metadata.releaseVersion == listing.version)
        check(httpsPackageUrl(metadata.appstoreData.pbwLink))
        appStoreClient.downloadPbwFile(metadata.appstoreData.pbwLink, destination)
        return metadata
    }

    private companion object {
        const val PREFERENCE_KEY = "appstore.preferredSource"
    }
}

internal data class RockpoolStoreListing(
    val app: JsonObject,
    val id: AppstoreListingId,
    val uuid: Uuid,
    val version: String,
    val updateAvailable: Boolean,
) {
    fun toJson(): String = JsonObject(mapOf(
        "app" to app,
        "updateAvailable" to JsonPrimitive(updateAvailable),
    )).toString()
}

internal fun rockpoolStoreListing(
    body: String,
    target: RockpoolAppUpdateTarget,
    source: RockpoolAppStoreSource,
    expectedId: AppstoreListingId?,
): RockpoolStoreListing? {
    val records = (Json.parseToJsonElement(body) as? JsonObject)?.get("data") as? JsonArray ?: return null
    val variants = WatchType.fromCodename(target.hardware)?.getCompatibleAppVariants() ?: return null
    return records.filterIsInstance<JsonObject>().firstNotNullOfOrNull { app ->
        val id = app.string("id")?.let {
            AppstoreListingId.parse(if (source.core) "core:$it" else it)
        } ?: return@firstNotNullOfOrNull null
        if (id.core != source.core ||
            (expectedId != null && !id.value.equals(expectedId.value, ignoreCase = true)) ||
            !app.string("uuid").equals(target.uuid.toString(), ignoreCase = true)
        ) return@firstNotNullOfOrNull null
        val compatibility = app["compatibility"] as? JsonObject ?: return@firstNotNullOfOrNull null
        if (variants.none {
            val supported = (compatibility[it.codename] as? JsonObject)?.get("supported") as? JsonPrimitive
            supported?.booleanOrNull == true
        }) return@firstNotNullOfOrNull null
        val release = app["latest_release"] as? JsonObject ?: return@firstNotNullOfOrNull null
        val version = release.string("version")?.takeIf { it.isNotBlank() } ?: return@firstNotNullOfOrNull null
        val pbw = release.string("pbw_file")
        val samePackage = id.value.equals(target.storeId, ignoreCase = true) && pbw == target.pbwUrl
        RockpoolStoreListing(
            app, id, target.uuid, version,
            !samePackage && pbw?.let(::httpsPackageUrl) == true && rockpoolVersionIsNewer(version, target.version),
        )
    }
}

private fun JsonObject.string(key: String): String? = (this[key] as? JsonPrimitive)
    ?.takeIf { it.isString }?.contentOrNull

private fun httpsPackageUrl(value: String): Boolean {
    val url = runCatching { URI(value) }.getOrNull() ?: return false
    return url.scheme == "https" && url.host != null && url.userInfo == null
}

private val numericVersion = Regex("[0-9]+(?:\\.[0-9]+)*")

/** Compare dotted numeric release labels, treating a missing component as zero. */
internal fun rockpoolVersionIsNewer(candidate: String, installed: String): Boolean {
    if (candidate.length > 128 || installed.length > 128 ||
        !numericVersion.matches(candidate) || !numericVersion.matches(installed)
    ) return false
    val newer = candidate.split('.')
    val older = installed.split('.')
    for (index in 0 until maxOf(newer.size, older.size)) {
        val left = newer.getOrElse(index) { "" }.trimStart('0')
        val right = older.getOrElse(index) { "" }.trimStart('0')
        if (left.length != right.length) return left.length > right.length
        if (left != right) return left > right
    }
    return false
}

private val appStoreClient by lazy {
    HttpClient(CIO) {
        install(HttpTimeout) {
            requestTimeoutMillis = 15_000L
            connectTimeoutMillis = 5_000L
            socketTimeoutMillis = 10_000L
        }
    }
}
