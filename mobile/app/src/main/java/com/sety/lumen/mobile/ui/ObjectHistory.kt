package com.sety.lumen.mobile.ui

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import java.util.UUID

/** Each visit keeps its own screen state, including repeated visits to an object. */
class ObjectHistory {
    data class Entry(val objectId: String, val key: String = UUID.randomUUID().toString())
    var entries by mutableStateOf<List<Entry>>(emptyList())
        private set
    val current: Entry? get() = entries.lastOrNull()

    fun open(id: String) {
        if (current?.objectId != id) entries = entries + Entry(id)
    }

    fun pop() {
        if (entries.isNotEmpty()) entries = entries.dropLast(1)
    }
}
