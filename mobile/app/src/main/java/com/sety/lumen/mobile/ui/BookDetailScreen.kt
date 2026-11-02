package com.sety.lumen.mobile.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.AlternateEmail
import androidx.compose.material.icons.filled.CalendarToday
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.MenuBook
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.Update
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import com.sety.lumen.mobile.data.LumenObject
import java.io.File

@Composable
fun BookDetailScreen(
    obj: LumenObject,
    vm: LumenViewModel,
    onBack: () -> Unit,
) {
    var selectedTab by rememberSaveable { mutableIntStateOf(1) }
    val tabs = listOf("Propiedades", "Contenido", "Gráfico")

    var title by remember(obj.id, obj.title) { mutableStateOf(obj.title) }
    var author by remember(obj.id, obj.author) { mutableStateOf(obj.author) }
    var rating by remember(obj.id, obj.rating) { mutableStateOf(obj.rating) }
    var recommendedBy by remember(obj.id, obj.recommendedBy) { mutableStateOf(obj.recommendedBy) }
    var mediaType by remember(obj.id, obj.mediaType) { mutableStateOf(obj.mediaType.ifBlank { "Físico" }) }
    var coverUrl by remember(obj.id, obj.coverUrl) { mutableStateOf(obj.coverUrl) }
    val initialNotes = remember(obj.id, obj) { com.sety.lumen.mobile.data.editorDocumentText(obj) }
    var readingNotes by rememberSaveable(obj.id, stateSaver = TextFieldValue.Saver) {
        mutableStateOf(TextFieldValue(initialNotes, TextRange(initialNotes.length)))
    }

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

    var editPropertyKey by remember { mutableStateOf<String?>(null) }
    var editPropertyValue by remember { mutableStateOf("") }
    androidx.compose.runtime.LaunchedEffect(obj.id, obj) {
        val canonical = com.sety.lumen.mobile.data.editorDocumentText(obj)
        if (readingNotes.text != canonical) {
            readingNotes = TextFieldValue(canonical, TextRange(readingNotes.selection.end.coerceAtMost(canonical.length)))
        }
    }
    var showDelete by remember { mutableStateOf(false) }

    val context = LocalContext.current
    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    if (editPropertyKey != null) {
        AlertDialog(
            onDismissRequest = { editPropertyKey = null },
            containerColor = MaterialTheme.colorScheme.surface,
            title = {
                Text(
                    "Editar $editPropertyKey",
                    color = MaterialTheme.colorScheme.onSurface,
                )
            },
            text = {
                OutlinedTextField(
                    value = editPropertyValue,
                    onValueChange = { editPropertyValue = it },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        val key = editPropertyKey ?: return@TextButton
                        val value = editPropertyValue
                        when (key) {
                            "Autor" -> {
                                author = value
                                vm.updateProperty(obj.id, "author", value)
                            }
                            "Calificación" -> {
                                rating = value
                                vm.updateProperty(obj.id, "rating", value)
                            }
                            "Recomendado por" -> {
                                recommendedBy = value
                                vm.updateProperty(obj.id, "recommended_by", value)
                            }
                            "Medio" -> {
                                mediaType = value
                                vm.updateProperty(obj.id, "media_type", value)
                            }
                            "Portada" -> {
                                coverUrl = value
                                vm.updateProperty(obj.id, "cover_url", value)
                            }
                        }
                        editPropertyKey = null
                    },
                ) {
                    Text("Guardar", color = MaterialTheme.colorScheme.primary)
                }
            },
            dismissButton = {
                TextButton(onClick = { editPropertyKey = null }) {
                    Text("Cancelar", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            },
        )
    }

    if (showDelete) {
        ConfirmDialog(
            title = "¿Eliminar este libro?",
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
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(MaterialTheme.colorScheme.surface),
            ) {
                if (coverUrl.isNotBlank()) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(240.dp)
                            .background(MaterialTheme.colorScheme.background),
                    ) {
                        AsyncImage(
                            model = if (coverUrl.startsWith("/")) rememberMediaModel(coverUrl) else coverUrl,
                            contentDescription = title,
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Fit,
                        )
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .background(
                                    Brush.verticalGradient(
                                        colors = listOf(
                                            Color.Black.copy(alpha = 0.55f),
                                            Color.Transparent,
                                            MaterialTheme.colorScheme.surface,
                                        )
                                    )
                                ),
                        )
                        BookTopBarRow(
                            tabs = tabs,
                            selectedTab = selectedTab,
                            onSelectTab = { selectedTab = it },
                            onBack = onBack,
                            pinned = obj.isPinned,
                            onTogglePin = { vm.updateProperty(obj.id, "pinned", !obj.isPinned) },
                            onCopyId = { copyId(context, obj.id) },
                            onDelete = { showDelete = true },
                            modifier = Modifier
                                .align(Alignment.TopCenter)
                                .statusBarsPadding(),
                        )
                    }
                } else {
                    BookTopBarRow(
                        tabs = tabs,
                        selectedTab = selectedTab,
                        onSelectTab = { selectedTab = it },
                        onBack = onBack,
                        pinned = obj.isPinned,
                        onTogglePin = { vm.updateProperty(obj.id, "pinned", !obj.isPinned) },
                        onCopyId = { copyId(context, obj.id) },
                        onDelete = { showDelete = true },
                        modifier = Modifier.statusBarsPadding(),
                    )
                }
            }
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
                        item { Text("Propiedades", style = MaterialTheme.typography.titleLarge) }
                        item { ObjectProperties(obj, vm) }
                        item {
                            LumenPropertyRow(
                                "Portada",
                                if (coverUrl.isBlank()) "" else "Imagen vinculada",
                                icon = Icons.Default.Image,
                                onClick = {
                                    editPropertyKey = "Portada"
                                    editPropertyValue = coverUrl
                                },
                            )
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
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    LumenTypeChip("book", showSwap = true)
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
                                    verticalAlignment = Alignment.CenterVertically,
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
                            item {
                                LumenEditorField(
                                    value = readingNotes,
                                    onValueChange = {
                                        val changed = readingNotes.text != it.text
                                        readingNotes = it
                                        if (changed) vm.updateContent(obj.id, it.text)
                                    },
                                    source = obj,
                                    objects = vm.objects(),
                                    onOpenObject = { vm.openObject(it) },
                                    assetFor = vm::asset,
                                    onBlocksChange = { vm.updateBlocks(obj.id, it) },
                                    placeholder = "Resumen, notas de lectura, citas y reflexiones…",
                                    minHeight = 300.dp,
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

@Composable
private fun BookTopBarRow(
    tabs: List<String>,
    selectedTab: Int,
    onSelectTab: (Int) -> Unit,
    onBack: () -> Unit,
    pinned: Boolean,
    onTogglePin: () -> Unit,
    onCopyId: () -> Unit,
    onDelete: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
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
        LumenMoreButton(
            pinned = pinned,
            onTogglePin = onTogglePin,
            onCopyId = onCopyId,
            onDelete = onDelete,
        )
    }
}
