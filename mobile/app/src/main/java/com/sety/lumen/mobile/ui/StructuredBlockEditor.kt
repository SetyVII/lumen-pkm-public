package com.sety.lumen.mobile.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject

@Composable
fun BlockInsertButton(onInsert: (JSONObject) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { expanded = true }) { Text("Añadir bloque") }
        DropdownMenu(expanded, onDismissRequest = { expanded = false }) {
            listOf("Párrafo", "Toggle", "Tabla", "Columnas").forEach { label -> DropdownMenuItem(text = { Text(label) }, onClick = {
                onInsert(when (label) { "Toggle" -> nativeToggle(); "Tabla" -> nativeTable(); "Columnas" -> nativeColumns(); else -> nativeParagraph() })
                expanded = false
            }) }
        }
    }
}

@Composable
internal fun NativeTableEditor(
    block: JSONObject, source: LumenObject, objects: List<LumenObject>,
    onOpenObject: (LumenObject) -> Unit, assetFor: (String) -> LumenAsset?,
    onChange: ((JSONObject) -> Unit)?,
) {
    val rows = block.optJSONArray("rows") ?: JSONArray()
    var selectedRow by remember(block.optString("id")) { mutableIntStateOf(0) }
    var selectedColumn by remember(block.optString("id")) { mutableIntStateOf(0) }
    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
        Column(Modifier.horizontalScroll(rememberScrollState())) {
            for (r in 0 until rows.length()) {
                val cells = rows.optJSONArray(r) ?: continue
                Row {
                    for (c in 0 until cells.length()) {
                        val cell = cells.optJSONObject(c) ?: JSONObject()
                        val runs = cell.optJSONArray("runs") ?: JSONArray()
                        val text = (0 until runs.length()).joinToString("") { runs.getJSONObject(it).optString("text").replace('\u2028', '\n') }
                        key(r, c) {
                            var value by remember { mutableStateOf(TextFieldValue(text)) }
                            LaunchedEffect(text) { if (value.text != text) value = value.copy(text = text) }
                            Surface(Modifier.width(180.dp).padding(1.dp), color = if (r == 0) MaterialTheme.colorScheme.surfaceVariant else MaterialTheme.colorScheme.surface,
                                border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)) {
                                val cellBlock = nativeParagraph(text).put("alignment", when (block.optJSONArray("align")?.optString(c)) { "center" -> 4; "right" -> 2; else -> 1 }).put("id", "cell-${block.optString("id")}-$r-$c").put("runs", runs)
                                val cellSource = source.copy(content = text, properties = JSONObject().put("blocks", JSONArray().put(cellBlock)))
                                if (onChange == null) Text(EditorPresentation(text, editorTokens(text, cellSource, objects), MaterialTheme.colorScheme.onSurface,
                                    MaterialTheme.colorScheme.surfaceVariant, runs).transformed.text, Modifier.padding(8.dp))
                                else LumenEditorField(value, { edited ->
                                    selectedRow = r; selectedColumn = c; value = edited
                                    if (text != edited.text) onChange(editTableCell(block, r, c, edited.text))
                                }, "Celda vacía", modifier = Modifier.padding(8.dp).semantics { contentDescription = "Celda ${r + 1}, ${c + 1}" },
                                    minHeight = 40.dp, source = cellSource, objects = objects, onOpenObject = onOpenObject, assetFor = assetFor)
                            }
                        }
                    }
                }
            }
        }
        if (onChange != null) {
            Row(Modifier.horizontalScroll(rememberScrollState())) {
                TextButton(onClick = { onChange(resizeTable(block, addRow = true)) }) { Text("Añadir fila") }
                TextButton(onClick = { onChange(resizeTable(block, addColumn = true)) }) { Text("Añadir columna") }
            }
            var menu by remember { mutableStateOf(false) }
            var remove by remember { mutableStateOf<String?>(null) }
            Box {
                TextButton(onClick = { menu = true }) { Text("Opciones de tabla") }
                DropdownMenu(menu, { menu = false }) {
                    DropdownMenuItem(text = { Text("Eliminar fila ${selectedRow + 1}") }, enabled = rows.length() > 1,
                        onClick = { menu = false; remove = "fila" })
                    DropdownMenuItem(text = { Text("Eliminar columna ${selectedColumn + 1}") }, enabled = (rows.optJSONArray(0)?.length() ?: 0) > 1,
                        onClick = { menu = false; remove = "columna" })
                    listOf("left" to "Izquierda", "center" to "Centro", "right" to "Derecha").forEach { (align, label) ->
                        DropdownMenuItem(text = { Text("Alinear columna: $label") }, onClick = {
                            val updated = JSONObject(block.toString()); val aligns = updated.optJSONArray("align") ?: JSONArray()
                            aligns.put(selectedColumn, align); updated.put("align", aligns); onChange(updated); menu = false
                        })
                    }
                }
            }
            remove?.let { kind -> AlertDialog(onDismissRequest = { remove = null }, title = { Text("¿Eliminar $kind?") },
                text = { Text("Se eliminará también su contenido.") }, confirmButton = { TextButton(onClick = {
                    onChange(resizeTable(block, removeRow = if (kind == "fila") selectedRow.coerceAtMost(rows.length() - 1) else null,
                        removeColumn = if (kind == "columna") selectedColumn.coerceAtMost(rows.getJSONArray(0).length() - 1) else null))
                    selectedRow = 0; selectedColumn = 0; remove = null
                }) { Text("Eliminar") } }, dismissButton = { TextButton(onClick = { remove = null }) { Text("Cancelar") } }) }
        }
    }
}

@Composable
internal fun NativeColumnsEditor(
    block: JSONObject, source: LumenObject, objects: List<LumenObject>,
    onOpenObject: (LumenObject) -> Unit, assetFor: (String) -> LumenAsset?, onChange: ((JSONObject) -> Unit)?,
) {
    val group = block.optString("type") == "group"
    val columns = if (group) JSONArray().put(block.optJSONArray("children") ?: JSONArray()) else block.optJSONArray("columns") ?: JSONArray()
    var remove by remember { mutableStateOf<Int?>(null) }
    // A narrow phone stacks columns in reading order. The native grouping is
    // retained, so desktop can still lay them out side by side.
    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        for (c in 0 until columns.length()) key(c) {
            val children = columns.optJSONArray(c) ?: JSONArray()
            fun replace(next: JSONArray) {
                val updated = JSONObject(block.toString())
                if (group) updated.put("children", next) else updated.getJSONArray("columns").put(c, next)
                onChange?.invoke(updated)
            }
            val childSource = source.copy(content = nativeDocumentText(children), properties = JSONObject().put("blocks", children))
            var value by remember { mutableStateOf(TextFieldValue(childSource.content)) }
            LaunchedEffect(childSource.content) { if (value.text != childSource.content) value = value.copy(text = childSource.content) }
            OutlinedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    if (!group) Text("Columna ${c + 1}", style = MaterialTheme.typography.labelLarge)
                    NativeEditorContent(value, childSource, children, objects, onValueChange = { next ->
                        value = next
                        if (next.text != childSource.content) replace(reconcileNativeDocument(childSource, next.text, objects))
                    }, onOpenObject = onOpenObject, assetFor = assetFor, onBlocksChange = onChange?.let { { next -> replace(next) } })
                    if (!group && onChange != null) Row(Modifier.horizontalScroll(rememberScrollState())) {
                        if (c > 0) TextButton(onClick = {
                            val updated = JSONObject(block.toString()); val cols = updated.getJSONArray("columns")
                            val before = cols.get(c - 1); cols.put(c - 1, cols.get(c)); cols.put(c, before); onChange(updated)
                        }) { Text("Mover antes") }
                        if (columns.length() > 1) TextButton(onClick = { remove = c }) { Text("Eliminar columna ${c + 1}") }
                    }
                }
            }
        }
        if (!group && onChange != null) TextButton(onClick = {
            val updated = JSONObject(block.toString()); updated.getJSONArray("columns").put(JSONArray().put(nativeParagraph())); onChange(updated)
        }) { Text("Añadir columna al grupo") }
    }
    remove?.let { index -> AlertDialog(onDismissRequest = { remove = null }, title = { Text("¿Eliminar columna ${index + 1}?") },
        text = { Text("Se eliminará también su contenido.") }, confirmButton = { TextButton(onClick = {
            val updated = JSONObject(block.toString()); updated.getJSONArray("columns").remove(index); onChange?.invoke(updated); remove = null
        }) { Text("Eliminar") } }, dismissButton = { TextButton(onClick = { remove = null }) { Text("Cancelar") } }) }
}
