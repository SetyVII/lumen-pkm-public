package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NativeDocumentTest {
    @Test fun toggleStateAndEditedChildrenSurviveSerialization() {
        val child = documentParagraph("Dentro").put("attrs", JSONObject().put("future", "kept"))
        val toggle = documentParagraph("Título").put("type", "toggle").put("attrs", JSONObject().put("expanded", true)).put("children", JSONArray().put(child))
        val before = LumenObject("source", "note", "Prueba").withDocument(CanonicalDocument.empty().withBlocks(JSONArray().put(toggle)))
        val changed = before.document.blocks()
        changed.getJSONObject(0).getJSONObject("attrs").put("expanded", false)
        val received = LumenObject.fromJson(before.withDocument(before.document.withBlocks(changed)).toJson())
        assertFalse(received.document.blocks().getJSONObject(0).getJSONObject("attrs").getBoolean("expanded"))
        assertEquals(child.toString(), received.document.blocks().getJSONObject(0).getJSONArray("children").getJSONObject(0).toString())
        assertEquals("Título\nDentro", received.document.plainText())
    }
    @Test fun editingAListKeepsItsSemanticKindChildrenAndInlineMarks() {
        val item = documentParagraph("Principal").put("type", "list_item").put("attrs", JSONObject().put("kind", "roman").put("case", "upper"))
            .put("children", JSONArray().put(documentParagraph("Hijo")))
        item.getJSONArray("content").getJSONObject(0).put("marks", JSONObject().put("bold", true))
        val changed = editDocumentText(item, "Principal editado")
        assertEquals("roman", changed.getJSONObject("attrs").getString("kind"))
        assertEquals(item.getJSONArray("children").toString(), changed.getJSONArray("children").toString())
        assertEquals("Principal editado", documentInlineText(changed.getJSONArray("content")))
        assertTrue(changed.getJSONArray("content").getJSONObject(0).getJSONObject("marks").getBoolean("bold"))
    }
    @Test fun codeIsSourceNotMarkdownOrGlyphMarkers() {
        val code = documentNode("code", JSONObject().put("source", "**literal**\nval x = 1").put("language", "kotlin"))
        val obj = LumenObject("source", "pdf", "Documento").withDocument(CanonicalDocument.empty().withBlocks(JSONArray().put(code)))
        val received = LumenObject.fromJson(obj.toJson()).document.blocks().getJSONObject(0)
        assertEquals("kotlin", received.getJSONObject("attrs").getString("language"))
        assertEquals("**literal**\nval x = 1", received.getJSONObject("attrs").getString("source"))
        assertEquals(code.getString("id"), received.getString("id"))
    }
}
