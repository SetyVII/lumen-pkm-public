package com.sety.lumen.mobile.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.FormatIndentDecrease
import androidx.compose.material.icons.automirrored.filled.FormatIndentIncrease
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.AlternateEmail
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Code
import androidx.compose.material.icons.filled.FormatQuote
import androidx.compose.material.icons.filled.KeyboardHide
import androidx.compose.material.icons.filled.Notes
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Title
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.objectMentionText
import com.sety.lumen.mobile.ui.theme.LumenColors

fun indentLine(tfv: TextFieldValue): TextFieldValue {
    val text = tfv.text
    val cursor = tfv.selection.start
    val lastNewline = text.lastIndexOf('\n', (cursor - 1).coerceAtLeast(0))
    val lineStart = if (lastNewline == -1) 0 else lastNewline + 1
    val newText = text.substring(0, lineStart) + "  " + text.substring(lineStart)
    return tfv.copy(text = newText, selection = TextRange(cursor + 2))
}

fun outdentLine(tfv: TextFieldValue): TextFieldValue {
    val text = tfv.text
    val cursor = tfv.selection.start
    val lastNewline = text.lastIndexOf('\n', (cursor - 1).coerceAtLeast(0))
    val lineStart = if (lastNewline == -1) 0 else lastNewline + 1
    val spacesToRemove = when {
        text.startsWith("  ", lineStart) -> 2
        text.startsWith(" ", lineStart) -> 1
        else -> 0
    }
    if (spacesToRemove == 0) return tfv
    val newText = text.substring(0, lineStart) + text.substring(lineStart + spacesToRemove)
    val newCursor = (cursor - spacesToRemove).coerceAtLeast(lineStart)
    return tfv.copy(text = newText, selection = TextRange(newCursor))
}

fun insertSnippet(tfv: TextFieldValue, snippet: String): TextFieldValue {
    val text = tfv.text
    val start = tfv.selection.min
    val end = tfv.selection.max
    val insertion = if (snippet == "---\n" && start > 0 && text[start - 1] != '\n') "\n$snippet" else snippet
    val newText = text.substring(0, start) + insertion + text.substring(end)
    val newCursor = start + insertion.length
    return tfv.copy(text = newText, selection = TextRange(newCursor))
}

fun indentLine(text: String): String = indentLine(TextFieldValue(text, TextRange(text.length))).text
fun outdentLine(text: String): String = outdentLine(TextFieldValue(text, TextRange(text.length))).text
fun insertSnippet(text: String, snippet: String): String = insertSnippet(TextFieldValue(text, TextRange(text.length)), snippet).text

@Composable
fun EditorKeyboardBar(
    vm: LumenViewModel,
    onInsertText: (String) -> Unit,
    onIndent: () -> Unit = {},
    onOutdent: () -> Unit = {},
    modifier: Modifier = Modifier,
) {
    val keyboardController = LocalSoftwareKeyboardController.current
    var showMentionDialog by remember { mutableStateOf(false) }
    var showImageDialog by remember { mutableStateOf(false) }
    if (showImageDialog) ObjectMentionDialog(vm, onDismiss = { showImageDialog = false },
        onSelect = { onInsertText("\n![${it.title.replace("]", "\\]")}](lumen://object/${it.id})\n"); showImageDialog = false }, typeFilter = "image")
    var showSlashDialog by remember { mutableStateOf(false) }

    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    if (showMentionDialog) {
        ObjectMentionDialog(
            vm = vm,
            onDismiss = { showMentionDialog = false },
            onSelect = { selectedObj ->
                onInsertText(objectMentionText(selectedObj))
                showMentionDialog = false
            },
        )
    }

    if (showSlashDialog) {
        SlashMenuDialog(
            onDismiss = { showSlashDialog = false },
            onSelect = { blockSnippet ->
                onInsertText(blockSnippet)
                showSlashDialog = false
            },
        )
    }

    if (!imeVisible) return

    Column(
        modifier = modifier
            .fillMaxWidth()
            .semantics { contentDescription = "Barra de edición" },
    ) {
        HorizontalDivider(color = LumenColors.EditorBarBorder)
        Surface(
            color = LumenColors.EditorBar,
            shadowElevation = 8.dp,
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 6.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween,
            ) {
                Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                    IconButton(onClick = onOutdent, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.AutoMirrored.Filled.FormatIndentDecrease,
                            contentDescription = "Desangrar",
                            tint = Color(0xFFE4E3E2),
                            modifier = Modifier.size(20.dp),
                        )
                    }
                    IconButton(onClick = onIndent, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.AutoMirrored.Filled.FormatIndentIncrease,
                            contentDescription = "Sangrar",
                            tint = Color(0xFFE4E3E2),
                            modifier = Modifier.size(20.dp),
                        )
                    }
                    IconButton(onClick = { onInsertText("# ") }, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.Default.Title,
                            contentDescription = "Encabezado",
                            tint = Color(0xFFE4E3E2),
                            modifier = Modifier.size(20.dp),
                        )
                    }
                    IconButton(onClick = { showSlashDialog = true }, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.Default.Add,
                            contentDescription = "Insertar bloque",
                            tint = Color(0xFFE4E3E2),
                            modifier = Modifier.size(20.dp),
                        )
                    }
                    IconButton(onClick = { showImageDialog = true }, modifier = Modifier.size(36.dp)) {
                        Icon(Icons.Default.Image, contentDescription = "Insertar imagen", tint = MaterialTheme.colorScheme.primary)
                    }
                    IconButton(onClick = { showMentionDialog = true }, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.Default.AlternateEmail,
                            contentDescription = "Vincular objeto",
                            tint = MaterialTheme.colorScheme.primary,
                            modifier = Modifier.size(20.dp),
                        )
                    }
                    IconButton(onClick = { onInsertText("- [ ] ") }, modifier = Modifier.size(36.dp)) {
                        Icon(
                            Icons.Default.CheckCircle,
                            contentDescription = "Tarea checklist",
                            tint = Color(0xFFE4E3E2),
                            modifier = Modifier.size(20.dp),
                        )
                    }
                }

                IconButton(
                    onClick = { keyboardController?.hide() },
                    modifier = Modifier.size(36.dp),
                ) {
                    Icon(
                        Icons.Default.KeyboardHide,
                        contentDescription = "Ocultar teclado",
                        tint = Color(0xFFE4E3E2),
                        modifier = Modifier.size(20.dp),
                    )
                }
            }
        }
    }
}

@Composable
fun ObjectMentionDialog(
    vm: LumenViewModel,
    onDismiss: () -> Unit,
    onSelect: (LumenObject) -> Unit,
    typeFilter: String? = null,
) {
    var query by remember { mutableStateOf("") }
    val revision by vm.revision.collectAsStateWithLifecycle()
    val objects = remember(query, revision, typeFilter) {
        vm.objects().filter { (typeFilter == null || it.typeId == typeFilter) && it.title.contains(query, true) }.take(80)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = MaterialTheme.colorScheme.surface,
        title = {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    Icons.Default.AlternateEmail,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                )
                Spacer(Modifier.width(8.dp))
                Text(
                    if (typeFilter == "image") "Insertar imagen" else "Vincular objeto",
                    style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }
        },
        text = {
            Column(Modifier.fillMaxWidth()) {
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    leadingIcon = { Icon(Icons.Default.Search, contentDescription = null) },
                    placeholder = { Text("Buscar libros, enlaces, notas…") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )

                Spacer(Modifier.padding(vertical = 6.dp))

                LazyColumn(
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 280.dp),
                ) {
                    if (objects.isEmpty()) {
                        item {
                            Text(
                                "No se encontraron objetos",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    } else {
                        items(objects, key = { it.id }) { obj ->
                            Column {
                                Row(
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .clickable { onSelect(obj) }
                                        .padding(vertical = 9.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    LumenTypeBadge(obj.typeId, size = 28.dp, iconSize = 15.dp)
                                    Spacer(Modifier.width(10.dp))
                                    Column(Modifier.weight(1f)) {
                                        Text(
                                            obj.title.ifBlank { "Sin título" },
                                            style = MaterialTheme.typography.bodyMedium,
                                            fontWeight = FontWeight.SemiBold,
                                            color = MaterialTheme.colorScheme.onSurface,
                                        )
                                        Text(
                                            LumenTypeVisuals.forType(obj.typeId).label,
                                            style = MaterialTheme.typography.labelSmall,
                                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                                        )
                                    }
                                }
                                HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cerrar")
            }
        },
    )
}

@Composable
fun SlashMenuDialog(
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit,
) {
    val items = listOf(
        Triple("Encabezado H1", "# ", Icons.Default.Title),
        Triple("Encabezado H2", "## ", Icons.Default.Title),
        Triple("Lista de tareas", "- [ ] ", Icons.Default.CheckCircle),
        Triple("Cita textual", "> ", Icons.Default.FormatQuote),
        Triple("Bloque de código", "```\n\n```", Icons.Default.Code),
        Triple("Separador divisor", "---\n", Icons.Default.Notes),
    )

    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = MaterialTheme.colorScheme.surface,
        title = {
            Text(
                "Insertar bloque",
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.onSurface,
            )
        },
        text = {
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(max = 300.dp),
            ) {
                items(items) { (label, snippet, icon) ->
                    Column {
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onSelect(snippet) }
                                .padding(vertical = 10.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Surface(
                                shape = CircleShape,
                                color = MaterialTheme.colorScheme.surfaceVariant,
                                modifier = Modifier.size(30.dp),
                            ) {
                                Box(contentAlignment = Alignment.Center) {
                                    Icon(
                                        icon,
                                        contentDescription = null,
                                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                        modifier = Modifier.size(17.dp),
                                    )
                                }
                            }
                            Spacer(Modifier.width(12.dp))
                            Text(
                                label,
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.Medium,
                                color = MaterialTheme.colorScheme.onSurface,
                            )
                        }
                        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
                    }
                }
            }
        },
        confirmButton = {},
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Cancelar")
            }
        },
    )
}

@Composable
fun InlineObjectChip(
    title: String,
    typeId: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    LumenTypeChip(
        typeId = typeId,
        label = title,
        modifier = modifier,
        onClick = onClick,
    )
}
