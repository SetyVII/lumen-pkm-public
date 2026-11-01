package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.MemoryReadCache
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlin.concurrent.thread

class MemoryReadCacheTest {
    @Test fun repeatedReadsReuseTheDecryptedSnapshotUntilInvalidated() {
        val cache = MemoryReadCache(); var reads = 0
        fun read() = cache.read("library") { reads++; Any() }
        val original = read()
        repeat(100) { assertSame(original, read()) }
        assertEquals(1, reads)
        cache.clear()
        assertNotSame(original, read()); assertEquals(2, reads)
    }
    @Test fun missingRecordsAreCachedAndRefetchedAfterAWrite() {
        val cache = MemoryReadCache(); var reads = 0
        repeat(10) { assertNull(cache.read<Any?>("missing") { reads++; null }) }
        assertEquals(1, reads)
        cache.clear()
        assertEquals("created", cache.read("missing") { "created" })
    }
    @Test fun aReadStartedBeforeLockCannotRepublishDecryptedDataAfterLock() {
        val cache = MemoryReadCache(); val started = CountDownLatch(1); val finish = CountDownLatch(1)
        val worker = thread {
            cache.read("note") { started.countDown(); check(finish.await(5, TimeUnit.SECONDS)); "old decrypted value" }
        }
        check(started.await(5, TimeUnit.SECONDS)); cache.clear(); finish.countDown(); worker.join(5000)
        assertFalse(worker.isAlive)
        assertEquals("fresh session", cache.read("note") { "fresh session" })
    }
}
