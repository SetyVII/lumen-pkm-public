package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class StructuredBlocksTest {
    @Test fun editingATableCellPreservesWidthsReferencesAndOtherCells() {
        val table = documentTable(2, 2)
        table.getJSONObject("attrs").getJSONArray("columns").getJSONObject(1).put("width", 293)
        val before = CanonicalDocument.empty().withBlocks(JSONArray().put(table))
        val next = before.blocks()
        val cell = next.getJSONObject(0).getJSONArray("children").getJSONObject(1).getJSONArray("children").getJSONObject(0)
        val paragraph = cell.getJSONArray("children").getJSONObject(0)
        cell.getJSONArray("children").put(0, editDocumentText(paragraph, "Editado desde Android"))
        val result = before.withBlocks(next)
        assertEquals("Editado desde Android", documentNodeText(result.blocks().getJSONObject(0).getJSONArray("children").getJSONObject(1).getJSONArray("children").getJSONObject(0)))
        assertEquals(293, result.blocks().getJSONObject(0).getJSONObject("attrs").getJSONArray("columns").getJSONObject(1).getInt("width"))
        assertEquals(before.blocks().getJSONObject(0).getJSONArray("children").getJSONObject(0).toString(), result.blocks().getJSONObject(0).getJSONArray("children").getJSONObject(0).toString())
    }
    @Test fun columnsKeepIndependentIdsAndNestedMedia() {
        val columns = documentColumns()
        val image = documentNode("image", JSONObject().put("asset_id", "asset").put("width", 320).put("height", 200))
        columns.getJSONArray("children").getJSONObject(1).getJSONArray("children").put(image)
        val source = CanonicalDocument.empty().withBlocks(JSONArray().put(documentParagraph("Antes")).put(columns))
        val changed = source.blocks(); changed.put(0, editDocumentText(changed.getJSONObject(0), "Después"))
        val result = source.withBlocks(changed)
        assertEquals(columns.toString(), result.blocks().getJSONObject(1).toString())
        assertEquals(320, result.blocks().getJSONObject(1).getJSONArray("children").getJSONObject(1).getJSONArray("children").getJSONObject(1).getJSONObject("attrs").getInt("width"))
    }
}
