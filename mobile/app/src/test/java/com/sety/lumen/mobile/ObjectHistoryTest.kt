package com.sety.lumen.mobile

import com.sety.lumen.mobile.ui.ObjectHistory
import org.junit.Assert.*
import org.junit.Test

class ObjectHistoryTest {
    @Test fun backReturnsThroughEachObjectToItsOrigin() {
        val history = ObjectHistory()
        history.open("journal-2026-11-01")
        val journalKey = history.current!!.key
        history.open("page")
        history.open("image")
        history.pop()
        assertEquals("page", history.current!!.objectId)
        history.pop()
        assertEquals("journal-2026-11-01", history.current!!.objectId)
        assertEquals(journalKey, history.current!!.key)
        history.pop()
        assertNull(history.current)
        history.pop()
    }

    @Test fun repeatedTapsDoNotAddStepsAndRevisitsHaveIndependentState() {
        val history = ObjectHistory()
        history.open("a")
        val firstKey = history.current!!.key
        history.open("a")
        assertEquals(1, history.entries.size)
        history.open("b")
        history.open("a")
        assertNotEquals(firstKey, history.current!!.key)
        history.pop()
        assertEquals("b", history.current!!.objectId)
        history.pop()
        assertEquals(firstKey, history.current!!.key)
    }
}
