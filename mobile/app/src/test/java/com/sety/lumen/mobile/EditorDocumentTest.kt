package com.sety.lumen.mobile

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import com.sety.lumen.mobile.data.*
import com.sety.lumen.mobile.ui.EditorPresentation
import com.sety.lumen.mobile.ui.insertSnippet
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class EditorDocumentTest {
    private val task = LumenObject("task-1", "task", "Comprobar backend")
    private val source = LumenObject("journal-test", "journal", "Diario", links = listOf(task.id))

    @Test
    fun desktopEmbedAndLegacyWikiReferencesShowTheTitleAndKeepTheId() {
        val native = source.copy(properties = JSONObject().put("blocks", JSONArray().put(
            JSONObject().put("id", "embed-id").put("type", "embed").put("text", "\uFFFC")
                .put("runs", JSONArray().put(JSONObject().put("text", "\uFFFC").put("object_id", task.id).put("type", "image"))),
        )))
        for (text in listOf("\uFFFC", "\uFFFC[[Comprobar backend]]", "[[Comprobar backend]]")) {
            val tokens = editorTokens(text, native, listOf(task))
            assertEquals(1, tokens.size)
            assertEquals(task.id, tokens.single().objectId)
            assertEquals(task.title, tokens.single().label)
            val display = EditorPresentation(text, tokens, Color.Black, Color.White).transformed.text.text
            assertEquals(" ${task.title} ", display)
            assertFalse(display.contains('\uFFFC'))
        }
    }

    @Test
    fun explicitReferencesAreStableWhenTitlesCollideAndEscapeBrackets() {
        val other = task.copy(id = "task-2")
        val text = objectMentionText(other)
        assertEquals(other.id, editorTokens(text, source, listOf(task, other)).single().objectId)
        assertEquals("", editorTokens("[[Comprobar backend]]", null, listOf(task, other)).single().objectId)
        val escaped = task.copy(title = "Leer [capítulo] \\ anexo")
        assertEquals(escaped.title, editorTokens(objectMentionText(escaped), source, listOf(escaped)).single().label)
    }

    @Test
    fun separatorInsertionStartsItsOwnLineAndKeepsTheCursorAfterIt() {
        val value = TextFieldValue("AntesDespués", TextRange(5))
        val edited = insertSnippet(value, "---\n")
        assertEquals("Antes\n---\nDespués", edited.text)
        assertEquals(TextRange(10), edited.selection)
        assertEquals(1, editorTokens(edited.text).count { it.kind == EditorTokenKind.DIVIDER })
        assertTrue(editorTokens("Texto --- en línea\n```\n---\n```").isEmpty())
    }

    @Test
    fun transformedOffsetsStayMonotonicAndInBoundsForEditing() {
        val text = "Antes ${objectMentionText(task)} después\n---\nFinal"
        val presentation = EditorPresentation(text, editorTokens(text, source, listOf(task)), Color.Black, Color.White)
        val shown = presentation.transformed
        assertFalse(shown.text.text.contains("lumen://"))
        assertFalse(shown.text.text.contains("---"))
        val forward = (0..text.length).map(shown.offsetMapping::originalToTransformed)
        val backward = (0..shown.text.length).map(shown.offsetMapping::transformedToOriginal)
        assertTrue(forward.zipWithNext().all { it.first <= it.second })
        assertTrue(backward.zipWithNext().all { it.first <= it.second })
        assertEquals(shown.text.length, forward.last())
        assertEquals(text.length, backward.last())
        assertEquals(3, shown.offsetMapping.transformedToOriginal(shown.offsetMapping.originalToTransformed(3)))
    }

    @Test
    fun mobileSaveWritesCanonicalReferencesAndDividers() {
        val blocks = JSONArray().put(documentParagraph("Antes"))
            .put(documentParagraph().put("content", JSONArray().put(JSONObject().put("type", "object_ref").put("object_id", task.id).put("label", task.title))))
            .put(documentNode("divider")).put(documentParagraph("Después"))
        val edited = source.withDocument(CanonicalDocument.empty().withBlocks(blocks))
        val received = LumenObject.fromJson(edited.toJson())
        assertEquals("divider", received.document.blocks().getJSONObject(2).getString("type"))
        assertTrue(task.id in received.document.references())
        assertEquals(blocks.toString(), received.document.blocks().toString())
        assertFalse(received.properties.has("rich_text"))
        assertFalse(received.properties.has("blocks"))
    }
}
