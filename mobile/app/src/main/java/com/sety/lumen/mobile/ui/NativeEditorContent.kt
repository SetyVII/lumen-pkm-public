package com.sety.lumen.mobile.ui

import androidx.compose.ui.draw.drawBehind
import androidx.compose.material.icons.filled.ArrowDropDown
import androidx.compose.material.icons.automirrored.filled.ArrowRight
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil.compose.AsyncImage
import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

@Composable
fun NativeEditorContent(
    value: TextFieldValue,
    source: LumenObject,
    blocks: JSONArray,
    objects: List<LumenObject>,
    onValueChange: (TextFieldValue) -> Unit,
    onOpenObject: (LumenObject) -> Unit,
    assetFor: (String) -> LumenAsset?,
    modifier: Modifier = Modifier,
    onBlocksChange: ((JSONArray) -> Unit)? = null,
) {
    val spans = nativeBlockSpans(blocks)
    Column(modifier.fillMaxWidth()) {
        spans.forEachIndexed { index, span ->
            key(span.block.optString("id").ifBlank { "block-$index" }) {
                val block = span.block
                val type = block.optString("type")
                val runs = block.optJSONArray("runs") ?: JSONArray()
                val images = (0 until runs.length()).mapNotNull { runs.optJSONObject(it) }
                    .filter { it.optString("type") == "image" && it.optString("image_name").startsWith("lumen://image/") }
                when {
                    type == "toggle" -> NativeToggleEditor(block, source, objects, onOpenObject, assetFor,
                        onChange = onBlocksChange?.let { save -> { replacement -> save(replaceNativeBlock(blocks, index, replacement)) } })
                    images.isNotEmpty() -> {
                        var runOffset = 0
                        for (r in 0 until runs.length()) {
                            val run = runs.getJSONObject(r)
                            val body = run.optString("text").replace('\u2028', '\n')
                            val runStart = span.start + runOffset
                            runOffset += body.length
                            if (run.optString("type") == "image") {
                        val id = run.optString("object_id").ifBlank { run.optString("image_name").removePrefix("lumen://image/") }
                        val image = objects.firstOrNull { it.id == id }
                        val asset = image?.assetId?.takeIf(String::isNotBlank)?.let(assetFor)
                        val path = asset?.localPath?.takeIf { it.isNotBlank() && File(it).exists() }
                            ?: asset?.thumbPath?.takeIf { it.isNotBlank() && File(it).exists() }
                        val width = run.optDouble("width", 0.0)
                        val height = run.optDouble("height", 0.0)
                        val ratio = if (width > 0 && height > 0) (width / height).toFloat() else 1.5f
                        if (path != null) {
                            AsyncImage(
                                model = rememberMediaModel(path), contentDescription = image?.title ?: "Imagen incrustada",
                                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp).aspectRatio(ratio)
                                    .clickable(enabled = image != null) { image?.let(onOpenObject) },
                                contentScale = ContentScale.Fit,
                            )
                        } else {
                            Text("Imagen pendiente de descarga", Modifier.fillMaxWidth().padding(vertical = 20.dp),
                                color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                            } else if (body.isNotEmpty()) {
                                val blockSource = source.copy(content = body, properties = JSONObject().put("blocks", JSONArray().put(
                                    JSONObject(block.toString()).put("text", body).put("runs", JSONArray().put(run)))))
                                LumenEditorField(TextFieldValue(body, TextRange((value.selection.end - runStart).coerceIn(0, body.length))),
                                    onValueChange = { edit -> onValueChange(TextFieldValue(value.text.replaceRange(runStart, runStart + body.length, edit.text),
                                        TextRange(runStart + edit.selection.start, runStart + edit.selection.end))) },
                                    placeholder = "", minHeight = 28.dp, source = blockSource, objects = objects,
                                    onOpenObject = onOpenObject, assetFor = assetFor, renderNative = false)
                            }
                        }
                    }
                    type == "divider" -> HorizontalDivider(Modifier.padding(vertical = 12.dp))
                    type == "table" -> NativeTableEditor(block, source, objects, onOpenObject, assetFor,
                        onChange = onBlocksChange?.let { save -> { replacement -> save(replaceNativeBlock(blocks, index, replacement)) } })
                    type == "columns" || type == "group" -> NativeColumnsEditor(block, source, objects, onOpenObject, assetFor,
                        onChange = onBlocksChange?.let { save -> { replacement -> save(replaceNativeBlock(blocks, index, replacement)) } })
                    else -> {
                        val raw = nativeBlockText(block)
                        val todoPrefix = if (type == "todo" && (raw.startsWith("○ ") || raw.startsWith("✔ "))) 2 else 0
                        val body = if (type == "code") block.optString("text").replace('\u2028', '\n') else raw.drop(todoPrefix)
                        val start = span.start + span.prefix + todoPrefix
                        val end = span.end - span.suffix
                        val localSelection = TextRange((value.selection.start - start).coerceIn(0, body.length),
                            (value.selection.end - start).coerceIn(0, body.length))
                        val indent = block.optInt("indent", 1).coerceIn(1, 8)
                        val baseStyle = MaterialTheme.typography.bodyLarge
                        val style: TextStyle = when (type) {
                            "heading" -> baseStyle.copy(fontSize = when (block.optInt("level", 1)) { 1 -> 28.sp; 2 -> 24.sp; 3 -> 21.sp; else -> 18.sp }, fontWeight = FontWeight.SemiBold)
                            "small" -> baseStyle.copy(fontSize = 13.sp)
                            "code" -> baseStyle.copy(fontFamily = FontFamily.Monospace)
                            else -> baseStyle
                        }.let { style ->
                            val alignment = block.optInt("alignment", 1)
                            style.copy(textAlign = when { alignment and 4 != 0 -> TextAlign.Center; alignment and 2 != 0 -> TextAlign.End; else -> TextAlign.Start })
                        }
                        Row(Modifier.fillMaxWidth().padding(start = if (type.endsWith("_list")) ((indent - 1) * 16).dp else 0.dp,
                            top = 3.dp, bottom = 3.dp)) {
                            when (type) {
                                "todo" -> Checkbox(checked = raw.startsWith("✔ "), onCheckedChange = { checked ->
                                    val updated = value.text.replaceRange(span.start, span.start + todoPrefix, if (checked) "✔ " else "○ ")
                                    onValueChange(value.copy(text = updated, selection = TextRange(value.selection.end.coerceAtMost(updated.length))))
                                }, modifier = Modifier.size(32.dp))
                                "bullet_list" -> Text("•", Modifier.width(22.dp), style = baseStyle)
                                "numbered_list" -> {
                                    val number = spans.take(index + 1).reversed().takeWhile { it.block.optString("type") == type && it.block.optInt("indent", 1) == indent }.size
                                    Text("$number.", Modifier.width(28.dp), style = baseStyle)
                                }
                                "alphabetic_list" -> Text("${('a'.code + index % 26).toChar()}.", Modifier.width(28.dp), style = baseStyle)
                                "quote" -> Box(Modifier.padding(end = 12.dp).width(3.dp).height(24.dp).background(MaterialTheme.colorScheme.outline))
                            }
                            val visibleBlock = if (todoPrefix > 0) JSONObject(block.toString()).apply {
                                put("text", body)
                                val visibleRuns = JSONArray()
                                var skip = todoPrefix
                                for (r in 0 until runs.length()) {
                                    val run = runs.getJSONObject(r)
                                    val text = run.optString("text")
                                    val drop = minOf(skip, text.length); skip -= drop
                                    if (drop < text.length) visibleRuns.put(JSONObject(run.toString()).put("text", text.drop(drop)))
                                }
                                put("runs", visibleRuns)
                            } else block
                            val blockSource = source.copy(content = body, properties = JSONObject().put("blocks", JSONArray().put(visibleBlock)))
                            LumenEditorField(
                                value = TextFieldValue(body, localSelection),
                                onValueChange = { edit ->
                                    val updated = value.text.replaceRange(start, end, edit.text)
                                    onValueChange(TextFieldValue(updated, TextRange(start + edit.selection.start, start + edit.selection.end),
                                        edit.composition?.let { TextRange(start + it.start, start + it.end) }))
                                },
                                placeholder = if (body.isEmpty()) "Escribe aquí…" else "",
                                modifier = Modifier.weight(1f), minHeight = 28.dp,
                                source = blockSource, objects = objects, onOpenObject = onOpenObject,
                                assetFor = assetFor, renderNative = false, blockTextStyle = style,
                            )
                        }
                    }
                }
                if (type in setOf("table", "columns", "group") && onBlocksChange != null) {
                    var deleting by remember { mutableStateOf(false) }
                    TextButton(onClick = { deleting = true }) { Text(if (type == "table") "Eliminar tabla" else "Eliminar grupo de bloques") }
                    if (deleting) AlertDialog(onDismissRequest = { deleting = false }, title = { Text("¿Eliminar este bloque?") },
                        text = { Text("Se eliminará también su contenido.") }, confirmButton = { TextButton(onClick = {
                            deleting = false
                            val updated = JSONArray(blocks.toString()); updated.remove(index)
                            if (updated.length() == 0) updated.put(nativeParagraph())
                            onBlocksChange(updated)
                        }) { Text("Eliminar") } }, dismissButton = { TextButton(onClick = { deleting = false }) { Text("Cancelar") } })
                }
            }
        }
        if (onBlocksChange != null) BlockInsertButton { onBlocksChange(JSONArray(blocks.toString()).put(it)) }
    }
}


@Composable
private fun NativeToggleEditor(
    block: JSONObject, source: LumenObject, objects: List<LumenObject>,
    onOpenObject: (LumenObject) -> Unit, assetFor: (String) -> LumenAsset?,
    onChange: ((JSONObject) -> Unit)?,
) {
    val raw = block.optString("text")
    val title = raw.removePrefix("▶ ").removePrefix("▼ ")
    val expanded = toggleIsExpanded(block)
    val titleBlock = JSONObject(block.toString()).put("text", title).put("runs", editedRuns(block, title)).apply { remove("children") }
    var titleValue by remember(block.optString("id")) { mutableStateOf(TextFieldValue(title)) }
    LaunchedEffect(title) { if (titleValue.text != title) titleValue = titleValue.copy(text = title) }
    Column(Modifier.fillMaxWidth()) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
            IconButton(onClick = { onChange?.invoke(setToggleExpanded(block, !expanded)) }, enabled = onChange != null,
                modifier = Modifier.size(40.dp)) {
                Icon(if (expanded) androidx.compose.material.icons.Icons.Default.ArrowDropDown else androidx.compose.material.icons.Icons.AutoMirrored.Filled.ArrowRight,
                    contentDescription = if (expanded) "Contraer toggle" else "Expandir toggle")
            }
            if (onChange == null) Text(title, style = MaterialTheme.typography.bodyLarge)
            else LumenEditorField(titleValue, { edit ->
                titleValue = edit
                if (edit.text != title) {
                    val lines = edit.text.split('\n', limit = 2)
                    val next = editToggleTitle(block, lines[0], expanded = if (lines.size > 1) true else expanded)
                    if (lines.size > 1) {
                        val children = next.optJSONArray("children") ?: JSONArray()
                        children.put(nativeParagraph(lines[1])); next.put("children", children)
                        titleValue = TextFieldValue(lines[0])
                    }
                    onChange(next)
                }
            }, placeholder = "Título del toggle", modifier = Modifier.weight(1f), minHeight = 28.dp,
                source = source.copy(content = title, properties = JSONObject().put("blocks", JSONArray().put(titleBlock))), objects = objects,
                onOpenObject = onOpenObject, assetFor = assetFor, renderNative = false)
        }
        if (expanded) {
            val children = block.optJSONArray("children") ?: JSONArray()
            val childSource = source.copy(content = nativeDocumentText(children), properties = JSONObject().put("blocks", children))
            var childValue by remember(block.optString("id")) { mutableStateOf(TextFieldValue(childSource.content)) }
            LaunchedEffect(childSource.content) { if (childValue.text != childSource.content) childValue = childValue.copy(text = childSource.content) }
            val outline = MaterialTheme.colorScheme.outlineVariant
            Column(Modifier.padding(start = 20.dp).drawBehind {
                drawLine(outline, androidx.compose.ui.geometry.Offset.Zero, androidx.compose.ui.geometry.Offset(0f, size.height), 1.dp.toPx())
            }.padding(start = 20.dp)) {
                fun save(next: JSONArray) { onChange?.invoke(JSONObject(block.toString()).put("children", next)) }
                NativeEditorContent(childValue, childSource, children, objects, onValueChange = { edit ->
                    childValue = edit
                    if (edit.text != childSource.content) save(reconcileNativeDocument(childSource, edit.text, objects))
                }, onOpenObject = onOpenObject, assetFor = assetFor,
                    onBlocksChange = onChange?.let { { next -> save(next) } })
            }
        }
    }
}
