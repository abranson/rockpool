/*
 * SPDX-License-Identifier: Apache-2.0
 */
package io.rebble.libpebblecommon.rockpool

import io.rebble.libpebblecommon.connection.BondedWatchImportOutcome
import java.io.IOException
import kotlinx.coroutines.runBlocking
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs
import kotlin.test.assertTrue

class BondedWatchImportOperationTest {
    @Test
    fun `completed import preserves all aggregate counters`() = runBlocking {
        var commitRequested = false

        val result = runBondedWatchImport(
            importer = { beginCommit ->
                commitRequested = beginCommit()
                BondedWatchImportOutcome.Completed(3, 2, 1, 4)
            },
            beginCommit = { true },
        )

        assertTrue(commitRequested)
        assertEquals(
            BondedWatchImportOperationResult.Completed(3, 2, 1, 4),
            result,
        )
    }

    @Test
    fun `unavailable import is a transport failure`() = runBlocking {
        val result = runBondedWatchImport(
            importer = { BondedWatchImportOutcome.Unavailable },
            beginCommit = { error("no writes should be committed") },
        )

        assertEquals(
            BondedWatchImportOperationResult.Failed(
                ERROR_TRANSPORT_FAILED,
                "bond enumeration is unavailable",
            ),
            result,
        )
    }

    @Test
    fun `cancelled and persistence outcomes retain their stable errors`() = runBlocking {
        val cancelled = runBondedWatchImport(
            importer = { BondedWatchImportOutcome.Cancelled },
            beginCommit = { false },
        )
        val persistenceFailed = runBondedWatchImport(
            importer = { BondedWatchImportOutcome.PersistenceFailed },
            beginCommit = { false },
        )

        assertEquals(ERROR_CANCELLED, assertIs<BondedWatchImportOperationResult.Failed>(cancelled).error)
        assertEquals(ERROR_IO, assertIs<BondedWatchImportOperationResult.Failed>(persistenceFailed).error)
    }

    @Test
    fun `io exception is sanitized as an io failure`() = runBlocking {
        val result = runBondedWatchImport(
            importer = { throw IOException("database location") },
            beginCommit = { false },
        )

        assertEquals(ERROR_IO, assertIs<BondedWatchImportOperationResult.Failed>(result).error)
    }
}
