/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.bearerAuth
import io.ktor.client.request.get
import io.ktor.client.request.parameter
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.statement.bodyAsText
import io.ktor.http.ContentType
import io.ktor.http.contentType
import io.ktor.http.isSuccess
import io.rebble.libpebblecommon.linux.web.AppstoreListingId
import io.rebble.libpebblecommon.linux.web.CoreAppstore
import io.rebble.libpebblecommon.linux.web.RebbleAppstore
import io.rebble.libpebblecommon.web.*
import kotlinx.serialization.json.*
import java.util.Base64
import kotlin.uuid.Uuid

internal data class SavedBuild(val uuid: Uuid, val listing: AppstoreListingId?)
internal const val CORE_FEED = "https://appstore-api.repebble.com/api"
internal const val REBBLE_FEED = "https://appstore-api.rebble.io/api"

internal class CoreCloudLocker(private val client: HttpClient = HttpClient(CIO) {
    followRedirects = false
    install(HttpTimeout) { requestTimeoutMillis = 15_000 }
}) {
    private val json = Json { ignoreUnknownKeys = true }

    // The exchanged Firebase ID token identifies the project. Its audience only enters
    // a path segment on Google's fixed origin. Firestore verifies the bearer itself.
    private fun collection(uid: String, token: String): String {
        val payload = Json.parseToJsonElement(Base64.getUrlDecoder().decode(token.split('.')[1]).decodeToString()).jsonObject
        val project = payload.getValue("aud").jsonPrimitive.content
        require(project.matches(Regex("[a-z][a-z0-9-]{4,29}")) && payload["sub"]?.jsonPrimitive?.content == uid)
        require(uid.matches(Regex("[A-Za-z0-9_-]{1,128}")))
        return "https://firestore.googleapis.com/v1/projects/$project/databases/(default)/documents/lockers/$uid/entries"
    }

    class Document(val build: SavedBuild)

    suspend fun entries(uid: String, token: String): List<Document> {
        val url = collection(uid, token)
        val documents = mutableListOf<Document>()
        val seen = mutableSetOf<String>()
        var page = ""
        do {
            check(seen.add(page) && seen.size <= 100)
            val reply = client.get(url) {
                bearerAuth(token)
                parameter("pageSize", 100)
                if (page.isNotEmpty()) parameter("pageToken", page)
            }
            check(reply.status.isSuccess())
            val body = Json.parseToJsonElement(reply.bodyAsText()).jsonObject
            for (record in body["documents"]?.jsonArray.orEmpty()) {
                val doc = record.jsonObject
                val fields = doc.getValue("fields").jsonObject
                fun field(key: String) = fields[key]?.jsonObject?.get("stringValue")?.jsonPrimitive?.content
                val uuid = Uuid.parse(requireNotNull(field("uuid")))
                val id = field("appstoreId").orEmpty()
                val listing = when (field("appstoreSource")) {
                    CORE_FEED -> AppstoreListingId.parse("core:$id")
                    REBBLE_FEED -> AppstoreListingId.parse(id)?.takeUnless { it.core }
                    else -> null
                }
                val name = doc.getValue("name").jsonPrimitive.content
                require(name.substringBeforeLast('/') == url.substringAfter("/v1/"))
                documents += Document(SavedBuild(uuid, listing))
            }
            page = body["nextPageToken"]?.jsonPrimitive?.content.orEmpty()
        } while (page.isNotEmpty())
        return documents
    }

    suspend fun add(uid: String, token: String, uuid: Uuid, id: AppstoreListingId, current: () -> Boolean = { true }) =
        edit(uid, token, uuid, id, null, current)

    suspend fun remove(uid: String, token: String, uuid: Uuid, current: () -> Boolean = { true }) =
        edit(uid, token, uuid, null, null, current)

    suspend fun replace(uid: String, token: String, uuid: Uuid, from: AppstoreListingId, to: AppstoreListingId, current: () -> Boolean) =
        edit(uid, token, uuid, to, from, current)

    private suspend fun edit(uid: String, token: String, uuid: Uuid, target: AppstoreListingId?,
                             from: AppstoreListingId?, current: () -> Boolean): Boolean {
        val url = collection(uid, token)
        val root = url.substringBefore("/documents/") + "/documents"
        val collectionName = url.substringAfter("/v1/")
        suspend fun post(endpoint: String, body: JsonObject): JsonElement {
            val reply = client.post(endpoint) {
                bearerAuth(token)
                contentType(ContentType.Application.Json)
                setBody(body.toString())
            }
            check(reply.status.isSuccess()) { "Locker request failed (${reply.status.value})" }
            return Json.parseToJsonElement(reply.bodyAsText())
        }
        if (!current()) return false
        val query = buildJsonObject {
            put("from", buildJsonArray { add(buildJsonObject { put("collectionId", "entries") }) })
            put("where", buildJsonObject { put("fieldFilter", buildJsonObject {
                put("field", buildJsonObject { put("fieldPath", "uuid") })
                put("op", "EQUAL")
                put("value", buildJsonObject { put("stringValue", uuid.toString()) })
            }) })
        }
        val result = post(url.substringBeforeLast('/') + ":runQuery", buildJsonObject {
            put("structuredQuery", query)
        }).jsonArray
        val docs = result.mapNotNull { it.jsonObject["document"]?.jsonObject }
        val revisions = docs.associate { doc ->
            doc.getValue("name").jsonPrimitive.content to doc.getValue("updateTime").jsonPrimitive.content
        }
        fun precondition(name: String) = buildJsonObject {
            val revision = revisions[name]
            if (revision == null) put("exists", false) else put("updateTime", revision)
        }
        val existing = docs.map { doc ->
            val name = doc.getValue("name").jsonPrimitive.content
            require(name.substringBeforeLast('/') == collectionName)
            val fields = doc.getValue("fields").jsonObject
            fun field(key: String) = fields[key]?.jsonObject?.get("stringValue")?.jsonPrimitive?.content
            require(field("uuid") == uuid.toString())
            name to when (field("appstoreSource")) {
                CORE_FEED -> "core:" + field("appstoreId")
                REBBLE_FEED -> field("appstoreId")
                else -> null
            }
        }
        if (!current()) return false
        if (target != null && existing.isNotEmpty()) {
            if (existing.size == 1 && existing.single().second == target.value) return true
            // A copy must never change an existing flavour. An explicit replacement
            // may update the matching old source only, even after a concurrent retry.
            if (from == null || existing.size != 1 || existing.single().second != from.value) return false
        }
        if (target == null && existing.isEmpty()) return true
        val writes = buildJsonArray {
            val destination = target?.let { "$collectionName/${it.appId}-$uuid" }
            for ((name, _) in existing) if (name != destination) add(buildJsonObject {
                put("delete", name)
                put("currentDocument", precondition(name))
            })
            if (target != null) {
                val fields = buildJsonObject {
                    // Retain unrelated per-app fields when the document ID moves.
                    docs.singleOrNull()?.get("fields")?.jsonObject?.forEach { (key, value) -> put(key, value) }
                    for ((key, value) in mapOf("uuid" to uuid.toString(), "appstoreId" to target.appId,
                            "appstoreSource" to if (target.core) CORE_FEED else REBBLE_FEED)) {
                        put(key, buildJsonObject { put("stringValue", value) })
                    }
                    put("timelineToken", buildJsonObject { put("nullValue", JsonNull) })
                }
                add(buildJsonObject {
                    put("update", buildJsonObject { put("name", destination); put("fields", fields) })
                    put("currentDocument", precondition(requireNotNull(destination)))
                })
            }
        }
        if (!current()) return false
        post("$root:commit", buildJsonObject { put("writes", writes) })
        return true
    }

    suspend fun resolve(build: SavedBuild): LockerEntry? {
        val id = build.listing ?: return null
        val reply = client.get(id.lookupUrl)
        if (!reply.status.isSuccess()) return null
        val body = reply.bodyAsText()
        val app = Json.parseToJsonElement(body).jsonObject.getValue("data").jsonArray.single().jsonObject
        if (!app.getValue("id").jsonPrimitive.content.equals(id.appId, true)) return null
        val metadata = if (id.core) CoreAppstore.decodeMetadata(body, id) else RebbleAppstore.decodeMetadata(body, id.appId)
        if (metadata == null || metadata.uuid != build.uuid || metadata.releaseVersion.isNullOrBlank()) return null
        val compatibility = app.getValue("compatibility").jsonObject
        val platforms = metadata.platforms.filter { compatibility[it.name]?.jsonObject?.get("supported")?.jsonPrimitive?.booleanOrNull == true }
        val type = app.getValue("type").jsonPrimitive.content
        if (type !in setOf("watchapp", "watchface") || platforms.isEmpty()) return null
        require(java.net.URI(metadata.appstoreData.pbwLink).let { it.scheme == "https" && it.host != null && it.userInfo == null })
        val rawPlatforms = app.getValue("hardware_platforms").jsonArray.map { it.jsonObject }
            .associateBy { it.getValue("name").jsonPrimitive.content }
        val capabilities = app["capabilities"]?.let { it as? JsonArray }?.map { it.jsonPrimitive.content }.orEmpty()
        return LockerEntry(
            id = id.value, uuid = build.uuid.toString(), title = app.getValue("title").jsonPrimitive.content,
            type = type, category = app["category"]?.jsonPrimitive?.content.orEmpty(), version = metadata.releaseVersion,
            hearts = metadata.appstoreData.hearts, isConfigurable = "configurable" in capabilities,
            isTimelineEnabled = "timeline" in capabilities, links = LockerEntryLinks("", "", metadata.appstoreData.shareLink),
            developer = LockerEntryDeveloper(metadata.appstoreData.developerId, app["author"]?.jsonPrimitive?.content.orEmpty(), ""),
            hardwarePlatforms = platforms.map { platform ->
                val raw = rawPlatforms.getValue(platform.name)
                platform.copy(sdkVersion = raw.getValue("sdk_version").jsonPrimitive.content,
                    pebbleProcessInfoFlags = raw.getValue("pebble_process_info_flags").jsonPrimitive.int)
            },
            compatibility = json.decodeFromJsonElement(compatibility),
            capabilities = capabilities, companions = (app["companions"] as? JsonObject)?.let { json.decodeFromJsonElement<LockerEntryCompanions>(it) }
                ?: LockerEntryCompanions(null, null),
            pbw = LockerEntryPBW(metadata.appstoreData.pbwLink, app["icon_resource_id"]?.jsonPrimitive?.intOrNull ?: 0, ""),
            manifestVersion = app["manifest_version"]?.jsonPrimitive?.intOrNull,
            configPage = app["config_page"]?.jsonPrimitive?.contentOrNull,
            iconTintable = app["icon_tintable"]?.jsonPrimitive?.booleanOrNull == true,
            pluginManifest = app["plugin"] as? JsonObject,
            usesPermissions = (app["uses_permissions"] as? JsonArray)?.let { json.decodeFromJsonElement(it) },
            source = if (id.core) CORE_FEED else REBBLE_FEED,
        )
    }
}
