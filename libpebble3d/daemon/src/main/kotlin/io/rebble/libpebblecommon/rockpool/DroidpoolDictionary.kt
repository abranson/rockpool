/* SPDX-License-Identifier: Apache-2.0 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.packets.AppMessageTuple
import java.util.Base64
import kotlinx.serialization.json.*

internal fun encodeDroidpoolDictionary(tuples: List<AppMessageTuple>) = buildJsonArray {
    for (tuple in tuples) addJsonObject {
        put("key", tuple.key.get().toInt())
        when (AppMessageTuple.Type.fromValue(tuple.type.get())) {
            AppMessageTuple.Type.Int -> {
                put("type", "int")
                put("length", tuple.dataLength.get().toInt())
                put("value", tuple.dataAsSignedNumber)
            }
            AppMessageTuple.Type.UInt -> {
                put("type", "uint")
                put("length", tuple.dataLength.get().toInt())
                put("value", tuple.dataAsUnsignedNumber.toLong())
            }
            AppMessageTuple.Type.CString -> {
                put("type", "string")
                put("length", 0)
                put("value", tuple.dataAsString)
            }
            AppMessageTuple.Type.ByteArray -> {
                put("type", "bytes")
                put("length", 0)
                put("value", Base64.getEncoder().encodeToString(tuple.dataAsBytes.toByteArray()))
            }
        }
    }
}

internal fun decodeDroidpoolDictionary(array: JsonArray, byteLimit: Int): Map<Int, Any> = buildMap {
    require(array.size <= 255)
    var wireSize = 19
    for (element in array) {
        val tuple = element.jsonObject
        val key = tuple.getValue("key").jsonPrimitive.int
        val value = tuple.getValue("value").jsonPrimitive
        val width = tuple.getValue("length").jsonPrimitive.int
        val type = tuple.getValue("type").jsonPrimitive.content
        require(!containsKey(key))
        wireSize += 7
        val decoded: Any = when (type) {
            "bytes" -> Base64.getDecoder().decode(value.content).also { wireSize += it.size }
            "string" -> value.content.also {
                require(value.isString && '\u0000' !in it)
                wireSize += it.toByteArray().size + 1
            }
            "int", "uint" -> {
                require(width == 1 || width == 2 || width == 4)
                val n = value.long
                val signBit = 1L shl (8 * width - 1)
                // The classic Java SDK serialises unsigned values using signed Java primitives.
                require(n >= -signBit && n < if (type == "int") signBit else 2 * signBit)
                wireSize += width
                when (width) {
                    1 -> if (type == "int") n.toByte() else n.toUByte()
                    2 -> if (type == "int") n.toShort() else n.toUShort()
                    else -> if (type == "int") n.toInt() else n.toUInt()
                }
            }
            else -> throw IllegalArgumentException("Unknown tuple type")
        }
        require(wireSize <= byteLimit)
        put(key, decoded)
    }
}
