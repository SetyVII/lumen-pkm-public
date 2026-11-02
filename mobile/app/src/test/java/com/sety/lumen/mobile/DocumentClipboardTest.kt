package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import java.io.File
import org.json.*
import org.junit.Assert.*
import org.junit.Test

class DocumentClipboardTest {
    @Test
    fun framedClipboardRejectsMissingAndUnknownKeysAndWrongScalarTypes() {
        val original = documentClipboard("source", JSONArray().put(documentParagraph("Kept")))
        for (key in listOf("clipboard_version", "source_object_id", "operation")) {
            val invalid = JSONObject(original.toString())
            invalid.remove(key)
            assertTrue(key, runCatching { pasteDocumentClipboard(invalid, "target") }.isFailure)
        }
        for ((key, value) in
            listOf(
                "extra" to true,
                "clipboard_version" to "1",
                "source_object_id" to 1,
                "operation" to "unexpected",
            )) {
            val invalid = JSONObject(original.toString()).put(key, value)
            assertTrue(key, runCatching { pasteDocumentClipboard(invalid, "target") }.isFailure)
        }
    }

    @Test
    fun sharedClipboardCollisionVectorMatchesGoAndQt() {
        val path =
            generateSequence(File(requireNotNull(System.getProperty("user.dir")))) { it.parentFile }
                .map {
                    File(it, "contracts/fixtures/clipboard-v1/internal-and-external-reference.json")
                }
                .first { it.isFile }
        val fixture = JSONObject(path.readText())
        val target = CanonicalDocument.parse(fixture.getJSONObject("target_document"))
        val ids = fixture.getJSONObject("new_ids")
        val copied =
            pasteDocumentClipboard(
                fixture.getJSONObject("clipboard"),
                fixture.getString("target_object_id"),
                target,
            ) { old ->
                ids.getString(old)
            }
        val combined = target.blocks()
        for (i in 0 until copied.length()) combined.put(copied.getJSONObject(i))
        assertTrue(
            target
                .withBlocks(combined)
                .semanticallyEquals(
                    CanonicalDocument.parse(fixture.getJSONObject("expected_document"))
                )
        )
    }

    @Test
    fun framedClipboardRequiresVersionButRawDocumentIsExplicitSafeCopy() {
        val frame = documentClipboard("source", JSONArray().put(documentParagraph("Text")))
        frame.remove("clipboard_version")
        assertTrue(runCatching { pasteDocumentClipboard(frame, "target") }.isFailure)
        frame.put("clipboard_version", 2)
        assertTrue(runCatching { pasteDocumentClipboard(frame, "target") }.isFailure)
        val raw = frame.getJSONObject("document")
        assertEquals(
            "Text",
            CanonicalDocument.empty().withBlocks(pasteDocumentClipboard(raw, "target")).plainText(),
        )
    }

    @Test
    fun cutMovesPreserveIdsButRepeatedPasteMintsNewOnes() {
        val block = documentParagraph("Moved")
        val envelope = documentClipboard("source", JSONArray().put(block), move = true)
        val moved = pasteDocumentClipboard(envelope, "target", CanonicalDocument.empty())
        assertEquals(block.getString("id"), moved.getJSONObject(0).getString("id"))
        val repeated =
            pasteDocumentClipboard(envelope, "target", CanonicalDocument.empty().withBlocks(moved))
        assertNotEquals(block.getString("id"), repeated.getJSONObject(0).getString("id"))
    }

    @Test
    fun subtreeSelectionCopiesOnceRemapsInternalReferencesAndPreservesExtensions() {
        val child =
            documentParagraph("Child")
                .put("attrs", JSONObject().put("extension", JSONObject().put("arbitrary", true)))
        val ref =
            documentParagraph()
                .put(
                    "content",
                    JSONArray()
                        .put(
                            JSONObject()
                                .put("type", "block_ref")
                                .put("object_id", "source")
                                .put("block_id", child.getString("id"))
                                .put("label", "Child")
                        ),
                )
        val external =
            documentParagraph()
                .put(
                    "content",
                    JSONArray()
                        .put(
                            JSONObject()
                                .put("type", "block_ref")
                                .put("object_id", "external")
                                .put("block_id", "outside")
                                .put("label", "Outside")
                        ),
                )
        val parent = documentNode("group", children = JSONArray().put(child).put(ref).put(external))
        val doc =
            CanonicalDocument.empty()
                .withBlocks(JSONArray().put(parent).put(documentParagraph("Kept")))
        val selected = setOf(parent.getString("id"), child.getString("id"))
        val subtree = selectedDocumentBlocks(doc, selected)
        assertEquals(1, subtree.length())
        val copied = pasteDocumentClipboard(documentClipboard("source", subtree), "target")
        val children = copied.getJSONObject(0).getJSONArray("children")
        assertNotEquals(parent.getString("id"), copied.getJSONObject(0).getString("id"))
        assertTrue(
            children
                .getJSONObject(0)
                .getJSONObject("attrs")
                .getJSONObject("extension")
                .getBoolean("arbitrary")
        )
        val internal = children.getJSONObject(1).getJSONArray("content").getJSONObject(0)
        assertEquals(children.getJSONObject(0).getString("id"), internal.getString("block_id"))
        assertEquals("target", internal.getString("object_id"))
        assertEquals(
            "external",
            children
                .getJSONObject(2)
                .getJSONArray("content")
                .getJSONObject(0)
                .getString("object_id"),
        )
        assertEquals("Kept", removeSelectedDocumentBlocks(doc, selected).plainText())
        assertEquals(2, doc.blocks().length())
    }

    @Test
    fun malformedInternalDocumentNeverFallsBackToLossyText() {
        val envelope =
            JSONObject()
                .put("document", JSONObject().put("schema_version", 99).put("blocks", JSONArray()))
        assertTrue(runCatching { pasteDocumentClipboard(envelope, "target") }.isFailure)
    }

    @Test
    fun externalHtmlPreservesFormattingListsAndTableWithoutExecutingOrCreatingFakeImages() {
        val nodes =
            normalizeDocumentPaste(
                "fallback",
                "<script>bad()</script><h2>Title</h2><p><b>Bold</b> <a href='javascript:bad()'>plain</a><img src='https://example.com/a.png' alt='Remote'></p><ol start='3'><li>Outer<ul><li>Inner</li></ul></li></ol><table><tr><th>A</th><th>B</th></tr><tr><td>C</td><td>D</td></tr></table>",
            )
        val doc = CanonicalDocument.empty().withBlocks(nodes)
        assertFalse(doc.plainText().contains("bad()"))
        assertEquals("heading", nodes.getJSONObject(0).getString("type"))
        assertTrue(
            nodes
                .getJSONObject(1)
                .getJSONArray("content")
                .getJSONObject(0)
                .getJSONObject("marks")
                .getBoolean("bold")
        )
        assertEquals("list_item", nodes.getJSONObject(2).getString("type"))
        assertEquals(3, nodes.getJSONObject(2).getJSONObject("attrs").getInt("start"))
        assertEquals(
            "list_item",
            nodes.getJSONObject(2).getJSONArray("children").getJSONObject(0).getString("type"),
        )
        assertEquals("table", nodes.getJSONObject(3).getString("type"))
        assertTrue(
            nodes
                .getJSONObject(3)
                .getJSONArray("children")
                .getJSONObject(0)
                .getJSONObject("attrs")
                .getBoolean("header")
        )
        assertFalse(doc.toJson().toString().contains("asset_id"))
    }

    @Test
    fun plainTextPasteKeepsUnicodeAndLiteralMarkdown() {
        val doc = CanonicalDocument.empty().withBlocks(normalizeDocumentPaste("**Literal**\n👩‍💻"))
        assertEquals("**Literal**\n👩‍💻", doc.plainText())
        assertFalse(
            doc.blocks().getJSONObject(0).getJSONArray("content").getJSONObject(0).has("marks")
        )
    }
}
