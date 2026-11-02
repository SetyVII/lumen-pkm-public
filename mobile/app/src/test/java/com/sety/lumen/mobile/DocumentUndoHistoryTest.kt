package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.*
import org.junit.Assert.*
import org.junit.Test

class DocumentUndoHistoryTest {
    @Test
    fun structuralGroupDeleteAndUndoRestoreImageAndSubtreeIdentities() {
        val image =
            documentNode(
                "image",
                JSONObject().put("asset_id", "asset").put("width", 320).put("caption", "Caption"),
            )
        val child = documentParagraph("Nested")
        val parent = documentNode("group", children = JSONArray().put(child))
        val before = CanonicalDocument.empty().withBlocks(JSONArray().put(image).put(parent))
        val selected = setOf(image.getString("id"), parent.getString("id"), child.getString("id"))
        val grouped = groupSelectedDocumentBlocks(before, selected)
        assertEquals(1, grouped.blocks().length())
        val children = grouped.blocks().getJSONObject(0).getJSONArray("children")
        assertEquals(image.getString("id"), children.getJSONObject(0).getString("id"))
        assertEquals(
            child.getString("id"),
            children.getJSONObject(1).getJSONArray("children").getJSONObject(0).getString("id"),
        )
        val history = DocumentUndoHistory()
        assertTrue(history.record(before, grouped))
        val empty =
            removeSelectedDocumentBlocks(
                grouped,
                setOf(grouped.blocks().getJSONObject(0).getString("id")),
            )
        assertTrue(history.record(grouped, empty))
        assertEquals(0, empty.blocks().length())
        val first = history.undo(empty)!!
        assertTrue(first.semanticallyEquals(grouped))
        val restored = history.undo(first)!!
        assertTrue(restored.semanticallyEquals(before))
        assertTrue(history.redo(restored)!!.semanticallyEquals(grouped))
    }

    @Test
    fun redoIsInvalidatedByNewEditAndHistoryIsBounded() {
        val history = DocumentUndoHistory(limit = 1)
        val a = CanonicalDocument.fromText("A")
        val b = CanonicalDocument.fromText("B")
        val c = CanonicalDocument.fromText("C")
        history.record(a, b)
        history.record(b, c)
        val undone = history.undo(c)!!
        assertTrue(undone.semanticallyEquals(b))
        assertFalse(history.canUndo)
        assertFalse(history.record(b, b))
        assertTrue(history.canRedo)
        history.record(b, a)
        assertFalse(history.canRedo)
        history.reset()
        assertFalse(history.canUndo)
    }
}
