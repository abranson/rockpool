/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.ui

import org.freedesktop.dbus.types.UInt16
import org.freedesktop.dbus.types.UInt32
import org.freedesktop.dbus.types.UInt64
import org.freedesktop.dbus.types.Variant
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class RockpoolAppMessageTest {
    @Test
    fun `compat UUID accepts Qt braces but rejects malformed input`() {
        val uuid = "12345678-1234-4abc-8def-1234567890ab"

        assertEquals(uuid, parseRockpoolAppUuid(uuid).toString())
        assertEquals(uuid, parseRockpoolAppUuid("{${uuid.uppercase()}}").toString())
        assertFailsWith<IllegalArgumentException> { parseRockpoolAppUuid("{$uuid") }
        assertFailsWith<IllegalArgumentException> { parseRockpoolAppUuid(uuid.replace("-", "")) }
        assertFailsWith<IllegalArgumentException> { parseRockpoolAppUuid("not-a-uuid") }
    }

    @Test
    fun `dictionary converts every supported D-Bus scalar and byte array`() {
        val bytes = byteArrayOf(0x00, 0x7f, 0x80.toByte(), 0xff.toByte())
        val converted = rockpoolAppMessageDictionary(
            values = linkedMapOf(
                "text" to Variant("hello"),
                "2" to Variant(bytes),
                "3" to Variant(0xfe.toByte()),
                "4" to Variant((-12).toShort()),
                "5" to Variant(UInt16(65_530)),
                "6" to Variant(-123_456),
                "7" to Variant(UInt32(4_000_000_000L)),
                "8" to Variant(true),
                "9" to Variant(false),
            ),
            manifestAppKeys = mapOf("text" to 1),
            maximumPayloadSize = 2_048,
        )
        bytes.fill(0)

        assertEquals("hello", converted.getValue(1))
        assertContentEquals(
            byteArrayOf(0x00, 0x7f, 0x80.toByte(), 0xff.toByte()),
            converted.getValue(2) as ByteArray,
        )
        assertEquals(0xfe.toUByte(), converted.getValue(3))
        assertEquals((-12).toShort(), converted.getValue(4))
        assertEquals(65_530.toUShort(), converted.getValue(5))
        assertEquals(-123_456, converted.getValue(6))
        assertEquals(4_000_000_000u, converted.getValue(7))
        assertEquals(1.toByte(), converted.getValue(8))
        assertEquals(0.toByte(), converted.getValue(9))
    }

    @Test
    fun `dictionary resolves manifest names and rejects ambiguous or unknown keys`() {
        assertEquals(
            mapOf(42 to 7),
            rockpoolAppMessageDictionary(
                values = mapOf("answer" to Variant(7)),
                manifestAppKeys = mapOf("answer" to 42),
                maximumPayloadSize = 2_048,
            ),
        )

        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(
                values = mapOf("unknown" to Variant(7)),
                manifestAppKeys = emptyMap(),
                maximumPayloadSize = 2_048,
            )
        }
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(
                values = linkedMapOf("answer" to Variant(7), "42" to Variant(8)),
                manifestAppKeys = mapOf("answer" to 42),
                maximumPayloadSize = 2_048,
            )
        }
        listOf("01", "+1", "-2147483649", "4294967296").forEach { key ->
            assertFailsWith<IllegalArgumentException> {
                rockpoolAppMessageDictionary(
                    values = mapOf(key to Variant(7)),
                    manifestAppKeys = emptyMap(),
                    maximumPayloadSize = 2_048,
                )
            }
        }
    }

    @Test
    fun `dictionary preserves all unsigned 32-bit key patterns`() {
        val converted = rockpoolAppMessageDictionary(
            values = linkedMapOf(
                "2147483648" to Variant(1),
                "4294967295" to Variant(2),
            ),
            manifestAppKeys = mapOf("manifestMax" to -1),
            maximumPayloadSize = 2_048,
        )

        assertEquals(1, converted.getValue(Int.MIN_VALUE))
        assertEquals(2, converted.getValue(-1))
        assertEquals(
            3,
            rockpoolAppMessageDictionary(
                values = mapOf("manifestMax" to Variant(3)),
                manifestAppKeys = mapOf("manifestMax" to -1),
                maximumPayloadSize = 2_048,
            ).getValue(-1),
        )
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(
                values = linkedMapOf("4294967295" to Variant(1), "manifestMax" to Variant(2)),
                manifestAppKeys = mapOf("manifestMax" to -1),
                maximumPayloadSize = 2_048,
            )
        }
    }

    @Test
    fun `dictionary rejects unsupported signatures and malformed strings`() {
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(
                values = mapOf("1" to Variant(UInt64(1))),
                manifestAppKeys = emptyMap(),
                maximumPayloadSize = 2_048,
            )
        }
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(
                values = mapOf("1" to Variant(1.5)),
                manifestAppKeys = emptyMap(),
                maximumPayloadSize = 2_048,
            )
        }
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(
                values = mapOf("1" to Variant("before\u0000after")),
                manifestAppKeys = emptyMap(),
                maximumPayloadSize = 2_048,
            )
        }
    }

    @Test
    fun `dictionary enforces tuple count and encoded payload bounds`() {
        val tooMany = (0..255).associate { it.toString() to Variant(1.toByte()) }
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(tooMany, emptyMap(), 8_222)
        }

        // 19-byte AppMessage header + 7-byte tuple header + 5 bytes including CString NUL.
        assertEquals(
            mapOf(1 to "four"),
            rockpoolAppMessageDictionary(mapOf("1" to Variant("four")), emptyMap(), 31),
        )
        assertFailsWith<IllegalArgumentException> {
            rockpoolAppMessageDictionary(mapOf("1" to Variant("four")), emptyMap(), 30)
        }
    }
}
