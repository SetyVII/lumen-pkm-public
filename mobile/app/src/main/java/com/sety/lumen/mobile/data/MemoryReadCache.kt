package com.sety.lumen.mobile.data

import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicLong

/** Decrypted read results live only in RAM. Invalidating during a slow read
 * prevents that read from republishing an old value after a write or lock. */
internal class MemoryReadCache(private val capacity: Int = 128) {
    private data class Entry(val generation: Long, val value: Any?)
    private val generation = AtomicLong()
    private val entries = ConcurrentHashMap<String, Entry>()

    @Suppress("UNCHECKED_CAST")
    fun <T> read(key: String, load: () -> T): T {
        val version = generation.get()
        entries[key]?.takeIf { it.generation == version }?.let { return it.value as T }
        val value = load()
        synchronized(this) {
            if (version == generation.get()) {
                if (entries.size >= capacity) entries.clear()
                entries[key] = Entry(version, value)
            }
        }
        return value
    }

    @Synchronized fun clear() { generation.incrementAndGet(); entries.clear() }
}
