/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import org.freedesktop.dbus.types.UInt16
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.Variant
import kotlin.uuid.Uuid

private const val APP_MESSAGE_HEADER_SIZE = 19L
private const val APP_MESSAGE_TUPLE_HEADER_SIZE = 7L
private const val MAX_APP_MESSAGE_TUPLES = 255

private val ROCKPOOL_UUID = Regex(
    "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$",
)
private val NUMERIC_APP_KEY = Regex("-?(0|[1-9][0-9]*)")

internal fun parseRockpoolAppUuid(value: String): Uuid {
    val trimmed = value.trim()
    val unwrapped = when {
        trimmed.startsWith('{') && trimmed.endsWith('}') && trimmed.length > 2 ->
            trimmed.substring(1, trimmed.lastIndex)

        trimmed.startsWith('{') || trimmed.endsWith('}') ->
            throw IllegalArgumentException("Malformed application UUID")

        else -> trimmed
    }
    require(ROCKPOOL_UUID.matches(unwrapped)) { "Malformed application UUID" }
    return Uuid.parse(unwrapped)
}

/**
 * Convert the org.rockpool UI a{sv} representation into the exact value types accepted by
 * libpebble3's AppMessage encoder. Named keys can only be resolved when the running app supplies
 * its manifest map; numeric keys continue to work for callers which already know their app key.
 */
internal fun rockpoolAppMessageDictionary(
    values: Map<String, Variant<*>>,
    manifestAppKeys: Map<String, Int>,
    maximumPayloadSize: Int,
): Map<Int, Any> {
    require(maximumPayloadSize.toLong() >= APP_MESSAGE_HEADER_SIZE) {
        "Invalid AppMessage payload limit"
    }
    require(values.size <= MAX_APP_MESSAGE_TUPLES) { "Too many AppMessage values" }

    var encodedSize = APP_MESSAGE_HEADER_SIZE
    val result = linkedMapOf<Int, Any>()
    values.forEach { (name, variant) ->
        val key = resolveRockpoolAppKey(name, manifestAppKeys)
        require(!result.containsKey(key)) { "Duplicate AppMessage key $key" }

        val converted = convertRockpoolAppValue(variant)
        encodedSize += APP_MESSAGE_TUPLE_HEADER_SIZE + converted.encodedSize
        require(encodedSize <= maximumPayloadSize.toLong()) { "AppMessage payload is too large" }
        result[key] = converted.value
    }
    return result
}

private fun resolveRockpoolAppKey(name: String, manifestAppKeys: Map<String, Int>): Int {
    return manifestAppKeys[name] ?: run {
        require(NUMERIC_APP_KEY.matches(name)) { "Unknown AppMessage key '$name'" }
        val value = name.toLongOrNull()
            ?: throw IllegalArgumentException("AppMessage key is out of range")
        require(value in Int.MIN_VALUE.toLong()..UInt.MAX_VALUE.toLong()) {
            "AppMessage key is out of range"
        }
        // AppMessageDictionary predates Kotlin's UInt keys. Its encoder deliberately calls
        // Int.toUInt(), so retain the complete 32-bit key as that signed bit pattern.
        value.toInt()
    }
}

private data class ConvertedAppValue(
    val value: Any,
    val encodedSize: Int,
)

private fun convertRockpoolAppValue(variant: Variant<*>): ConvertedAppValue {
    val raw = variant.value
    return when (variant.sig) {
        "s" -> {
            val value = raw as? String ?: invalidVariant(variant)
            require('\u0000' !in value) { "AppMessage strings cannot contain NUL" }
            val size = value.encodeToByteArray().size.toLong() + 1L
            require(size <= UShort.MAX_VALUE.toLong()) { "AppMessage string is too large" }
            ConvertedAppValue(value, size.toInt())
        }

        "ay" -> {
            val value = (raw as? ByteArray)?.copyOf() ?: invalidVariant(variant)
            require(value.size <= UShort.MAX_VALUE.toInt()) { "AppMessage byte array is too large" }
            ConvertedAppValue(value, value.size)
        }

        "y" -> ConvertedAppValue(
            (raw as? Byte)?.toUByte() ?: invalidVariant(variant),
            UByte.SIZE_BYTES,
        )

        "n" -> ConvertedAppValue(
            raw as? Short ?: invalidVariant(variant),
            Short.SIZE_BYTES,
        )

        "q" -> ConvertedAppValue(
            (raw as? UInt16)?.toInt()?.toUShort() ?: invalidVariant(variant),
            UShort.SIZE_BYTES,
        )

        "i" -> ConvertedAppValue(
            raw as? Int ?: invalidVariant(variant),
            Int.SIZE_BYTES,
        )

        "u" -> ConvertedAppValue(
            (raw as? UInt32)?.toLong()?.toUInt() ?: invalidVariant(variant),
            UInt.SIZE_BYTES,
        )

        // The old backend encoded QVariant bools as one signed byte. Preserve that wire shape
        // instead of passing Boolean through libpebble3, where it is represented as a short.
        "b" -> ConvertedAppValue(
            if (raw as? Boolean ?: invalidVariant(variant)) 1.toByte() else 0.toByte(),
            Byte.SIZE_BYTES,
        )

        else -> throw IllegalArgumentException("Unsupported AppMessage D-Bus type '${variant.sig}'")
    }
}

private fun invalidVariant(variant: Variant<*>): Nothing =
    throw IllegalArgumentException("Invalid value for AppMessage D-Bus type '${variant.sig}'")
