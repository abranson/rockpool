/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import io.rebble.libpebblecommon.connection.CommonConnectedDevice
import io.rebble.libpebblecommon.connection.ConnectedPebbleDevice
import io.rebble.libpebblecommon.connection.PebbleDevice
import io.rebble.libpebblecommon.connection.endpointmanager.LanguagePackInstallState
import kotlinx.io.files.Path
import java.net.URI
import java.nio.file.Paths

internal sealed interface RockpoolLanguagePackSource {
    val name: String

    data class Remote(val url: String, override val name: String) : RockpoolLanguagePackSource
    data class Local(val path: Path, override val name: String) : RockpoolLanguagePackSource
}

/** Interpret the legacy one-string argument, which accepted downloads as well as local files. */
internal fun parseRockpoolLanguagePackSource(value: String): RockpoolLanguagePackSource {
    require(value.isNotBlank()) { "Language-pack location is empty" }
    return when {
        value.startsWith("http://", ignoreCase = true) ||
            value.startsWith("https://", ignoreCase = true) -> {
            val uri = runCatching { URI(value) }
                .getOrElse {
                    throw IllegalArgumentException("Language-pack URL is invalid", it)
                }
            require(uri.host != null) { "Language-pack URL has no host" }
            RockpoolLanguagePackSource.Remote(value, languagePackName(uri.path))
        }

        value.startsWith("file://", ignoreCase = true) -> {
            // Qt callers historically passed both correctly escaped file URLs and raw local
            // strings such as "file:///tmp/Italian pack.pbl".  Prefer URI decoding, but retain
            // that legacy raw-path fallback for spaces and literal percent characters.
            val local = runCatching { Paths.get(URI(value)) }
                .getOrElse { Paths.get(value.substring(FILE_URL_PREFIX_LENGTH)) }
            RockpoolLanguagePackSource.Local(Path(local.toString()), languagePackName(local.toString()))
        }

        "://" in value -> throw IllegalArgumentException("Unsupported language-pack URL scheme")
        else -> RockpoolLanguagePackSource.Local(Path(value), languagePackName(value))
    }
}

internal fun installRockpoolLanguagePack(
    source: RockpoolLanguagePackSource,
    installRemote: (String, String) -> Unit,
    installLocal: (Path, String) -> Unit,
) {
    when (source) {
        is RockpoolLanguagePackSource.Remote ->
            installRemote(source.url, source.name)

        is RockpoolLanguagePackSource.Local ->
            installLocal(source.path, source.name)
    }
}

private fun languagePackName(path: String?): String {
    val leaf = path.orEmpty().substringAfterLast('/').substringAfterLast('\\')
    return leaf.substringBeforeLast('.', leaf).ifBlank { "language-pack" }.take(128)
}

private const val FILE_URL_PREFIX_LENGTH = 7

internal data class RockpoolLanguageStatus(
    val version: String,
    val completedInstall: String?,
) {
    fun shouldSignalAfter(previous: RockpoolLanguageStatus): Boolean =
        version != previous.version ||
            (completedInstall != null && completedInstall != previous.completedInstall)
}

internal fun rockpoolLanguageStatus(device: PebbleDevice?): RockpoolLanguageStatus {
    val connected = device as? CommonConnectedDevice
        ?: return RockpoolLanguageStatus(version = "", completedInstall = null)
    val info = connected.watchInfo
    val completed = ((device as? ConnectedPebbleDevice)?.languagePackInstallState
        as? LanguagePackInstallState.Idle)
        ?.successfullyInstalledLanguage
    return RockpoolLanguageStatus(
        version = "${info.language}:${info.languageVersion}",
        completedInstall = completed,
    )
}
