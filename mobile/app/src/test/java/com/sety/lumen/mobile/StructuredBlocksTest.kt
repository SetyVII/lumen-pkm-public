package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class StructuredBlocksTest {
    @Test fun sharedDesktopStructuresStayIntactAfterACellEditOnMobile() {
        val file = generateSequence(java.io.File(requireNotNull(System.getProperty("user.dir")))) { it.parentFile }
            .map { java.io.File(it, "contracts/fixtures/native-structures.json") }.first { it.exists() }
        val blocks = JSONObject(file.readText()).getJSONArray("blocks")
        val source = LumenObject("shared", "note", "Compartida").withNativeBlocks(blocks)
        val changed = source.withNativeBlocks(replaceNativeBlock(blocks, 0, editTableCell(blocks.getJSONObject(0), 1, 0, "Editada desde Android")))
        assertEquals("shared-table", changed.properties.getJSONArray("blocks").getJSONObject(0).getString("id"))
        assertEquals(blocks.getJSONObject(1).toString(), changed.properties.getJSONArray("blocks").getJSONObject(1).toString())
        assertEquals(setOf("shared-book", "shared-image"), nativeReferenceIds(changed.properties.getJSONArray("blocks")))
        assertEquals(setOf("shared-image"), embeddedImageIds(changed))
        assertTrue(changed.content.contains("Editada desde Android"))
    }
    @Test fun editingAndResizingATablePreservesStylesReferencesAndUnknownMetadata() {
        val table = nativeTable(2, 2).put("extension", "keep")
        val cell = table.getJSONArray("rows").getJSONArray(0).getJSONObject(0)
        cell.put("runs", JSONArray().put(JSONObject().put("text", "Libro").put("bold", true).put("object_id", "book").put("href", "lumen://object/book")))
        val before = table.toString()
        val untouched = table.getJSONArray("rows").getJSONArray(1).toString()
        val changed = editTableCell(table, 0, 0, "Libro editado")
        assertEquals(before, table.toString())
        val run = changed.getJSONArray("rows").getJSONArray(0).getJSONObject(0).getJSONArray("runs").getJSONObject(0)
        assertTrue(run.getBoolean("bold")); assertEquals("book", run.getString("object_id"))
        assertEquals(untouched, changed.getJSONArray("rows").getJSONArray(1).toString())
        val resized = resizeTable(changed, addRow = true, addColumn = true)
        assertEquals(3, resized.getJSONArray("rows").length())
        assertEquals(3, resized.getJSONArray("rows").getJSONArray(0).length())
        assertEquals("keep", resized.getString("extension"))
        assertEquals(table.getString("id"), resized.getString("id"))
        assertEquals(setOf("book"), nativeReferenceIds(JSONArray().put(resized)))
    }

    @Test fun columnsKeepNestedImagesAndUpdateSearchableContentAndLinks() {
        val imageRun = JSONObject().put("text", "\uFFFC").put("type", "image").put("object_id", "image").put("image_name", "lumen://image/image")
        val columns = nativeColumns()
        columns.getJSONArray("columns").getJSONArray(0).put(nativeParagraph("\uFFFC").put("runs", JSONArray().put(imageRun)))
        val table = nativeTable(1, 1)
        table.getJSONArray("rows").getJSONArray(0).getJSONObject(0).put("runs", JSONArray().put(JSONObject().put("text", "Buscable")))
        columns.getJSONArray("columns").getJSONArray(1).put(table)
        val source = LumenObject("note", "note", "Nota", links = listOf("unrelated"), properties = JSONObject().put("custom", 7))
        val edited = source.withNativeBlocks(JSONArray().put(columns))
        assertTrue(edited.content.contains("Buscable"))
        assertEquals(setOf("unrelated", "image"), edited.links.toSet())
        assertEquals(7, edited.properties.getInt("custom"))
        assertTrue(edited.properties.isNull("rich_text"))
        val next = edited.withNativeBlocks(JSONArray().put(nativeParagraph("Sin imagen")))
        assertEquals(listOf("unrelated"), next.links)
        assertEquals(columns.toString(), edited.properties.getJSONArray("blocks").getJSONObject(0).toString())
    }

    @Test fun editingAfterAStructuredBlockDoesNotFlattenIt() {
        val table = editTableCell(nativeTable(), 0, 0, "Título")
        val columns = nativeColumns()
        columns.getJSONArray("columns").getJSONArray(1).put(nativeParagraph("Dentro"))
        val blocks = JSONArray().put(table).put(columns).put(nativeParagraph("Después"))
        val source = LumenObject("source", "note", "Prueba").withNativeBlocks(blocks)
        val edited = source.withPlainContent(source.content.replace("Después", "Después editado"))
        val result = edited.properties.getJSONArray("blocks")
        assertEquals(table.toString(), result.getJSONObject(0).toString())
        assertEquals(columns.toString(), result.getJSONObject(1).toString())
        assertTrue(edited.content.endsWith("Después editado"))
    }
}
