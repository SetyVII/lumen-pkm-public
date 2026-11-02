package com.sety.lumen.mobile.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.ClipDescription
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.launch
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.*
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject

val LocalDocumentMediaImporter = staticCompositionLocalOf<(suspend (Uri) -> LumenObject)?> { null }
private data class DocumentSelection(val selected:Set<String> = emptySet(), val covered:Set<String> = emptySet(), val toggle:(String)->Unit = {})
private val LocalDocumentSelection = staticCompositionLocalOf { DocumentSelection() }

/** The same tree editor is used for journal, page and media-object notes. */
@Composable
fun CanonicalDocumentEditor(
    document: CanonicalDocument,
    objects: List<LumenObject>,
    assetFor: (String) -> LumenAsset?,
    onOpenObject: (LumenObject) -> Unit,
    onChange: (CanonicalDocument) -> Unit,
    modifier: Modifier = Modifier,
    objectId: String = "",
) {
    val context = LocalContext.current
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    var selected by remember { mutableStateOf(setOf<String>()) }
    var local by remember { mutableStateOf(document) }
    val history = remember { DocumentUndoHistory() }
    var error by remember { mutableStateOf("") }
    LaunchedEffect(document) {
        if (!local.semanticallyEquals(document)) {
            // An incoming remote revision starts a new local undo branch.
            local = document; history.reset()
        }
    }
    fun save(blocks: JSONArray) {
        runCatching { local.withBlocks(blocks) }.onSuccess {
            if (!history.record(local,it)) return@onSuccess
            local = it; error = ""; onChange(it)
        }.onFailure { error = it.message ?: "Documento no válido" }
    }
    val covered=mutableSetOf<String>()
    fun cover(nodes:JSONArray) {for(i in 0 until nodes.length()){val node=nodes.getJSONObject(i);covered+=node.getString("id");node.optJSONArray("children")?.let(::cover)}}
    cover(selectedDocumentBlocks(local,selected))
    fun copySelection(move:Boolean=false):Boolean {
        val nodes=selectedDocumentBlocks(local,selected)
        if(nodes.length()==0)return false
        val envelope=documentClipboard(objectId,nodes,move)
        val plain=CanonicalDocument.empty().withBlocks(nodes).plainText()
        val item=ClipData.Item(plain,null,Intent().putExtra("lumen_document_v2",envelope.toString()),null)
        clipboard.setPrimaryClip(ClipData(ClipDescription("Bloques Lumen",arrayOf(DOCUMENT_CLIPBOARD_MIME,"text/plain")),item));return true
    }
    CompositionLocalProvider(LocalDocumentSelection provides DocumentSelection(selected,covered){ id->selected=if(id in selected)selected-id else selected+id }) {
    Column(modifier.fillMaxWidth()) {
        Row {
            TextButton(enabled=history.canUndo,onClick={history.undo(local)?.let{local=it;onChange(it)}}){Text("Deshacer")}
            TextButton(enabled=history.canRedo,onClick={history.redo(local)?.let{local=it;onChange(it)}}){Text("Rehacer")}

        }
        Row(Modifier.horizontalScroll(rememberScrollState())) {
            TextButton(enabled=selected.isNotEmpty(),onClick={runCatching{copySelection()}.onFailure{error=it.message.orEmpty()}}){Text("Copiar selección")}
            TextButton(enabled=selected.isNotEmpty(),onClick={runCatching { val next=removeSelectedDocumentBlocks(local,selected);if(copySelection(move=true)){save(next.blocks());selected=emptySet()} }.onFailure{error=it.message.orEmpty()}}){Text("Cortar")}
            TextButton(enabled=selected.isNotEmpty(),onClick={runCatching{save(groupSelectedDocumentBlocks(local,selected).blocks());selected=emptySet()}.onFailure{error=it.message.orEmpty()}}){Text("Agrupar selección")}
            TextButton(enabled=selected.isNotEmpty(),onClick={runCatching { save(removeSelectedDocumentBlocks(local,selected).blocks());selected=emptySet() }.onFailure{error=it.message.orEmpty()}}){Text("Eliminar selección")}
            TextButton(onClick={runCatching {
                val clip=clipboard.primaryClip?.takeIf{it.itemCount>0}?:return@runCatching
                val item=clip.getItemAt(0)
                val internal=item.intent?.getStringExtra("lumen_document_v2") ?: if(clip.description.hasMimeType(DOCUMENT_CLIPBOARD_MIME)) {
                    item.text?.toString() ?: error("El portapapeles estructurado no contiene JSON accesible")
                } else null
                require(internal==null || internal.toByteArray(Charsets.UTF_8).size<=8*1024*1024){"Portapapeles demasiado grande"}
                val incoming=if(internal!=null)pasteDocumentClipboard(JSONObject(internal),objectId,local) else normalizeDocumentPaste(item.coerceToText(context).toString(),item.htmlText)
                val next=local.blocks();for(i in 0 until incoming.length())next.put(incoming.getJSONObject(i));save(next)
            }.onFailure{error=it.message.orEmpty()}}){Text("Pegar bloques al final")}
            TextButton(onClick={selected=emptySet()}){Text("Deseleccionar")}
        }
        CanonicalNodes(local.blocks(), objects, assetFor, onOpenObject, ::save)
        if (error.isNotEmpty()) Text(error, color = MaterialTheme.colorScheme.error)
    }
    }
}

@Composable
private fun CanonicalNodes(
    nodes: JSONArray, objects: List<LumenObject>, assetFor: (String) -> LumenAsset?,
    onOpenObject: (LumenObject) -> Unit, onChange: (JSONArray) -> Unit,
) {
    fun replace(index: Int, value: JSONObject) = onChange(JSONArray(nodes.toString()).put(index, value))
    Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
        for (index in 0 until nodes.length()) {
            val node = nodes.getJSONObject(index)
            key(node.getString("id")) {
                Column(Modifier.testTag("document-node-${node.getString("id")}")) {
                    val selection=LocalDocumentSelection.current
                    Row { Checkbox(node.getString("id") in selection.covered, {selection.toggle(node.getString("id"))}, enabled=node.getString("id") !in selection.covered || node.getString("id") in selection.selected, modifier=Modifier.testTag("document-select-${node.getString("id")}")); Text("Seleccionar bloque",style=MaterialTheme.typography.labelSmall) }
                    CanonicalNode(node, objects, assetFor, onOpenObject, documentListLabel(nodes, index)) { replace(index, it) }
                    var menu by remember { mutableStateOf(false) }
                    Box {
                        TextButton(onClick = { menu = true }, contentPadding = PaddingValues(2.dp)) { Text("⋯", style = MaterialTheme.typography.labelSmall) }
                        DropdownMenu(menu, { menu = false }) {
                            if (index > 0) DropdownMenuItem(text = { Text("Mover antes") }, onClick = {
                                val next = JSONArray(nodes.toString()); val previous = next.get(index - 1)
                                next.put(index - 1, next.get(index)); next.put(index, previous); onChange(next); menu = false
                            })
                            if (index < nodes.length() - 1) DropdownMenuItem(text = { Text("Mover después") }, onClick = {
                                val next = JSONArray(nodes.toString()); val after = next.get(index + 1)
                                next.put(index + 1, next.get(index)); next.put(index, after); onChange(next); menu = false
                            })
                            DropdownMenuItem(text = { Text("Eliminar bloque") }, onClick = {
                                val next = JSONArray(nodes.toString()); next.remove(index); onChange(next); menu = false
                            })
                        }
                    }
                }
            }
        }
        if (nodes.optJSONObject(0)?.optString("type") == "column") {
            TextButton(enabled = nodes.length() < 8, onClick = { onChange(JSONArray(nodes.toString()).put(documentNode("column", JSONObject().put("width", 1), JSONArray().put(documentParagraph())))) }) { Text("Añadir columna") }
        } else CanonicalInsert(objects) { onChange(JSONArray(nodes.toString()).put(it)) }
    }
}

@Composable
private fun CanonicalNode(
    node: JSONObject, objects: List<LumenObject>, assetFor: (String) -> LumenAsset?,
    onOpenObject: (LumenObject) -> Unit, listLabel: String, onChange: (JSONObject) -> Unit,
) {
    val type = node.getString("type")
    val attrs = node.optJSONObject("attrs") ?: JSONObject()
    val children = node.optJSONArray("children") ?: JSONArray()
    fun attr(name: String, value: Any) = onChange(JSONObject(node.toString()).put("attrs", JSONObject(attrs.toString()).put(name, value)))
    fun content(next: JSONArray) = onChange(JSONObject(node.toString()).put("content", next))
    fun child(next: JSONArray) = onChange(JSONObject(node.toString()).put("children", next))
    when (type) {
        "divider" -> HorizontalDivider(Modifier.padding(vertical = 8.dp))
        "image" -> {
            val asset = assetFor(attrs.optString("asset_id"))
            val path = asset?.localPath?.ifBlank { asset.thumbPath }
            val owner = objects.firstOrNull { it.id == attrs.optString("object_id") }
            BoxWithConstraints(Modifier.fillMaxWidth()) {
                val alignment = when (attrs.optString("alignment")) {
                    "right" -> androidx.compose.ui.Alignment.CenterEnd
                    "center" -> androidx.compose.ui.Alignment.Center
                    else -> androidx.compose.ui.Alignment.CenterStart
                }
                val width = attrs.optDouble("width", maxWidth.value.toDouble()).toFloat().coerceIn(24f, maxWidth.value.coerceAtLeast(24f))
                Box(Modifier.fillMaxWidth(), contentAlignment = alignment) {
                    if (!path.isNullOrBlank()) AsyncImage(rememberMediaModel(path), "Imagen",
                        Modifier.width(width.dp).heightIn(max = 400.dp).clickable(enabled = owner != null) { owner?.let(onOpenObject) },
                        contentScale = if (attrs.optString("fit") == "cover") androidx.compose.ui.layout.ContentScale.Crop else androidx.compose.ui.layout.ContentScale.Fit)
                    else Text("Imagen pendiente de descargar", style = MaterialTheme.typography.labelMedium)
                }
            }
            Row(Modifier.horizontalScroll(rememberScrollState())) {
                listOf("left" to "Izquierda", "center" to "Centrar", "right" to "Derecha").forEach { (value, label) ->
                    TextButton(onClick = { attr("alignment", value) }) { Text(label) }
                }
                TextButton(onClick = { attr("width", (attrs.optDouble("width", 320.0) - 32).coerceAtLeast(32.0)) }) { Text("Reducir") }
                TextButton(onClick = { attr("width", attrs.optDouble("width", 320.0) + 32) }) { Text("Ampliar") }
                TextButton(enabled = owner != null, onClick = { owner?.let(onOpenObject) }) { Text("Abrir") }
            }
            TextField(attrs.optString("caption"), { attr("caption", it) }, label = { Text("Leyenda") }, modifier = Modifier.fillMaxWidth())
        }
        "object_embed" -> {
            val target = objects.firstOrNull { it.id == attrs.optString("object_id") }
            val blockId = attrs.optString("block_id")
            val excerpt = if (blockId.isBlank()) target?.document?.plainText().orEmpty()
                else target?.document?.block(blockId)?.let { documentNodesText(JSONArray().put(it)) } ?: "Bloque no disponible"
            val view = attrs.optString("view", "embed")
            var viewMenu by remember { mutableStateOf(false) }
            Column {
                OutlinedCard(Modifier.fillMaxWidth().clickable(enabled = target != null) { target?.let(onOpenObject) }) {
                    Column(Modifier.padding(10.dp)) {
                        Text(target?.title ?: "Objeto no disponible")
                        if (blockId.isNotBlank()) Text("Referencia a bloque", style = MaterialTheme.typography.labelSmall)
                        if (view in setOf("small_card", "wide_card", "embed")) {
                            target?.assetId?.takeIf(String::isNotBlank)?.let(assetFor)?.let { asset ->
                                val path = asset.thumbPath.ifBlank { asset.localPath }
                                if (path.isNotBlank()) AsyncImage(rememberMediaModel(path), target.title, Modifier.fillMaxWidth().heightIn(max = if (view == "small_card") 100.dp else 240.dp))
                            }
                            Text(excerpt.take(if (view == "embed") 4000 else 200), style = MaterialTheme.typography.bodySmall)
                        }
                    }
                }
                Box {
                    TextButton(onClick = { viewMenu = true }) { Text("Vista: $view") }
                    DropdownMenu(viewMenu, { viewMenu = false }) {
                        listOf("inline" to "En línea", "link_block" to "Bloque de enlace", "small_card" to "Tarjeta pequeña", "wide_card" to "Tarjeta ancha", "embed" to "Incrustar").forEach { (value, label) ->
                            DropdownMenuItem(text = { Text(label) }, onClick = { attr("view", value); viewMenu = false })
                        }
                    }
                }
            }
        }
        "math" -> Column {
            NativeMathPreview(attrs.optString("source"), Modifier.fillMaxWidth())
            TextField(attrs.optString("source"), { attr("source", it) }, label = { Text("LaTeX") }, textStyle = LocalTextStyle.current.copy(fontFamily = FontFamily.Monospace), modifier = Modifier.fillMaxWidth())
        }
        "code" -> TextField(attrs.optString("source"), { attr("source", it) }, label = { Text(attrs.optString("language", "Código")) },
            textStyle = LocalTextStyle.current.copy(fontFamily = FontFamily.Monospace), modifier = Modifier.fillMaxWidth())
        "table" -> CanonicalTable(node, objects, assetFor, onOpenObject, onChange)
        "toggle" -> {
            Row {
                TextButton(onClick = { attr("expanded", !attrs.optBoolean("expanded")) }) { Text(if (attrs.optBoolean("expanded")) "▼" else "▶") }
                Box(Modifier.weight(1f)) { CanonicalInline(node, ::content, objects, onOpenObject) }
            }
            if (attrs.optBoolean("expanded")) Box(Modifier.padding(start = 16.dp)) { CanonicalNodes(children, objects, assetFor, onOpenObject, ::child) }
        }
        "columns", "group", "quote", "column", "table_row", "table_cell" -> OutlinedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(10.dp)) {
                if (type == "columns") Text("Columnas · orden de lectura", style = MaterialTheme.typography.labelSmall)
                if (node.optJSONArray("content") != null) CanonicalInline(node, ::content, objects, onOpenObject)
                CanonicalNodes(children, objects, assetFor, onOpenObject, ::child)
            }
        }
        else -> {
            Row(Modifier.fillMaxWidth()) {
                if (type == "list_item") {
                    if (attrs.optString("kind") == "task") Checkbox(attrs.optBoolean("checked"), { attr("checked", it) })
                    else Text("$listLabel ", Modifier.padding(top = 6.dp))
                }
                Box(Modifier.weight(1f)) { CanonicalInline(node, ::content, objects, onOpenObject) }
            }
            if (children.length() > 0) Box(Modifier.padding(start = 16.dp)) { CanonicalNodes(children, objects, assetFor, onOpenObject, ::child) }
        }
    }
}

@Composable
private fun CanonicalInline(node: JSONObject, onChange: (JSONArray) -> Unit, objects: List<LumenObject>, onOpenObject: (LumenObject) -> Unit, showFormatting: Boolean = true) {
    val content = node.optJSONArray("content") ?: JSONArray()
    val text = documentInlineText(content)
    var value by remember(node.getString("id")) { mutableStateOf(androidx.compose.ui.text.input.TextFieldValue(text)) }
    var typingMarks by remember(node.getString("id")) { mutableStateOf<JSONObject?>(null) }
    LaunchedEffect(text) { if (value.text != text) value = value.copy(text = text, selection = TextRange(value.selection.start.coerceAtMost(text.length), value.selection.end.coerceAtMost(text.length))) }
    var error by remember { mutableStateOf("") }
    var temporalIndex by remember { mutableStateOf<Int?>(null) }
    var formulaIndex by remember { mutableStateOf<Int?>(null) }
    val attrs = node.optJSONObject("attrs") ?: JSONObject()
    val align = when (attrs.optString("alignment")) { "center" -> TextAlign.Center; "right" -> TextAlign.Right; else -> TextAlign.Left }
    val headingSize = if (node.optString("type") == "heading") (30 - attrs.optInt("level", 1) * 2).sp else 16.sp
    Column {
        BasicTextField(value, { proposed ->
            runCatching { editDocumentText(node, proposed.text, typingMarks) }.onSuccess {
                value = proposed; error = ""; if (proposed.text != text) onChange(it.getJSONArray("content"))
            }.onFailure { error = "Usa el control de la referencia para eliminarla" }
        }, textStyle = LocalTextStyle.current.copy(color = MaterialTheme.colorScheme.onSurface, textAlign = align, fontSize = headingSize),
            visualTransformation = VisualTransformation {
                val annotated = buildAnnotatedString {
                    for (i in 0 until content.length()) {
                        val item = content.getJSONObject(i); val marks = item.optJSONObject("marks") ?: JSONObject()
                        withStyle(SpanStyle(fontWeight = if (marks.optBoolean("bold")) FontWeight.Bold else FontWeight.Normal,
                            fontStyle = if (marks.optBoolean("italic")) FontStyle.Italic else FontStyle.Normal,
                            textDecoration = when { marks.optBoolean("underline") && marks.optBoolean("strike") -> TextDecoration.combine(listOf(TextDecoration.Underline, TextDecoration.LineThrough)); marks.optBoolean("underline") -> TextDecoration.Underline; marks.optBoolean("strike") -> TextDecoration.LineThrough; else -> TextDecoration.None },
                            fontFamily = if (marks.optBoolean("code")) FontFamily.Monospace else null)) {
                            append(documentInlineText(JSONArray().put(item)))
                        }
                    }
                }
                TransformedText(annotated, OffsetMapping.Identity)
            }, modifier = Modifier.fillMaxWidth().heightIn(min = 30.dp).padding(vertical = 5.dp))
        if (showFormatting) Row(Modifier.horizontalScroll(rememberScrollState())) {
            listOf("bold" to "B", "italic" to "I", "underline" to "U", "strike" to "S").forEach { (mark, label) ->
                TextButton(onClick = {
                    val start = minOf(value.selection.start, value.selection.end)
                    val end = maxOf(value.selection.start, value.selection.end)
                    if (start == end) {
                        typingMarks = JSONObject(typingMarks?.toString() ?: "{}").apply { put(mark, !optBoolean(mark)) }
                    } else {
                        val marked = formatDocumentSelection(node, start, end, mark, true)
                        onChange(marked.getJSONArray("content"))
                    }
                }, contentPadding = PaddingValues(2.dp)) { Text(label) }
            }
        }
        for (i in 0 until content.length()) {
            val item = content.getJSONObject(i)
            if (item.optString("type") !in setOf("text", "link", "break")) {
                if (item.optString("type") == "math") NativeMathPreview(item.optString("latex"), inline = true)
                Row {
                    val target = objects.firstOrNull { it.id == item.optString("object_id") }
                    val display = if (item.optString("type") == "temporal") temporalDisplay(item.getJSONObject("attrs")) else documentInlineText(JSONArray().put(item))
                    TextButton(onClick = {
                        when (item.optString("type")) {
                            "temporal" -> temporalIndex = i
                            "math" -> formulaIndex = i
                            else -> target?.let(onOpenObject)
                        }
                    }, modifier = Modifier.weight(1f)) { Text(display, style = MaterialTheme.typography.labelSmall) }
                    TextButton(onClick = { val next = JSONArray(content.toString()); next.remove(i); onChange(next) }) { Text("Quitar referencia") }
                }
            }
        }
        if (showFormatting) TextButton(onClick = {
            val now = java.time.ZonedDateTime.now().withSecond(0).withNano(0)
            val attrs = JSONObject().put("kind", "datetime").put("start", now.toInstant().toString()).put("timezone", now.zone.id)
            onChange(JSONArray(content.toString()).put(JSONObject().put("type", "temporal").put("attrs", attrs)))
        }) { Text("Ahora / fecha y hora") }
        temporalIndex?.let { index ->
            DocumentTemporalPicker(content.getJSONObject(index).getJSONObject("attrs"), onDismiss = { temporalIndex = null }, onConfirm = { attrs ->
                val next = JSONArray(content.toString()); next.getJSONObject(index).put("attrs", attrs); onChange(next); temporalIndex = null
            }, onRemove = { val next = JSONArray(content.toString()); next.remove(index); onChange(next); temporalIndex = null })
        }
        formulaIndex?.let { index ->
            var source by remember(index) { mutableStateOf(content.getJSONObject(index).optString("latex")) }
            AlertDialog(onDismissRequest = { formulaIndex = null }, title = { Text("Fórmula inline") }, text = {
                Column { NativeMathPreview(source, inline = true); TextField(source, { source = it }, label = { Text("LaTeX") }) }
            }, confirmButton = { TextButton(enabled = source.isNotBlank(), onClick = {
                val next = JSONArray(content.toString()); next.getJSONObject(index).put("latex", source); onChange(next); formulaIndex = null
            }) { Text("Confirmar") } }, dismissButton = { TextButton(onClick = { formulaIndex = null }) { Text("Cancelar") } })
        }
        if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error)
    }
}

@Composable
private fun CanonicalTable(node: JSONObject, objects: List<LumenObject>, assetFor: (String) -> LumenAsset?, onOpenObject: (LumenObject) -> Unit, onChange: (JSONObject) -> Unit) {
    val rows = node.optJSONArray("children") ?: JSONArray()
    val columns = node.getJSONObject("attrs").getJSONArray("columns")
    var selectedRow by remember(node.getString("id")) { mutableIntStateOf(0) }
    var selectedColumn by remember(node.getString("id")) { mutableIntStateOf(0) }
    var scope by remember(node.getString("id")) { mutableStateOf("cell") }
    var menu by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf("") }
    val rIndex = selectedRow.coerceIn(0, rows.length() - 1)
    val cIndex = selectedColumn.coerceIn(0, columns.length() - 1)
    fun command(type: String, configure: (JSONObject) -> Unit = {}) {
        val value = JSONObject().put("type", type).put("node_id", node.getString("id"))
            .put("column_id", columns.getJSONObject(cIndex).getString("id")).put("parent_id", rows.getJSONObject(rIndex).getString("id"))
        configure(value)
        runCatching { applyDocumentTableCommand(node, value) }.onSuccess { error = ""; onChange(it) }.onFailure { error = it.message.orEmpty() }
    }
    fun selection(change: (JSONObject) -> Unit) {
        val next = JSONObject(node.toString()); val list = next.getJSONArray("children")
        for (r in 0 until list.length()) for (c in 0 until columns.length()) {
            if (scope == "table" || scope == "row" && r == rIndex || scope == "column" && c == cIndex || scope == "cell" && r == rIndex && c == cIndex)
                change(list.getJSONObject(r).getJSONArray("children").getJSONObject(c))
        }
        onChange(next)
    }
    Column {
        Column(Modifier.horizontalScroll(rememberScrollState())) {
            for (r in 0 until rows.length()) {
                val row = rows.getJSONObject(r); val cells = row.getJSONArray("children")
                Row(Modifier.height(IntrinsicSize.Min)) {
                    for (c in 0 until cells.length()) {
                        val cell = cells.getJSONObject(c)
                        val highlighted = scope == "table" || scope == "row" && r == rIndex || scope == "column" && c == cIndex || scope == "cell" && r == rIndex && c == cIndex
                        Surface(Modifier.width(columns.getJSONObject(c).getDouble("width").toFloat().coerceIn(60f, 600f).dp).fillMaxHeight().padding(1.dp),
                            color = if (highlighted) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.surface,
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)) {
                            Column(Modifier.padding(8.dp)) {
                                TextButton(onClick = { selectedRow = r; selectedColumn = c }) { Text("${r + 1}:${c + 1}", style = MaterialTheme.typography.labelSmall) }
                                val paragraphs = cell.getJSONArray("children")
                                fun updateCell(nextChildren: JSONArray) {
                                    val next = JSONObject(node.toString())
                                    next.getJSONArray("children").getJSONObject(r).getJSONArray("children").getJSONObject(c).put("children", nextChildren)
                                    onChange(next)
                                }
                                for (p in 0 until paragraphs.length()) CanonicalInline(paragraphs.getJSONObject(p), { nextContent ->
                                    selectedRow = r; selectedColumn = c
                                    val nextChildren = JSONArray(paragraphs.toString())
                                    nextChildren.getJSONObject(p).put("content", nextContent); updateCell(nextChildren)
                                }, objects, onOpenObject, showFormatting = false)
                            }
                        }
                    }
                }
            }
        }
        Row(Modifier.horizontalScroll(rememberScrollState())) {
            listOf("cell" to "Celda", "row" to "Fila", "column" to "Columna", "table" to "Tabla").forEach { (value, label) ->
                FilterChip(scope == value, { scope = value }, label = { Text(label) }, modifier=Modifier.testTag("table-scope-${node.getString("id")}-$value"))
            }
        }
        Row(Modifier.horizontalScroll(rememberScrollState())) {
            listOf("bold" to "B", "italic" to "I", "underline" to "U", "strike" to "S").forEach { (mark, label) ->
                TextButton(modifier=Modifier.testTag("table-format-${node.getString("id")}-$mark"),onClick = { selection { cell ->
                    val paragraphs = cell.getJSONArray("children")
                    for (p in 0 until paragraphs.length()) {
                        val text = paragraphs.getJSONObject(p)
                        paragraphs.put(p, formatDocumentSelection(text, 0, documentInlineText(text.optJSONArray("content") ?: JSONArray()).length, mark, true))
                    }
                } }) { Text(label) }
            }
            listOf("left" to "Izq.", "center" to "Centro", "right" to "Der.").forEach { (alignment, label) ->
                TextButton(onClick = { selection { cell ->
                    val paragraphs = cell.getJSONArray("children")
                    for (p in 0 until paragraphs.length()) {
                        val paragraph = paragraphs.getJSONObject(p)
                        paragraph.put("attrs", (paragraph.optJSONObject("attrs") ?: JSONObject()).put("alignment", alignment))
                    }
                } }) { Text(label) }
            }
        }
        Box {
            TextButton(onClick = { menu = true }) { Text("Opciones de fila / columna") }
            DropdownMenu(menu, { menu = false }) {
                DropdownMenuItem(text={Text("Añadir párrafo en celda")},onClick={
                    val next=JSONObject(node.toString())
                    val cell=next.getJSONArray("children").getJSONObject(rIndex).getJSONArray("children").getJSONObject(cIndex)
                    cell.getJSONArray("children").put(documentParagraph());onChange(next);menu=false
                })
                DropdownMenuItem(text = { Text("Insertar fila antes") }, onClick = { command("table_insert_row") { it.put("position", rIndex).put("node", tableRow(columns.length())) }; menu = false })
                DropdownMenuItem(text = { Text("Insertar fila después") }, onClick = { command("table_insert_row") { it.put("position", rIndex + 1).put("node", tableRow(columns.length())) }; menu = false })
                DropdownMenuItem(text = { Text("Insertar columna antes") }, onClick = { onChange(applyDocumentTableCommand(node, tableInsertColumnCommand(node, cIndex))); menu = false })
                DropdownMenuItem(text = { Text("Insertar columna después") }, onClick = { onChange(applyDocumentTableCommand(node, tableInsertColumnCommand(node, cIndex + 1))); menu = false })
                DropdownMenuItem(text = { Text("Mover fila arriba") }, enabled = rIndex > 0, onClick = { command("table_move_row") { it.put("position", rIndex - 1) }; selectedRow = rIndex - 1; menu = false })
                DropdownMenuItem(text = { Text("Mover fila abajo") }, enabled = rIndex < rows.length() - 1, onClick = { command("table_move_row") { it.put("position", rIndex + 1) }; selectedRow = rIndex + 1; menu = false })
                DropdownMenuItem(text = { Text("Mover columna antes") }, enabled = cIndex > 0, onClick = { command("table_move_column") { it.put("position", cIndex - 1) }; selectedColumn = cIndex - 1; menu = false })
                DropdownMenuItem(text = { Text("Mover columna después") }, enabled = cIndex < columns.length() - 1, onClick = { command("table_move_column") { it.put("position", cIndex + 1) }; selectedColumn = cIndex + 1; menu = false })
                DropdownMenuItem(text = { Text("Estrechar columna") }, onClick = { command("table_set_width") { it.put("width", (columns.getJSONObject(cIndex).getDouble("width") - 24).coerceAtLeast(60.0)) }; menu = false })
                DropdownMenuItem(text = { Text("Ampliar columna") }, onClick = { command("table_set_width") { it.put("width", columns.getJSONObject(cIndex).getDouble("width") + 24) }; menu = false })
                DropdownMenuItem(text = { Text("Restablecer ancho") }, onClick = { command("table_set_width") { it.put("width", 180) }; menu = false })
                for (numeric in listOf(false, true)) for (direction in listOf("asc", "desc")) DropdownMenuItem(text = { Text("Ordenar ${if (numeric) "números" else "texto"} ${if (direction == "asc") "ascendente" else "descendente"}") }, onClick = { command("table_sort") { it.put("numeric", numeric).put("direction", direction) }; menu = false })
                DropdownMenuItem(text = { Text("Limpiar selección") }, onClick = { selection { cell -> cell.put("children", JSONArray().put(documentParagraph())) }; menu = false })
                DropdownMenuItem(text = { Text("Eliminar fila") }, enabled = rows.length() > 1, onClick = { command("table_delete_row"); menu = false })
                DropdownMenuItem(text = { Text("Eliminar columna") }, enabled = columns.length() > 1, onClick = { command("table_delete_column"); menu = false })
            }
        }
        if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error)
    }
}

@Composable
private fun CanonicalInsert(objects: List<LumenObject>, onInsert: (JSONObject) -> Unit) {
    val importer=LocalDocumentMediaImporter.current
    val currentInsert by rememberUpdatedState(onInsert)
    val scope=rememberCoroutineScope()
    var importing by remember {mutableStateOf(false)}
    var importError by remember {mutableStateOf("")}
    val picker=rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()){uri->if(uri!=null&&importer!=null)scope.launch{
        importing=true
        runCatching{importer(uri)}.onSuccess {obj->
            val block=if(obj.typeId=="image") documentNode("image",JSONObject().put("asset_id",obj.assetId).put("object_id",obj.id).put("alignment","center").put("fit","contain"))
                else documentNode("object_embed",JSONObject().put("object_id",obj.id).put("view","embed"))
            currentInsert(block)
        }.onFailure{importError=it.message?:"No se pudo importar el archivo"}
        importing=false
    }}
    var expanded by remember { mutableStateOf(false) }
    Box {
        TextButton(onClick = { expanded = true }) { Text("Añadir bloque") }
        DropdownMenu(expanded, { expanded = false }) {
            DropdownMenuItem(text={Text(if(importing)"Importando…" else "Insertar imagen desde archivo")},enabled=importer!=null&&!importing,onClick={picker.launch(arrayOf("image/*"));expanded=false})
            DropdownMenuItem(text={Text("Insertar PDF desde archivo")},enabled=importer!=null&&!importing,onClick={picker.launch(arrayOf("application/pdf"));expanded=false})
            listOf("paragraph" to "Párrafo", "heading" to "Encabezado", "list_item" to "Lista", "task" to "Tarea", "quote" to "Cita", "group" to "Grupo", "toggle" to "Desplegable", "table" to "Tabla", "columns" to "Columnas", "code" to "Código", "math" to "LaTeX", "divider" to "Separador").forEach { (type, label) ->
                DropdownMenuItem(text = { Text(label) }, onClick = {
                    val node = when (type) {
                        "table" -> documentTable()
                        "columns" -> documentColumns()
                        "code", "math" -> documentNode(type, JSONObject().put("source", ""))
                        "group", "quote" -> documentNode(type, children = JSONArray().put(documentParagraph()))
                        "toggle" -> documentParagraph().put("type", "toggle").put("attrs", JSONObject().put("expanded", true)).put("children", JSONArray().put(documentParagraph()))
                        "list_item", "task" -> documentParagraph().put("type", "list_item").put("attrs", JSONObject().put("kind", if (type == "task") "task" else "bullet").apply { if (type == "task") put("checked", false) })
                        "heading" -> documentParagraph().put("type", "heading").put("attrs", JSONObject().put("level", 1))
                        else -> if (type == "divider") documentNode(type) else documentParagraph()
                    }
                    onInsert(node); expanded = false
                })
            }
            objects.take(20).forEach { obj -> DropdownMenuItem(text = { Text("Vincular: ${obj.title}") }, onClick = {
                onInsert(documentNode("object_embed", JSONObject().put("object_id", obj.id).put("view", "link_block"))); expanded = false
            }) }
        }
    }
    if(importError.isNotBlank())Text(importError,color=MaterialTheme.colorScheme.error)
}
