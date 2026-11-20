package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.LibPebble
import io.rebble.libpebblecommon.disk.pbw.PbwApp
import io.rebble.libpebblecommon.permissions.requestedPermissions
import io.rebble.libpebblecommon.permissions.InstallReview
import io.rebble.libpebblecommon.permissions.PluginDefinedPermission
import io.rebble.libpebblecommon.permissions.fileScopeOf
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.*
import org.freedesktop.dbus.annotations.DBusInterfaceName
import org.freedesktop.dbus.connections.impl.DBusConnection
import org.freedesktop.dbus.interfaces.DBus
import org.freedesktop.dbus.interfaces.DBusInterface
import org.freedesktop.dbus.types.UInt32
import kotlin.uuid.Uuid

internal suspend fun LibPebble.reviewPbwPermissions(
    pbw: PbwApp,
    prompt: suspend (InstallReview) -> Map<String, Boolean>?,
): Map<String, Boolean>? {
    val info = pbw.info
    val uuid = Uuid.parse(info.uuid)
    val name = info.longName.ifBlank { info.shortName }
    val requested = pbw.requestedPermissions()
    val review = reviewInstall(uuid, name, requested) ?: return emptyMap()
    val decisions = prompt(review) ?: return null
    // An existing permission may have been changed while the dialog was open.
    return decisions.takeIf { review == reviewInstall(uuid, name, requested) }
}

@DBusInterfaceName("org.rockpool.UI")
internal interface RockpoolInstallReviewUi : DBusInterface {
    fun OpenInstallReview(token: String, reviewJson: String): Boolean
}

private const val UI_SERVICE = "org.rockpool.UI"
private const val BUS = "org.freedesktop.DBus"
private const val BUS_PATH = "/org/freedesktop/DBus"

internal class RockpoolInstallReviews {
    private val dialog = Mutex()
    @Volatile private var pending: Request? = null

    private class Request(val connection: DBusConnection, val owner: String, val review: InstallReview) {
        val token = Uuid.random().toString()
        val answer = CompletableDeferred<Map<String, Boolean>?>()
    }

    fun watchUiOwner(connection: DBusConnection) {
        connection.addSigHandler(DBus.NameOwnerChanged::class.java) { event ->
            if (event.source == BUS && event.path == BUS_PATH && event.name == UI_SERVICE) {
                pending?.takeIf { it.connection === connection && it.owner == event.oldOwner }?.answer?.complete(null)
            }
        }
    }

    fun disconnected(connection: DBusConnection) {
        pending?.takeIf { it.connection === connection }?.answer?.complete(null)
    }

    suspend fun request(connection: DBusConnection, review: InstallReview,
                        labels: Map<String, PluginDefinedPermission>): Map<String, Boolean>? = dialog.withLock {
        // Only a folder chosen by the user can create a scoped filesystem grant.
        val editable = review.copy(toReview = review.toReview.filter { fileScopeOf(it) == null },
                                   automatic = review.automatic.filter { fileScopeOf(it) == null })
        if (editable.toReview.isEmpty() && editable.automatic.isEmpty()) return@withLock emptyMap()
        val json = buildJsonObject {
            put("appName", editable.appName)
            putJsonArray("permissions") {
                for (key in editable.toReview + editable.automatic) addJsonObject {
                    put("key", key)
                    put("label", labels[key]?.label.orEmpty())
                    put("reason", editable.reasonFor(key).orEmpty())
                    put("required", editable.isRequired(key))
                    put("automatic", key in editable.automatic)
                    put("needsAnswer", key in editable.toReview && editable.needsAnswer(key))
                }
            }
        }.toString()
        withContext(Dispatchers.IO) {
            val bus = connection.getRemoteObject(BUS, BUS_PATH, DBus::class.java)
            if (!bus.NameHasOwner(UI_SERVICE)) bus.StartServiceByName(UI_SERVICE, UInt32(0))
            val owner = bus.GetNameOwner(UI_SERVICE)
            val request = Request(connection, owner, editable)
            pending = request
            try {
                val ui = connection.getRemoteObject(owner, "/org/rockpool/UI", RockpoolInstallReviewUi::class.java)
                if (bus.GetNameOwner(UI_SERVICE) != owner || !ui.OpenInstallReview(request.token, json)) null
                else request.answer.await()
            } finally {
                pending = null
            }
        }
    }

    fun answer(owner: String, token: String, json: String): Boolean {
        val request = pending?.takeIf { it.token == token && it.owner == owner } ?: return false
        val value = runCatching { Json.parseToJsonElement(json) }.getOrNull() ?: return false
        if (value == JsonNull) return request.answer.complete(null)
        if (value !is JsonObject) return false
        val review = request.review
        val choices = mutableMapOf<String, Boolean>()
        for ((key, element) in value) {
            if (key !in review.toReview && key !in review.automatic) return false
            if (element !is JsonPrimitive || element.isString) return false
            choices[key] = element.booleanOrNull ?: return false
        }
        if (review.toReview.any { review.needsAnswer(it) && it !in choices }) return false
        return request.answer.complete(choices)
    }
}
