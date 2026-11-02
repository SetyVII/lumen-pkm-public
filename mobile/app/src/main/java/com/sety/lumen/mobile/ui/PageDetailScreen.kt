package com.sety.lumen.mobile.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.widget.Toast
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CalendarToday
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.Update
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.embeddedImageIds

@Composable
fun PageDetailScreen(
    obj: LumenObject,
    vm: LumenViewModel,
    onBack: () -> Unit,
) {
    val revision by vm.revision.collectAsStateWithLifecycle()
    val linkedImages = remember(obj.id, obj.links, revision) {
        val embedded = embeddedImageIds(obj)
        obj.links.filter { it !in embedded }.mapNotNull { vm.repository.getObject(it) }.filter { it.isImage }
    }
    var selectedTab by rememberSaveable { mutableIntStateOf(1) }
    val tabs = listOf("Propiedades", "Contenido", "Gráfico")

    var title by remember(obj.id, obj.title) { mutableStateOf(obj.title) }
    val initialContent = remember(obj.id, obj) { com.sety.lumen.mobile.data.editorDocumentText(obj) }
    var content by rememberSaveable(obj.id, stateSaver = TextFieldValue.Saver) {
        mutableStateOf(TextFieldValue(initialContent, TextRange(initialContent.length)))
    }
    androidx.compose.runtime.LaunchedEffect(obj.id, obj) {
        val canonical = com.sety.lumen.mobile.data.editorDocumentText(obj)
        if (content.text != canonical) {
            content = TextFieldValue(canonical, TextRange(content.selection.end.coerceAtMost(canonical.length)))
        }
    }
    var showDelete by remember { mutableStateOf(false) }

    val allTags = remember(obj.id, obj.revision) {
        val assignedTagIds = mutableSetOf<String>()
        val tagsArray = obj.properties.optJSONArray("tags")
        if (tagsArray != null) {
            for (i in 0 until tagsArray.length()) {
                val t = tagsArray.optString(i)
                if (t.isNotBlank()) assignedTagIds.add(t)
            }
        }
        assignedTagIds.addAll(obj.links)
        vm.objects().filter { it.typeId == "tag" && (it.id in assignedTagIds || it.title in assignedTagIds) }
    }

    val context = LocalContext.current
    val typeLabel = vm.types().firstOrNull { it.id == obj.typeId }?.name ?: LumenTypeVisuals.forType(obj.typeId).label
    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    if (showDelete) {
        ConfirmDialog(
            title = "¿Eliminar este objeto?",
            body = "Se quitará de este teléfono y de la sincronización. Esta acción no se puede deshacer.",
            confirmLabel = "Eliminar",
            onDismiss = { showDelete = false },
            onConfirm = {
                showDelete = false
                vm.deleteObject(obj.id)
                onBack()
            },
        )
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        topBar = {
            LumenObjectTopBar(
                tabs = tabs,
                selectedTab = selectedTab,
                onSelectTab = { selectedTab = it },
                onBack = onBack,
                trailing = {
                    LumenMoreButton(
                        pinned = obj.isPinned,
                        onTogglePin = { vm.updateProperty(obj.id, "pinned", !obj.isPinned) },
                        onCopyId = { copyId(context, obj.id) },
                        onDelete = { showDelete = true },
                    )
                },
            )
        },
    ) { padding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding),
        ) {
            when (selectedTab) {
                0 -> {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
                    ) {
                        item { ObjectProperties(obj, vm, exclude = emptySet()) }
                        item {
                            Text(
                                "Propiedades",
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onSurface,
                            )
                        }
                        item {
                            LumenPropertyRow("Tipo de objeto", typeLabel, icon = Icons.Default.Description)
                        }
                        item {
                            LumenPropertyRow(
                                "Creado",
                                obj.createdAt.take(10).ifBlank { "Hoy" },
                                icon = Icons.Default.CalendarToday,
                            )
                        }
                        item {
                            LumenPropertyRow(
                                "Última modificación",
                                obj.updatedAt.take(10).ifBlank { "Hoy" },
                                icon = Icons.Default.Update,
                            )
                        }
                        item {
                            LumenPropertyRow(
                                "Identificador",
                                obj.id.take(8),
                                icon = Icons.Default.Fingerprint,
                                showDivider = false,
                            )
                        }
                    }
                }
                1 -> {
                    Column(Modifier.fillMaxSize()) {
                        LazyColumn(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxWidth(),
                            contentPadding = PaddingValues(horizontal = 20.dp, vertical = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp),
                        ) {
                            item {
                                Row(
                                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                                    verticalAlignment = androidx.compose.ui.Alignment.CenterVertically,
                                ) {
                                    LumenTypeChip(obj.typeId, label = typeLabel, showSwap = true)
                                    LumenCollectionsChip()
                                }
                            }
                            item {
                                LumenTitleField(
                                    value = title,
                                    onValueChange = {
                                        title = it
                                        vm.repository.saveObject(obj.copy(title = it))
                                    },
                                )
                            }
                            item {
                                Row(
                                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                                    modifier = Modifier.horizontalScroll(rememberScrollState()),
                                ) {
                                    if (allTags.isEmpty()) {
                                        LumenTagChip("Etiquetas")
                                    } else {
                                        allTags.forEach { tag ->
                                            LumenTagChip(tag.title)
                                        }
                                    }
                                }
                            }
                            item { LocalAttachment(obj, vm) }
                            item {
                                LumenEditorField(
                                    value = content,
                                    onValueChange = {
                                        val changed = content.text != it.text
                                        content = it
                                        if (changed) vm.updateContent(obj.id, it.text)
                                    },
                                    source = obj,
                                    objects = vm.objects(),
                                    onOpenObject = { vm.openObject(it) },
                                    assetFor = vm::asset,
                                    onBlocksChange = { vm.updateBlocks(obj.id, it) },
                                    placeholder = "Escribe aquí…",
                                    minHeight = if (linkedImages.isEmpty()) 300.dp else 180.dp,
                                )
                            }
                            items(linkedImages, key = { "linked-image-${it.id}" }) { image ->
                                PhotoCard(
                                    asset = vm.asset(image.assetId),
                                    caption = image.content.ifBlank { image.title },
                                    onClick = { vm.openObject(image) },
                                    modifier = Modifier.padding(top = 12.dp),
                                )
                            }
                        }

                        // Canonical block controls are owned by CanonicalDocumentEditor.
                    }
                }
                2 -> {
                    ObjectRelations(obj, vm)
                }
            }
        }
    }
}

internal fun copyId(context: Context, id: String) {
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
    clipboard.setPrimaryClip(ClipData.newPlainText("ID", id))
    Toast.makeText(context, "ID copiado", Toast.LENGTH_SHORT).show()
}
