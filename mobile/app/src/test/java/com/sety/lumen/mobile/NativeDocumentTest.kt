package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class NativeDocumentTest {
    @Test
    fun toggleStateAndEditedChildrenSurviveSerialization() {
        val child = nativeParagraph("Contenido editable").put("extra", "retain")
        val toggle = nativeToggle().put("children", JSONArray().put(child))
        val closed = setToggleExpanded(editToggleTitle(toggle, "Mi toggle"), false)
        assertFalse(toggleIsExpanded(closed))
        assertEquals("▶ Mi toggle\nContenido editable", nativeBlockText(closed))
        assertEquals(child.toString(), closed.getJSONArray("children").getJSONObject(0).toString())
        val reopened = setToggleExpanded(JSONObject(closed.toString()), true)
        assertTrue(toggleIsExpanded(reopened))
        assertEquals("▼ Mi toggle", reopened.getString("text"))
        assertEquals(reopened.getString("text"), (0 until reopened.getJSONArray("runs").length()).joinToString("") {
            reopened.getJSONArray("runs").getJSONObject(it).getString("text")
        })
        val source = LumenObject("toggle", "note", "Test").withNativeBlocks(JSONArray().put(reopened))
        assertEquals("▼ Mi toggle\nContenido editable", source.content)
    }

    private fun block(id: String, type: String, text: String, vararg runs: JSONObject) = JSONObject()
        .put("id", id).put("type", type).put("text", text).put("runs", JSONArray(runs.toList()))

    private fun fixture(): LumenObject {
        val blocks = JSONArray()
            .put(block("bullet", "bullet_list", "Viñeta", JSONObject().put("text", "Viñeta").put("bold", true)).put("indent", 2))
            .put(block("image-a", "embed", "\uFFFC", JSONObject().put("text", "\uFFFC").put("type", "image")
                .put("object_id", "image-a").put("image_name", "lumen://image/image-a").put("width", 499).put("height", 146)))
            .put(block("heading", "heading", "Título", JSONObject().put("text", "Título")).put("level", 2))
            .put(block("code", "code", "val x = 1", JSONObject().put("text", "val x = 1").put("code", true)).put("language", "kotlin"))
            .put(block("image-b", "embed", "\uFFFC", JSONObject().put("text", "\uFFFC").put("type", "image")
                .put("object_id", "image-b").put("image_name", "lumen://image/image-b").put("width", 640).put("height", 849)))
        return LumenObject("native-note", "note", "Nota", content = nativeDocumentText(blocks),
            properties = JSONObject().put("blocks", blocks).put("custom", "retain"))
    }

    @Test
    fun editingABulletKeepsItsTypeIndentRunsAndBothInlineImages() {
        val original = fixture()
        val edited = original.withPlainContent(original.content.replace("Viñeta", "Viñeta editada"))
        val before = original.properties.getJSONArray("blocks")
        val after = edited.properties.getJSONArray("blocks")
        assertEquals(5, after.length())
        assertEquals("bullet_list", after.getJSONObject(0).getString("type"))
        assertEquals("bullet", after.getJSONObject(0).getString("id"))
        assertEquals(2, after.getJSONObject(0).getInt("indent"))
        assertTrue(after.getJSONObject(0).getJSONArray("runs").getJSONObject(0).getBoolean("bold"))
        for (i in 1 until before.length()) assertEquals(before.getJSONObject(i).toString(), after.getJSONObject(i).toString())
        assertEquals(setOf("image-a", "image-b"), embeddedImageIds(edited))
        assertEquals("retain", edited.properties.getString("custom"))
        assertEquals("Viñeta", before.getJSONObject(0).getString("text"))
    }

    @Test
    fun enterContinuesAListWithoutFlatteningOtherBlocks() {
        val original = fixture()
        val edited = original.withPlainContent(original.content.replace("Viñeta", "Viñeta\nSegunda"))
        val blocks = edited.properties.getJSONArray("blocks")
        assertEquals("bullet_list", blocks.getJSONObject(1).getString("type"))
        assertEquals(2, blocks.getJSONObject(1).getInt("indent"))
        assertEquals("image-a", blocks.getJSONObject(2).getString("id"))
        assertEquals("image-b", blocks.getJSONObject(5).getString("id"))
    }

    @Test
    fun typingInCodeRetainsTheLanguageAndImageOrder() {
        val original = fixture()
        val edited = original.withPlainContent(original.content.replace("val x = 1", "val x = 2\nprintln(x)"))
        val code = edited.properties.getJSONArray("blocks").getJSONObject(3)
        assertEquals("code", code.getString("id"))
        assertEquals("kotlin", code.getString("language"))
        assertEquals("val x = 2\u2028println(x)", code.getString("text"))
        assertEquals("image-b", edited.properties.getJSONArray("blocks").getJSONObject(4).getString("id"))
    }

    @Test
    fun newMarkdownRetainsListsHeadingAndInlineStylesInNativeBlocks() {
        val source = LumenObject("new", "note", "New")
        val edited = source.withPlainContent("- Uno\n- Dos\n\n## Título\n\n**Negrita** y *cursiva* y ~~tachado~~")
        val blocks = edited.properties.getJSONArray("blocks")
        assertEquals("bullet_list", blocks.getJSONObject(0).getString("type"))
        assertEquals("bullet_list", blocks.getJSONObject(1).getString("type"))
        assertEquals("heading", blocks.getJSONObject(2).getString("type"))
        assertEquals(2, blocks.getJSONObject(2).getInt("level"))
        val runs = blocks.getJSONObject(3).getJSONArray("runs")
        assertTrue((0 until runs.length()).any { runs.getJSONObject(it).optBoolean("bold") })
        assertTrue((0 until runs.length()).any { runs.getJSONObject(it).optBoolean("italic") })
        assertTrue((0 until runs.length()).any { runs.getJSONObject(it).optBoolean("strike") })
    }

    @Test
    fun splittingARichParagraphPreservesTheStylesOfBothHalves() {
        val blocks = JSONArray().put(block("rich", "paragraph", "Negrita cursiva",
            JSONObject().put("text", "Negrita ").put("bold", true),
            JSONObject().put("text", "cursiva").put("italic", true)))
        val source = LumenObject("rich", "note", "Rich", content = nativeDocumentText(blocks), properties = JSONObject().put("blocks", blocks))
        val edited = source.withPlainContent("Negrita \ncursiva").properties.getJSONArray("blocks")
        assertEquals(2, edited.length())
        assertTrue(edited.getJSONObject(0).getJSONArray("runs").getJSONObject(0).getBoolean("bold"))
        assertTrue(edited.getJSONObject(1).getJSONArray("runs").getJSONObject(0).getBoolean("italic"))
    }
}
