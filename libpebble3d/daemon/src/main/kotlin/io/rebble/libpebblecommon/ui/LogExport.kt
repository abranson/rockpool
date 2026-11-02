package io.rebble.libpebblecommon.ui

import java.io.Writer
import java.nio.channels.Channels
import java.nio.file.FileAlreadyExistsException
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.OpenOption
import java.nio.file.Paths
import java.nio.file.SecureDirectoryStream
import java.nio.file.StandardOpenOption
import java.nio.file.attribute.PosixFilePermissions

// Only package-selected log names are accepted. Directory handles prevent symlink
// swaps; a private temporary file is atomically published after a complete export.
internal inline fun writeLogExport(
    fileName: String,
    homePath: java.nio.file.Path = Paths.get(
        System.getProperty("user.home")?.takeIf { it.isNotBlank() }
            ?: error("User home directory is unavailable")
    ),
    write: (Writer) -> Unit,
): java.nio.file.Path {
    require(fileName == "pebble.log" || fileName == "libpebble3d.log")
    require(homePath.isAbsolute && Files.isDirectory(homePath, LinkOption.NOFOLLOW_LINKS)) {
        "User home is not a directory"
    }

    val downloadsName = Paths.get("Downloads")
    val downloadsPath = homePath.resolve(downloadsName)
    if (!Files.exists(downloadsPath, LinkOption.NOFOLLOW_LINKS)) {
        try {
            Files.createDirectory(downloadsPath)
        } catch (_: FileAlreadyExistsException) {
            // A concurrent creator won the race; validate it through the directory handle below.
        }
    }

    Files.newDirectoryStream(homePath).use { homeDirectory ->
        @Suppress("UNCHECKED_CAST")
        val secureHome = homeDirectory as? SecureDirectoryStream<java.nio.file.Path>
            ?: error("Secure directory access is unavailable")
        secureHome.newDirectoryStream(downloadsName, LinkOption.NOFOLLOW_LINKS).use { downloads ->
            val targetName = Paths.get(fileName)
            val temporaryName =
                Paths.get(".$fileName.${java.util.UUID.randomUUID()}.tmp")
            var temporaryExists = false
            try {
                val options = setOf<OpenOption>(
                    StandardOpenOption.CREATE_NEW,
                    StandardOpenOption.WRITE,
                    LinkOption.NOFOLLOW_LINKS,
                )
                downloads.newByteChannel(temporaryName, options,
                    PosixFilePermissions.asFileAttribute(
                        PosixFilePermissions.fromString("rw-------")
                    )).use { channel ->
                    temporaryExists = true
                    Channels.newOutputStream(channel).bufferedWriter().use { output ->
                        write(output)
                    }
                }

                try {
                    downloads.move(temporaryName, downloads, targetName)
                } catch (_: FileAlreadyExistsException) {
                    // Unix replaces the entry atomically. Retain a safe fallback for providers
                    // that require the existing target to be removed first.
                    downloads.deleteFile(targetName)
                    downloads.move(temporaryName, downloads, targetName)
                }
                temporaryExists = false
            } finally {
                if (temporaryExists) {
                    runCatching { downloads.deleteFile(temporaryName) }
                }
            }
        }
    }

    return downloadsPath.resolve(fileName)
}
