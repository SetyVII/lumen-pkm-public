package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.LumenObject
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class PlainContentTest {
    @Test
    fun editedTextExplicitlyInvalidatesServerFormatsWithoutMutatingTheOriginal() {
        val original = LumenObject(
            id = "journal-2098-11-01", typeId = "journal", title = "Daily note", content = "Old text",
            properties = JSONObject().put("date", "2098-11-01")
                .put("blocks", JSONArray().put(JSONObject().put("id", "original-block").put("type", "paragraph")
                    .put("text", "Old text").put("runs", JSONArray().put(JSONObject().put("text", "Old text")))))
                .put("rich_text", "<p>Old text</p>"),
            links = listOf("linked-note"),
        )
        val edited = original.withPlainContent("Old text\nTest desde el movil")
        val wire = JSONObject(edited.toJson().toString()).getJSONObject("properties")
        assertTrue(wire.has("rich_text") && wire.isNull("rich_text"))
        assertEquals(2, wire.getJSONArray("blocks").length())
        assertEquals("Old text", original.properties.getJSONArray("blocks").getJSONObject(0).getString("text"))
        assertEquals("<p>Old text</p>", original.properties.getString("rich_text"))
        assertEquals("2098-11-01", wire.getString("date"))
        assertEquals(original.links, edited.links)
        assertEquals("Old text\nTest desde el movil", edited.content)
        assertSame(original, original.withPlainContent(original.content))
    }

    @Test
    fun deletingAllTextAlsoInvalidatesTheNativeFormats() {
        val original = LumenObject("note", "note", "Note", content = "Delete me")
        val edited = original.withPlainContent("")
        assertEquals("", edited.content)
        assertEquals("", edited.properties.getJSONArray("blocks").getJSONObject(0).getString("text"))
        assertTrue(edited.properties.has("rich_text") && edited.properties.isNull("rich_text"))
    }
}
