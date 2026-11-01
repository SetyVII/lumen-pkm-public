package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

fun nativeParagraph(text: String = ""): JSONObject = JSONObject().put("id", UUID.randomUUID().toString())
    .put("type", "paragraph").put("text", text).put("runs", JSONArray().put(JSONObject().put("text", text)))

fun nativeTable(rows: Int = 3, columns: Int = 2): JSONObject = nativeParagraph().put("type", "table").put("runs", JSONArray())
    .put("rows", JSONArray((0 until rows).map { JSONArray((0 until columns).map { JSONObject().put("runs", JSONArray().put(JSONObject().put("text", ""))) }) }))
    .put("align", JSONArray((0 until columns).map { "" }))

fun nativeColumns(count: Int = 2): JSONObject = nativeParagraph().put("type", "columns").put("runs", JSONArray())
    .put("columns", JSONArray((0 until count).map { JSONArray().put(nativeParagraph()) }))

fun replaceNativeBlock(blocks: JSONArray, index: Int, replacement: JSONObject): JSONArray = JSONArray(blocks.toString()).put(index, replacement)

fun LumenObject.withNativeBlocks(blocks: JSONArray): LumenObject {
    val props = JSONObject(properties.toString()).put("blocks", JSONArray(blocks.toString())).put("rich_text", JSONObject.NULL)
    return copy(content = nativeDocumentText(blocks), properties = props,
        links = ((links - nativeReferenceIds(properties.optJSONArray("blocks"))) + nativeReferenceIds(blocks)).distinct())
}

/** Only touch the selected cell; keep every other cell/run and extension key. */
fun editTableCell(table: JSONObject, row: Int, column: Int, text: String): JSONObject {
    val result = JSONObject(table.toString())
    val cell = result.getJSONArray("rows").getJSONArray(row).getJSONObject(column)
    val runs = cell.optJSONArray("runs") ?: JSONArray()
    val oldText = (0 until runs.length()).joinToString("") { runs.getJSONObject(it).optString("text") }
    val edited = editedRuns(JSONObject().put("text", oldText).put("runs", runs), text.replace('\n', '\u2028'))
    cell.put("runs", edited)
    if (cell.has("text")) cell.put("text", (0 until edited.length()).joinToString("") { edited.getJSONObject(it).optString("text") })
    return result
}

fun resizeTable(table: JSONObject, addRow: Boolean = false, addColumn: Boolean = false, removeRow: Int? = null, removeColumn: Int? = null): JSONObject {
    val result = JSONObject(table.toString())
    val rows = result.getJSONArray("rows")
    if (removeRow != null && rows.length() > 1) rows.remove(removeRow)
    if (addRow) rows.put(JSONArray((0 until (rows.optJSONArray(0)?.length() ?: 1)).map { JSONObject().put("runs", JSONArray().put(JSONObject().put("text", ""))) }))
    for (r in 0 until rows.length()) {
        val cells = rows.getJSONArray(r)
        if (removeColumn != null && cells.length() > 1) cells.remove(removeColumn)
        if (addColumn) cells.put(JSONObject().put("runs", JSONArray().put(JSONObject().put("text", ""))))
    }
    val align = result.optJSONArray("align") ?: JSONArray()
    if (removeColumn != null && align.length() > removeColumn) align.remove(removeColumn)
    if (addColumn) { while (align.length() < (rows.optJSONArray(0)?.length() ?: 0)) align.put("") }
    result.put("align", align)
    return result
}

/** The triangle is the shared state: right = collapsed, down = expanded. */
fun nativeToggle(): JSONObject = nativeParagraph("▼ ").put("type", "toggle")
    .put("children", JSONArray().put(nativeParagraph()))

fun toggleIsExpanded(block: JSONObject): Boolean = !block.optString("text").startsWith("▶ ")

fun editToggleTitle(block: JSONObject, title: String, expanded: Boolean = toggleIsExpanded(block)): JSONObject {
    val replacement = (if (expanded) "▼ " else "▶ ") + title
    return JSONObject(block.toString()).put("text", replacement).put("runs", editedRuns(block, replacement))
}

fun setToggleExpanded(block: JSONObject, expanded: Boolean): JSONObject =
    editToggleTitle(block, block.optString("text").removePrefix("▶ ").removePrefix("▼ "), expanded)
