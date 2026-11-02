package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject
import java.util.Locale
import java.util.UUID

/** Local optimistic equivalents of the shared table commands; never mutate the input. */
fun applyDocumentTableCommand(table: JSONObject, command: JSONObject): JSONObject {
    require(table.optString("type") == "table" && command.optString("node_id") == table.getString("id"))
    val next = JSONObject(table.toString())
    val columns = next.getJSONObject("attrs").getJSONArray("columns")
    val rows = next.getJSONArray("children")
    fun items(array: JSONArray) = (0 until array.length()).map { array.getJSONObject(it) }.toMutableList()
    fun splice(array: JSONArray, index: Int, node: JSONObject): JSONArray {
        val values = items(array); require(index in 0..values.size); values.add(index, node); return JSONArray(values)
    }
    val type = command.getString("type")
    val column = (0 until columns.length()).firstOrNull { columns.getJSONObject(it).optString("id") == command.optString("column_id") }
    val row = (0 until rows.length()).firstOrNull { rows.getJSONObject(it).optString("id") == command.optString("parent_id") }
    when (type) {
        "table_set_width" -> {
            val width = command.getDouble("width"); require(width.isFinite() && width > 0)
            columns.getJSONObject(requireNotNull(column)).put("width", width)
        }
        "table_move_column" -> {
            val index = requireNotNull(column)
            val definition = columns.getJSONObject(index); columns.remove(index)
            next.getJSONObject("attrs").put("columns", splice(columns, command.getInt("position"), definition))
            for (r in 0 until rows.length()) {
                val cells = rows.getJSONObject(r).getJSONArray("children")
                val cell = cells.getJSONObject(index); cells.remove(index)
                rows.getJSONObject(r).put("children", splice(cells, command.getInt("position"), cell))
            }
        }
        "table_delete_column" -> {
            require(columns.length() > 1); val index = requireNotNull(column); columns.remove(index)
            for (r in 0 until rows.length()) rows.getJSONObject(r).getJSONArray("children").remove(index)
        }
        "table_insert_column" -> {
            val position = command.getInt("position")
            val id = command.getString("column_id"); val width = command.getDouble("width")
            require(id.isNotBlank() && width.isFinite() && width > 0)
            next.getJSONObject("attrs").put("columns", splice(columns, position, JSONObject().put("id", id).put("width", width)))
            for (r in 0 until rows.length()) {
                val parent = rows.getJSONObject(r)
                val cell = documentNode("table_cell", children = JSONArray().put(documentParagraph()))
                command.optJSONObject("new_ids")?.let { ids ->
                    ids.optString(parent.getString("id") + "/cell").takeIf(String::isNotBlank)?.let { cell.put("id", it) }
                    ids.optString(parent.getString("id") + "/paragraph").takeIf(String::isNotBlank)?.let { cell.getJSONArray("children").getJSONObject(0).put("id", it) }
                }
                parent.put("children", splice(parent.getJSONArray("children"), position, cell))
            }
        }
        "table_insert_row" -> next.put("children", splice(rows, command.getInt("position"), JSONObject(command.getJSONObject("node").toString())))
        "table_delete_row", "table_move_row" -> {
            val index = requireNotNull(row)
            if (type == "table_delete_row") require(rows.length() > 1)
            val value = rows.getJSONObject(index); rows.remove(index)
            if (type == "table_move_row") next.put("children", splice(rows, command.getInt("position"), value))
        }
        "table_sort" -> {
            val index = requireNotNull(column)
            val numeric = command.optBoolean("numeric")
            val direction = command.getString("direction"); require(direction in setOf("asc", "desc"))
            val headers = items(rows).filter { it.optJSONObject("attrs")?.optBoolean("header") == true }
            val body = items(rows).filterNot { it.optJSONObject("attrs")?.optBoolean("header") == true }
            fun value(node: JSONObject) = documentNodeText(node.getJSONArray("children").getJSONObject(index)).trim()
            if (numeric) require(body.all { value(it).let { text -> text.isEmpty() || text.toDoubleOrNull()?.isFinite() == true } }) { "Numeric sort contains nonnumeric cell" }
            val sorted = body.sortedWith { a, b ->
                val x = value(a); val y = value(b)
                if (x.isEmpty() || y.isEmpty()) when { x.isEmpty() && y.isEmpty() -> 0; x.isEmpty() -> 1; else -> -1 }
                else {
                    val comparison = if (numeric) x.toDouble().compareTo(y.toDouble()) else x.lowercase(Locale.ROOT).compareTo(y.lowercase(Locale.ROOT))
                    if (direction == "desc") -comparison else comparison
                }
            }
            next.put("children", JSONArray(headers + sorted))
        }
        else -> error("Unsupported table command")
    }
    CanonicalDocument.empty().withBlocks(JSONArray().put(next))
    return next
}

fun tableRow(columns: Int): JSONObject = documentNode("table_row", children = JSONArray((0 until columns).map {
    documentNode("table_cell", children = JSONArray().put(documentParagraph()))
}))

fun tableInsertColumnCommand(table: JSONObject, position: Int): JSONObject = JSONObject().put("type", "table_insert_column")
    .put("node_id", table.getString("id")).put("column_id", UUID.randomUUID().toString()).put("position", position).put("width", 180)
