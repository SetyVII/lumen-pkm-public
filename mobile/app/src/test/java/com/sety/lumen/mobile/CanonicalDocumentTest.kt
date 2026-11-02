package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

class CanonicalDocumentTest {
    @Test fun nestedBlockEmbedResolvesOnlyItsSubtreeAndReturnsDetachedData() {
        val child = documentParagraph("Target")
        val source = CanonicalDocument.empty().withBlocks(JSONArray().put(documentNode("group", children = JSONArray().put(child))).put(documentParagraph("Other")))
        val found = source.block(child.getString("id"))!!
        assertEquals("Target", documentNodesText(JSONArray().put(found)))
        found.put("type", "divider")
        assertEquals("paragraph", source.block(child.getString("id"))!!.getString("type"))
        assertNull(source.block("missing"))
    }
    @Test fun sharedServerCorpusIsLosslessAndDetached() {
        val fixtureRoot = sequenceOf(File("../contracts/fixtures/document-v2"), File("../../contracts/fixtures/document-v2"))
            .firstOrNull { it.isDirectory } ?: error("Shared contract fixtures missing")
        val fixtures = fixtureRoot.listFiles()!!.filter { it.name.endsWith(".document.json") }
        assertTrue(fixtures.isNotEmpty())
        fixtures.forEach { file ->
            val source = JSONObject(file.readText())
            val document = CanonicalDocument.parse(source)
            assertTrue(file.name, jsonEquivalent(source, document.toJson()))
            val copy = document.toJson(); copy.put("schema_version", 99)
            assertEquals(2, document.toJson().getInt("schema_version"))
            val roundTrip = LumenObject.fromJson(LumenObject("id", "image", "Imagen").withDocument(document).toJson())
            assertTrue(jsonEquivalent(document.toJson(), roundTrip.document.toJson()))
        }
    }

    @Test fun editingTextRetainsAdjacentRichNodesAndMarks() {
        val paragraph = documentParagraph().put("attrs", JSONObject().put("alignment", "right").put("extension", "kept"))
            .put("content", JSONArray().put(JSONObject().put("type", "text").put("text", "Hola 👩‍💻").put("marks", JSONObject().put("bold", true)))
                .put(JSONObject().put("type", "object_ref").put("object_id", "book").put("label", "Libro")))
        val changed = editDocumentText(paragraph, "Hola 👩‍💻 másLibro")
        assertEquals(paragraph.getString("id"), changed.getString("id"))
        assertEquals("kept", changed.getJSONObject("attrs").getString("extension"))
        assertEquals("book", changed.getJSONArray("content").getJSONObject(changed.getJSONArray("content").length() - 1).getString("object_id"))
        assertEquals("Hola 👩‍💻 másLibro", documentInlineText(changed.getJSONArray("content")))
    }

    @Test fun atomicReferencesCannotBePartiallyRewritten() {
        val paragraph = documentParagraph().put("content", JSONArray().put(JSONObject().put("type", "block_ref").put("object_id", "book").put("block_id", "p").put("label", "Destino")))
        assertTrue(runCatching { editDocumentText(paragraph, "Destin") }.isFailure)
        assertEquals("Destino", documentInlineText(paragraph.getJSONArray("content")))
    }

    @Test fun editingOneParagraphDoesNotFlattenColumnOrMedia() {
        val columns = documentColumns()
        columns.getJSONArray("children").getJSONObject(1).getJSONArray("children").put(documentNode("image", JSONObject().put("asset_id", "asset").put("width", 320).put("caption", "Detalle")))
        val blocks = JSONArray().put(documentParagraph("Antes")).put(columns)
        val source = CanonicalDocument.empty().withBlocks(blocks)
        val edited = source.blocks().apply { put(0, editDocumentText(getJSONObject(0), "Después")) }
        val result = source.withBlocks(edited)
        assertTrue(jsonEquivalent(source.blocks().getJSONObject(1), result.blocks().getJSONObject(1)))
        assertTrue(result.plainText().contains("Detalle"))
    }

    @Test fun validationRejectsDuplicateIdsUnsupportedSchemaAndInvertedDates() {
        val same = documentParagraph("a")
        assertTrue(runCatching { CanonicalDocument.empty().withBlocks(JSONArray().put(same).put(same)) }.isFailure)
        assertTrue(runCatching { CanonicalDocument.parse(JSONObject().put("schema_version", 1).put("blocks", JSONArray())) }.isFailure)
        val date = documentParagraph().put("content", JSONArray().put(JSONObject().put("type", "temporal").put("attrs", JSONObject()
            .put("kind", "date").put("start", "2026-11-02").put("end", "2026-11-01"))))
        assertTrue(runCatching { CanonicalDocument.empty().withBlocks(JSONArray().put(date)) }.isFailure)
    }

    @Test fun plainProjectionCannotOverwriteStructuredDocument() {
        val source = LumenObject("id", "pdf", "PDF").withDocument(CanonicalDocument.empty().withBlocks(JSONArray().put(documentTable())))
        assertTrue(runCatching { source.withPlainContent("Flattened") }.isFailure)
        assertEquals("table", source.document.blocks().getJSONObject(0).getString("type"))
    }

    @Test fun selectedFormattingDoesNotChangeTheRestOrAtomicReferences() {
        val paragraph = documentParagraph().put("content", JSONArray()
            .put(JSONObject().put("type", "text").put("text", "Antes café después"))
            .put(JSONObject().put("type", "object_ref").put("object_id", "target").put("label", "Libro")))
        val formatted = formatDocumentSelection(paragraph, 6, 10, "bold", true)
        val runs = formatted.getJSONArray("content")
        assertEquals("Antes ", runs.getJSONObject(0).getString("text"))
        assertFalse(runs.getJSONObject(0).optJSONObject("marks")?.optBoolean("bold") ?: false)
        assertEquals("café", runs.getJSONObject(1).getString("text"))
        assertTrue(runs.getJSONObject(1).getJSONObject("marks").getBoolean("bold"))
        assertEquals("target", runs.getJSONObject(runs.length() - 1).getString("object_id"))
        assertEquals(documentInlineText(paragraph.getJSONArray("content")), documentInlineText(runs))
    }

    @Test fun selectionFormattingCannotSplitASurrogatePair() {
        assertTrue(runCatching { formatDocumentSelection(documentParagraph("😀"), 0, 1, "bold", true) }.isFailure)
        val valid = formatDocumentSelection(documentParagraph("😀"), 0, 2, "italic", true)
        assertEquals("😀", documentInlineText(valid.getJSONArray("content")))
    }

    @Test fun listOrdinalsRestartAndRespectRomanAlphabeticAndNestedSequences() {
        fun item(kind: String, start: Int? = null, upper: Boolean = false) = documentParagraph("x").put("type", "list_item").put("attrs", JSONObject().put("kind", kind).apply {
            start?.let { put("start", it) }; if (upper) put("case", "upper")
        })
        val romans = JSONArray().put(item("roman", 8, true)).put(item("roman", upper = true)).put(item("roman", upper = true))
        assertEquals(listOf("VIII.", "IX.", "X."), (0..2).map { documentListLabel(romans, it) })
        val alpha = JSONArray().put(item("alpha", 26)).put(item("alpha")).put(documentParagraph("Break")).put(item("alpha"))
        assertEquals("z.", documentListLabel(alpha, 0)); assertEquals("aa.", documentListLabel(alpha, 1)); assertEquals("a.", documentListLabel(alpha, 3))
        romans.getJSONObject(0).put("children", JSONArray().put(item("decimal", 3)).put(item("decimal")))
        assertEquals("4.", documentListLabel(romans.getJSONObject(0).getJSONArray("children"), 1))
        romans.put(item("roman", 2, true)); assertEquals("II.", documentListLabel(romans, 3))
    }

    private fun jsonEquivalent(a: Any?, b: Any?): Boolean = when {
        a is JSONObject && b is JSONObject -> a.keys().asSequence().toSet() == b.keys().asSequence().toSet() && a.keys().asSequence().toSet().all { jsonEquivalent(a.get(it), b.get(it)) }
        a is JSONArray && b is JSONArray -> a.length() == b.length() && (0 until a.length()).all { jsonEquivalent(a.get(it), b.get(it)) }
        a is Number && b is Number -> a.toDouble() == b.toDouble()
        else -> a == b
    }
}
