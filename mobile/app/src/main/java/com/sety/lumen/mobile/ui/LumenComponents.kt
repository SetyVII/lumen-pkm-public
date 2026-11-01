package com.sety.lumen.mobile.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.selection.selectable
import androidx.compose.ui.semantics.Role
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.CloudDone
import androidx.compose.material.icons.filled.CloudOff
import androidx.compose.material.icons.filled.CloudSync
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.DeleteOutline
import androidx.compose.material.icons.filled.Folder
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.Notes
import androidx.compose.material.icons.filled.PushPin
import androidx.compose.material.icons.filled.Tag
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.foundation.layout.absoluteOffset
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject

@Composable
fun LumenCircleButton(
    icon: ImageVector,
    contentDescription: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    size: Dp = 36.dp,
) {
    Surface(
        modifier = modifier
            .size(size)
            .clip(CircleShape)
            .clickable(onClick = onClick),
        shape = CircleShape,
        color = MaterialTheme.colorScheme.surfaceVariant,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(
                icon,
                contentDescription = contentDescription,
                tint = MaterialTheme.colorScheme.onSurface,
                modifier = Modifier.size(size * 0.52f),
            )
        }
    }
}

@Composable
fun SegmentedPill(
    tabs: List<String>,
    selectedIndex: Int,
    onSelect: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val compact = tabs.size >= 4
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(50),
        color = MaterialTheme.colorScheme.surface,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(3.dp),
            horizontalArrangement = Arrangement.SpaceEvenly,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            tabs.forEachIndexed { index, label ->
                val selected = index == selectedIndex
                Surface(
                    modifier = Modifier
                        .clip(RoundedCornerShape(50))
                        .selectable(selected = selected, role = Role.Tab, onClick = { onSelect(index) }),
                    shape = RoundedCornerShape(50),
                    color = if (selected) MaterialTheme.colorScheme.secondaryContainer else Color.Transparent,
                ) {
                    Text(
                        text = label,
                        modifier = Modifier.padding(
                            horizontal = if (compact) 7.dp else 11.dp,
                            vertical = 6.dp,
                        ),
                        style = if (compact) {
                            MaterialTheme.typography.labelSmall
                        } else {
                            MaterialTheme.typography.labelMedium
                        },
                        fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                        color = if (selected) {
                            MaterialTheme.colorScheme.onSecondaryContainer
                        } else {
                            MaterialTheme.colorScheme.onSurface
                        },
                        maxLines = 1,
                    )
                }
            }
        }
    }
}

@Composable
fun LumenObjectTopBar(
    tabs: List<String>,
    selectedTab: Int,
    onSelectTab: (Int) -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
    trailing: (@Composable () -> Unit)? = null,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surface)
            .statusBarsPadding(),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            LumenCircleButton(
                icon = Icons.AutoMirrored.Filled.ArrowBack,
                contentDescription = "Atrás",
                onClick = onBack,
            )
            SegmentedPill(
                tabs = tabs,
                selectedIndex = selectedTab,
                onSelect = onSelectTab,
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 8.dp),
            )
            if (trailing != null) {
                trailing()
            } else {
                Spacer(Modifier.size(36.dp))
            }
        }
    }
}

@Composable
fun LumenMoreButton(
    pinned: Boolean,
    onTogglePin: () -> Unit,
    onCopyId: () -> Unit,
    onDelete: () -> Unit,
    onCopyImage: (() -> Unit)? = null,
) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        LumenCircleButton(
            icon = Icons.Default.MoreVert,
            contentDescription = "Más acciones",
            onClick = { expanded = true },
        )
        DropdownMenu(
            expanded = expanded,
            onDismissRequest = { expanded = false },
            containerColor = MaterialTheme.colorScheme.surface,
        ) {
            DropdownMenuItem(
                text = { Text(if (pinned) "Quitar de fijados" else "Fijar en Inicio") },
                leadingIcon = { Icon(Icons.Default.PushPin, contentDescription = null) },
                onClick = {
                    expanded = false
                    onTogglePin()
                },
            )
            if (onCopyImage != null) {
                DropdownMenuItem(
                    text = { Text("Copiar imagen") },
                    leadingIcon = { Icon(Icons.Default.ContentCopy, contentDescription = null) },
                    onClick = {
                        expanded = false
                        onCopyImage()
                    },
                )
            }
            DropdownMenuItem(
                text = { Text("Copiar ID") },
                leadingIcon = { Icon(Icons.Default.Tag, contentDescription = null) },
                onClick = {
                    expanded = false
                    onCopyId()
                },
            )
            DropdownMenuItem(
                text = {
                    Text(
                        "Eliminar",
                        color = MaterialTheme.colorScheme.error,
                    )
                },
                leadingIcon = {
                    Icon(
                        Icons.Default.DeleteOutline,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.error,
                    )
                },
                onClick = {
                    expanded = false
                    onDelete()
                },
            )
        }
    }
}

@Composable
fun LumenPropertyRow(
    label: String,
    value: String,
    modifier: Modifier = Modifier,
    icon: ImageVector? = null,
    onClick: (() -> Unit)? = null,
    showDivider: Boolean = true,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (icon != null) {
                Icon(
                    icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(18.dp),
                )
                Spacer(Modifier.width(10.dp))
            }
            Text(
                label,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(0.9f),
            )
            Spacer(Modifier.width(12.dp))
            Text(
                value.ifBlank { "Vacío" },
                style = MaterialTheme.typography.bodyMedium,
                color = if (value.isBlank()) {
                    MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f)
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                textAlign = TextAlign.End,
                modifier = Modifier.weight(1.1f),
            )
            if (onClick != null) {
                Spacer(Modifier.width(4.dp))
                Icon(
                    Icons.AutoMirrored.Filled.KeyboardArrowRight,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.6f),
                    modifier = Modifier.size(16.dp),
                )
            }
        }
        if (showDivider) {
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.7f))
        }
    }
}

@Composable
fun LumenTagChip(
    text: String,
    modifier: Modifier = Modifier,
    icon: ImageVector? = Icons.Default.Tag,
    onClick: (() -> Unit)? = null,
) {
    Surface(
        modifier = modifier.then(
            if (onClick != null) Modifier.clip(RoundedCornerShape(8.dp)).clickable(onClick = onClick) else Modifier
        ),
        shape = RoundedCornerShape(8.dp),
        color = MaterialTheme.colorScheme.surfaceVariant,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 9.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            if (icon != null) {
                Icon(
                    icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(13.dp),
                )
                Spacer(Modifier.width(5.dp))
            }
            Text(
                text,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
fun LumenCollectionsChip(modifier: Modifier = Modifier) {
    LumenTagChip(
        text = "Colecciones",
        icon = Icons.Default.Folder,
        modifier = modifier,
    )
}

@Composable
fun LumenSectionHeader(
    title: String,
    modifier: Modifier = Modifier,
    trailing: String? = null,
    action: (@Composable () -> Unit)? = null,
) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.onSurface,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            if (trailing != null) {
                Text(
                    trailing,
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (action != null) {
                Spacer(Modifier.width(4.dp))
                action()
            }
        }
    }
}

@Composable
fun LumenEditorField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    minHeight: Dp = 220.dp,
    source: LumenObject? = null,
    objects: List<LumenObject> = emptyList(),
    onOpenObject: (LumenObject) -> Unit = {},
    assetFor: (String) -> com.sety.lumen.mobile.data.LumenAsset? = { null },
    renderNative: Boolean = true,
    onBlocksChange: ((org.json.JSONArray) -> Unit)? = null,
    blockTextStyle: androidx.compose.ui.text.TextStyle? = null,
) {
    var state by remember { mutableStateOf(TextFieldValue(value)) }
    androidx.compose.runtime.LaunchedEffect(value) {
        if (state.text != value) state = TextFieldValue(value, androidx.compose.ui.text.TextRange(state.selection.end.coerceAtMost(value.length)))
    }
    LumenEditorField(
        value = state,
        onValueChange = {
            val changed = state.text != it.text
            state = it
            if (changed) onValueChange(it.text)
        },
        placeholder = placeholder, modifier = modifier, minHeight = minHeight,
        source = source, objects = objects, onOpenObject = onOpenObject,
        assetFor = assetFor, renderNative = renderNative, onBlocksChange = onBlocksChange, blockTextStyle = blockTextStyle,
    )
}

@Composable
fun LumenEditorField(
    value: TextFieldValue,
    onValueChange: (TextFieldValue) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    minHeight: Dp = 220.dp,
    source: LumenObject? = null,
    objects: List<LumenObject> = emptyList(),
    onOpenObject: (LumenObject) -> Unit = {},
    assetFor: (String) -> com.sety.lumen.mobile.data.LumenAsset? = { null },
    renderNative: Boolean = true,
    onBlocksChange: ((org.json.JSONArray) -> Unit)? = null,
    blockTextStyle: androidx.compose.ui.text.TextStyle? = null,
) {
    val blocks = source?.properties?.optJSONArray("blocks")
    if (renderNative && source != null && blocks != null && blocks.length() > 0 &&
        com.sety.lumen.mobile.data.nativeDocumentText(blocks) == value.text) {
        NativeEditorContent(value, source, blocks, objects, onValueChange, onOpenObject, assetFor, modifier, onBlocksChange)
        return
    }
    val linkContext = androidx.compose.ui.platform.LocalContext.current
    val tokens = remember(value.text, source, objects) {
        com.sety.lumen.mobile.data.editorTokens(value.text, source, objects)
    }
    val foreground = MaterialTheme.colorScheme.onSurface
    val background = MaterialTheme.colorScheme.surfaceVariant
    val ruleColor = MaterialTheme.colorScheme.outline
    val presentation = remember(value.text, tokens, foreground, background) {
        EditorPresentation(value.text, tokens, foreground, background,
            blocks?.takeIf { it.length() == 1 }?.optJSONObject(0)?.optJSONArray("runs"))
    }
    var textLayout by remember { mutableStateOf<androidx.compose.ui.text.TextLayoutResult?>(null) }
    val density = androidx.compose.ui.platform.LocalDensity.current
    Column(modifier) {
    BasicTextField(
        value = value,
        onValueChange = { proposed ->
            // Treat hidden syntax as one object on backspace/delete. Otherwise
            // a single deletion can leave half of an invisible ID or divider.
            val edited = if (proposed.text.length == value.text.length - 1) {
                val firstDifference = value.text.zip(proposed.text).indexOfFirst { it.first != it.second }
                    .let { if (it < 0) proposed.text.length else it }
                val token = tokens.firstOrNull { firstDifference >= it.start && firstDifference < it.end }
                if (token == null) proposed else TextFieldValue(
                    value.text.removeRange(token.start, token.end), androidx.compose.ui.text.TextRange(token.start),
                )
            } else proposed
            onValueChange(edited)
        },
        visualTransformation = presentation,
        onTextLayout = { textLayout = it },
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = minHeight)
            .drawBehind {
                val layout = textLayout?.takeIf { it.layoutInput.text == presentation.transformed.text } ?: return@drawBehind
                presentation.tokens.filter { it.source.kind == com.sety.lumen.mobile.data.EditorTokenKind.DIVIDER }.forEach { token ->
                    val line = layout.getLineForOffset(token.start)
                    val y = (layout.getLineTop(line) + layout.getLineBottom(line)) / 2f
                    drawLine(ruleColor, androidx.compose.ui.geometry.Offset(0f, y),
                        androidx.compose.ui.geometry.Offset(size.width, y), strokeWidth = 1.dp.toPx())
                }
            },
        textStyle = (blockTextStyle ?: MaterialTheme.typography.bodyLarge).copy(color = foreground),
        cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
        decorationBox = { innerTextField ->
            Box(Modifier.fillMaxWidth()) {
                if (value.text.isEmpty()) {
                    Text(placeholder, style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.65f))
                }
                innerTextField()
                val layout = textLayout?.takeIf { it.layoutInput.text == presentation.transformed.text }
                if (layout != null) {
                    presentation.tokens.filter { it.source.kind in setOf(com.sety.lumen.mobile.data.EditorTokenKind.REFERENCE, com.sety.lumen.mobile.data.EditorTokenKind.LINK) }.forEach { token ->
                        val target = objects.firstOrNull { it.id == token.source.objectId }
                        if (target != null || token.source.url.isNotBlank()) {
                            val firstLine = layout.getLineForOffset(token.start)
                            val lastLine = layout.getLineForOffset(token.end - 1)
                            for (line in firstLine..lastLine) {
                                val start = maxOf(token.start, layout.getLineStart(line))
                                val end = minOf(token.end, layout.getLineEnd(line, visibleEnd = true))
                                if (start < end) {
                                    val boxes = (start until end).map { layout.getBoundingBox(it) }
                                    val left = boxes.minOf { it.left }
                                    val right = boxes.maxOf { it.right }
                                    val top = layout.getLineTop(line)
                                    val bottom = layout.getLineBottom(line)
                                    Box(Modifier.absoluteOffset {
                                        androidx.compose.ui.unit.IntOffset(left.toInt(), top.toInt())
                                    }.size(with(density) { (right - left).toDp() }, with(density) { (bottom - top).toDp() })
                                        .semantics { contentDescription = if (target != null) "Abrir objeto: ${target.title}" else "Abrir enlace: ${token.source.label}" }
                                        .clickable {
                                            if (target != null) onOpenObject(target)
                                            else runCatching { linkContext.startActivity(android.content.Intent(android.content.Intent.ACTION_VIEW, android.net.Uri.parse(token.source.url))) }
                                        })
                                }
                            }
                        }
                    }
                }
            }
        },
    )
    if (renderNative && onBlocksChange != null) BlockInsertButton { block ->
        val existing = com.sety.lumen.mobile.data.reconcileNativeDocument(source ?: LumenObject("", "note", ""), value.text, objects)
        onBlocksChange(existing.put(block))
    }
    }
}

@Composable
fun LumenTitleField(
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
    placeholder: String = "Título",
) {
    BasicTextField(
        value = value,
        onValueChange = onValueChange,
        modifier = modifier.fillMaxWidth(),
        textStyle = MaterialTheme.typography.headlineMedium.copy(
            color = MaterialTheme.colorScheme.onSurface,
        ),
        cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
        maxLines = 3,
        decorationBox = { innerTextField ->
            Box(Modifier.fillMaxWidth()) {
                if (value.isEmpty()) {
                    Text(
                        placeholder,
                        style = MaterialTheme.typography.headlineMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f),
                    )
                }
                innerTextField()
            }
        },
    )
}

@Composable
fun LumenObjectRow(
    obj: LumenObject,
    subtitle: String?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    showDivider: Boolean = true,
) {
    Column(modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            LumenTypeBadge(obj.typeId, size = 30.dp, iconSize = 16.dp)
            Spacer(Modifier.width(12.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    obj.title.ifBlank { "Sin título" },
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurface,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                if (!subtitle.isNullOrBlank()) {
                    Text(
                        subtitle,
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Icon(
                Icons.AutoMirrored.Filled.KeyboardArrowRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f),
                modifier = Modifier.size(18.dp),
            )
        }
        if (showDivider) {
            HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
        }
    }
}

@Composable
fun LumenEmptyState(
    title: String,
    body: String,
    modifier: Modifier = Modifier,
    icon: ImageVector = Icons.Default.Notes,
) {
    Column(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 48.dp, horizontal = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Surface(
            shape = CircleShape,
            color = MaterialTheme.colorScheme.surfaceVariant,
            modifier = Modifier.size(56.dp),
        ) {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(24.dp),
                )
            }
        }
        Spacer(Modifier.size(14.dp))
        Text(
            title,
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.onSurface,
            textAlign = TextAlign.Center,
        )
        Spacer(Modifier.size(6.dp))
        Text(
            body,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
    }
}

@Composable
fun LumenSyncLine(message: String, pending: Int, modifier: Modifier = Modifier) {
    val isError = message.contains("No se", true) || message.contains("Error", true)
    val color = when {
        isError -> MaterialTheme.colorScheme.error
        pending > 0 -> MaterialTheme.colorScheme.secondary
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }
    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            when {
                pending > 0 -> Icons.Default.CloudSync
                isError -> Icons.Default.CloudOff
                else -> Icons.Default.CloudDone
            },
            contentDescription = null,
            tint = color,
            modifier = Modifier.size(14.dp),
        )
        Spacer(Modifier.width(6.dp))
        Text(
            if (pending > 0) "$pending cambio(s) local(es)" else message.ifBlank { "Sincronizado" },
            style = MaterialTheme.typography.labelSmall,
            color = color,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
fun LumenGraphView(
    centerTitle: String,
    centerType: String,
    connections: List<String>,
) {
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
        contentAlignment = Alignment.Center,
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
            modifier = Modifier.padding(horizontal = 20.dp),
        ) {
            Surface(
                shape = CircleShape,
                color = MaterialTheme.colorScheme.surface,
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
                modifier = Modifier.size(108.dp),
            ) {
                Column(
                    modifier = Modifier.padding(10.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    LumenTypeBadge(centerType, size = 26.dp, iconSize = 14.dp)
                    Spacer(Modifier.size(6.dp))
                    Text(
                        centerTitle.ifBlank { "Objeto" }.take(18),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurface,
                        textAlign = TextAlign.Center,
                        maxLines = 2,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
            Text(
                "Conexiones bidireccionales",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally),
            ) {
                connections.forEach { connection ->
                    LumenTagChip(connection, icon = null)
                }
            }
        }
    }
}
