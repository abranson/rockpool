/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.ktor.client.HttpClient
import io.ktor.client.engine.cio.CIO
import io.ktor.client.plugins.HttpTimeout
import io.ktor.client.request.bearerAuth
import io.ktor.client.request.delete
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpHeaders
import io.ktor.http.HttpStatusCode
import io.ktor.http.isSuccess
import io.rebble.libpebblecommon.linux.web.AppstoreListingId
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

internal data class AppVote(val voted: Boolean, val hearts: Int)
internal class AppVoteRequestFailed(val authentication: Boolean = false) : Exception()

internal class AccountAppVote(private val client: HttpClient = voteHttpClient()) {
    private fun base(app: AppstoreListingId): String = if (app.core) CORE_API else REBBLE_API

    private suspend fun voted(token: String, app: AppstoreListingId, coreUid: String?): Boolean {
        if (app.core != (coreUid != null)) throw AppVoteRequestFailed(authentication = true)
        val version = if (app.core) "v1" else "v0"
        val response = client.get("${base(app)}/$version/users/me") {
            bearerAuth(token)
            header(HttpHeaders.CacheControl, "no-cache")
        }
        requireVoteSuccess(response.status)
        val body = Json.parseToJsonElement(response.bodyAsText()).jsonObject
        val user = if (app.core) {
            val uid = body["uid"]?.jsonPrimitive
            if (uid?.isString != true || uid.content != coreUid) {
                throw AppVoteRequestFailed(authentication = true)
            }
            body
        } else {
            body.getValue("users").jsonArray.single().jsonObject
        }
        return user.getValue("voted_ids").jsonArray.any {
            val id = it.jsonPrimitive
            id.isString && id.content.equals(app.appId, ignoreCase = true)
        }
    }

    private suspend fun hearts(app: AppstoreListingId): Int {
        val response = client.get("${base(app)}/v1/apps/id/${app.appId.lowercase()}") {
            header(HttpHeaders.CacheControl, "no-cache")
        }
        if (!response.status.isSuccess()) throw AppVoteRequestFailed()
        val apps = Json.parseToJsonElement(response.bodyAsText()).jsonObject.getValue("data").jsonArray
        val listing = apps.single().jsonObject
        check(listing.getValue("id").jsonPrimitive.content.equals(app.appId, ignoreCase = true))
        return listing.getValue("hearts").jsonPrimitive.int.also { check(it >= 0) }
    }

    suspend fun get(token: String, app: AppstoreListingId, coreUid: String? = null): AppVote =
        AppVote(voted(token, app, coreUid), hearts(app))

    suspend fun set(
        token: String,
        app: AppstoreListingId,
        desired: Boolean,
        coreUid: String? = null,
        beginCommit: () -> Boolean,
    ): AppVote? {
        if (voted(token, app, coreUid) != desired) {
            if (!beginCommit()) return null
            changeVote(token, app, desired)
        }
        return get(token, app, coreUid).also { if (it.voted != desired) throw AppVoteRequestFailed() }
    }

    private suspend fun changeVote(token: String, app: AppstoreListingId, desired: Boolean) {
        val id = app.appId.lowercase()
        val status = if (app.core) {
            val url = "$CORE_API/v1/apps/id/$id/heart"
            if (desired) client.post(url) { bearerAuth(token) }.status
            else client.delete(url) { bearerAuth(token) }.status
        } else {
            val action = if (desired) "add_heart" else "remove_heart"
            client.post("$REBBLE_API/v0/applications/$id/$action") { bearerAuth(token) }.status
        }
        // A concurrent client may have voted between our initial read and the Core POST.
        // The mandatory read-back still verifies the requested state.
        if (!(app.core && desired && status == HttpStatusCode.Conflict)) requireVoteSuccess(status)
    }

    private fun requireVoteSuccess(status: HttpStatusCode) {
        if (!status.isSuccess()) {
            throw AppVoteRequestFailed(status == HttpStatusCode.Unauthorized || status == HttpStatusCode.Forbidden)
        }
    }

    private companion object {
        const val REBBLE_API = "https://appstore-api.rebble.io/api"
        const val CORE_API = "https://appstore-api.repebble.com/api"
    }
}

private fun voteHttpClient(): HttpClient = HttpClient(CIO) {
    followRedirects = false
    install(HttpTimeout) {
        requestTimeoutMillis = 15_000L
        connectTimeoutMillis = 5_000L
        socketTimeoutMillis = 10_000L
    }
}
