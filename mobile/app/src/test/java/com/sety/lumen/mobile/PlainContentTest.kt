package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class PlainContentTest {
    @Test fun editingAPlainParagraphKeepsItsIdAndMetadata() {
        val original = LumenObject("journal-2098-11-01", "journal", "Diario", content = "Old text", properties = JSONObject().put("date", "2098-11-01"))
        val edited = original.withPlainContent("Old text\nNuevo texto")
        assertEquals(original.document.blocks().getJSONObject(0).getString("id"), edited.document.blocks().getJSONObject(0).getString("id"))
        assertEquals("Old text", original.document.plainText())
        assertEquals("2098-11-01", edited.properties.getString("date"))
        assertEquals("Old text\nNuevo texto", edited.content)
        assertFalse(edited.properties.has("blocks"))
        assertFalse(edited.properties.has("rich_text"))
        assertSame(original, original.withPlainContent(original.content))
    }
    @Test fun clearingAParagraphDoesNotDeleteItsIdentity() {
        val original = LumenObject("note", "note", "Note", content = "Delete me")
        val edited = original.withPlainContent("")
        assertEquals("", edited.document.plainText())
        assertEquals(original.document.blocks().getJSONObject(0).getString("id"), edited.document.blocks().getJSONObject(0).getString("id"))
    }
}
