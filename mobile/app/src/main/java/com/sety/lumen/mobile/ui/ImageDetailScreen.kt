package com.sety.lumen.mobile.ui

import android.content.ClipData
import android.content.ClipDescription
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CalendarToday
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.Image
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Straighten
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import coil.compose.AsyncImage
import coil.request.ImageRequest
import coil.size.Precision
import coil.size.Size
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.SecureMedia
import com.sety.lumen.mobile.ui.theme.LumenColors
import java.io.File

@Composable
fun ImageDetailScreen(
    obj: LumenObject,
    vm: LumenViewModel,
    onBack: () -> Unit,
) {
    val revision by vm.revision.collectAsStateWithLifecycle()
    val context = LocalContext.current
    var selectedTab by rememberSaveable { mutableIntStateOf(0) }
    val tabs = listOf("Imagen", "Propiedades", "Contenido", "Gráfico")

    val asset = remember(obj.assetId, revision) { if (obj.assetId.isNotBlank()) vm.asset(obj.assetId) else null }
    val mediaPath = asset?.localPath?.takeIf { it.isNotBlank() && File(it).exists() }
        ?: asset?.thumbPath?.takeIf { it.isNotBlank() && File(it).exists() }
    val imageModel = rememberMediaModel(mediaPath)

    var notesText by remember(obj.id, obj.content) { mutableStateOf(com.sety.lumen.mobile.data.editorDocumentText(obj)) }
    var title by remember(obj.id, obj.title) { mutableStateOf(obj.title) }
    var showDelete by remember { mutableStateOf(false) }

    var scale by remember { mutableFloatStateOf(1f) }
    var offset by remember { mutableStateOf(Offset.Zero) }

    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    fun copyImage() {
        val plain = mediaPath?.let { SecureMedia.tempPlain(context, vm.vault, it) }
        if (plain != null) {
            try {
                val uri = FileProvider.getUriForFile(
                    context,
                    "${context.packageName}.fileprovider",
                    plain,
                )
                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                val resolvedMime = asset?.mime?.takeIf { it.isNotBlank() }
                    ?: context.contentResolver.getType(uri)
                    ?: "image/png"
                val clipData = ClipData(
                    ClipDescription(title.ifBlank { "Imagen" }, arrayOf(resolvedMime, "image/*")),
                    ClipData.Item(uri),
                )
                clipboard.setPrimaryClip(clipData)
                Toast.makeText(context, "Imagen copiada al portapapeles", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                Toast.makeText(context, "Error al copiar la imagen: ${e.message}", Toast.LENGTH_SHORT).show()
            }
        } else {
            Toast.makeText(context, "Imagen no disponible localmente", Toast.LENGTH_SHORT).show()
        }
    }

    fun shareImage() {
        val plain = mediaPath?.let { SecureMedia.tempPlain(context, vm.vault, it) }
        if (plain != null) {
            val uri = FileProvider.getUriForFile(
                context,
                "${context.packageName}.fileprovider",
                plain,
            )
            val shareIntent = Intent(Intent.ACTION_SEND).apply {
                type = asset?.mime?.ifBlank { "image/webp" } ?: "image/webp"
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
            context.startActivity(Intent.createChooser(shareIntent, "Compartir imagen"))
        }
    }

    if (showDelete) {
        ConfirmDialog(
            title = "¿Eliminar esta imagen?",
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
                        onCopyImage = { copyImage() },
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
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .background(LumenColors.BackgroundDeep),
                        contentAlignment = Alignment.Center,
                    ) {
                        if (imageModel != null) {
                            AsyncImage(
                                model = ImageRequest.Builder(context)
                                    .data(imageModel)
                                    .size(4096, 4096)
                                    .precision(Precision.INEXACT)
                                    .crossfade(true)
                                    .build(),
                                contentDescription = title,
                                modifier = Modifier
                                    .fillMaxSize()
                                    .pointerInput(Unit) {
                                        detectTapGestures(
                                            onDoubleTap = {
                                                if (scale > 1.2f) {
                                                    scale = 1f
                                                    offset = Offset.Zero
                                                } else {
                                                    scale = 2.5f
                                                }
                                            }
                                        )
                                    }
                                    .pointerInput(Unit) {
                                        detectTransformGestures { _, pan, zoom, _ ->
                                            scale = (scale * zoom).coerceIn(0.7f, 5.0f)
                                            val maxPan = 500f * (scale - 1f).coerceAtLeast(0f)
                                            offset = Offset(
                                                x = (offset.x + pan.x).coerceIn(-maxPan, maxPan),
                                                y = (offset.y + pan.y).coerceIn(-maxPan, maxPan),
                                            )
                                        }
                                    }
                                    .graphicsLayer {
                                        scaleX = scale
                                        scaleY = scale
                                        translationX = offset.x
                                        translationY = offset.y
                                    },
                                contentScale = ContentScale.Fit,
                            )
                        } else {
                            Text(
                                "Imagen no encontrada localmente",
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }

                        Surface(
                            modifier = Modifier
                                .align(Alignment.BottomCenter)
                                .padding(bottom = 32.dp),
                            shape = RoundedCornerShape(32.dp),
                            color = MaterialTheme.colorScheme.surface.copy(alpha = 0.94f),
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
                            shadowElevation = 8.dp,
                        ) {
                            Row(
                                modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.spacedBy(8.dp),
                            ) {
                                Text(
                                    "${(scale * 100).toInt()}%",
                                    style = MaterialTheme.typography.labelMedium,
                                    fontWeight = FontWeight.Bold,
                                    color = MaterialTheme.colorScheme.onSurface,
                                    modifier = Modifier.clickable {
                                        scale = 1f
                                        offset = Offset.Zero
                                    },
                                )
                                Spacer(Modifier.width(4.dp))
                                IconButton(
                                    onClick = { scale = (scale - 0.25f).coerceAtLeast(0.7f) },
                                    modifier = Modifier.size(28.dp),
                                ) {
                                    Icon(
                                        Icons.Default.Remove,
                                        contentDescription = "Alejar",
                                        tint = MaterialTheme.colorScheme.onSurface,
                                        modifier = Modifier.size(16.dp),
                                    )
                                }
                                IconButton(
                                    onClick = { scale = (scale + 0.25f).coerceAtMost(5f) },
                                    modifier = Modifier.size(28.dp),
                                ) {
                                    Icon(
                                        Icons.Default.Add,
                                        contentDescription = "Acercar",
                                        tint = MaterialTheme.colorScheme.onSurface,
                                        modifier = Modifier.size(16.dp),
                                    )
                                }
                                IconButton(
                                    onClick = { copyImage() },
                                    modifier = Modifier.size(28.dp),
                                ) {
                                    Icon(
                                        Icons.Default.ContentCopy,
                                        contentDescription = "Copiar imagen al portapapeles",
                                        tint = MaterialTheme.colorScheme.onSurface,
                                        modifier = Modifier.size(16.dp),
                                    )
                                }
                                IconButton(
                                    onClick = { shareImage() },
                                    modifier = Modifier.size(28.dp),
                                ) {
                                    Icon(
                                        Icons.Default.Share,
                                        contentDescription = "Compartir",
                                        tint = MaterialTheme.colorScheme.onSurface,
                                        modifier = Modifier.size(16.dp),
                                    )
                                }
                            }
                        }
                    }
                }
                1 -> {
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = PaddingValues(horizontal = 20.dp, vertical = 16.dp),
                    ) {
                        item { ObjectProperties(obj, vm, exclude = setOf("mime")) }
                        item {
                            Text(
                                "Propiedades",
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.SemiBold,
                                color = MaterialTheme.colorScheme.onSurface,
                            )
                        }
                        item {
                            LumenPropertyRow(
                                "Formato MIME",
                                asset?.mime ?: "image/webp",
                                icon = Icons.Default.Image,
                            )
                        }
                        item {
                            val sizeKb = (asset?.size ?: 0) / 1024
                            LumenPropertyRow(
                                "Tamaño",
                                if (sizeKb > 1024) "${sizeKb / 1024} MB" else "$sizeKb KB",
                                icon = Icons.Default.Storage,
                            )
                        }
                        item {
                            val w = obj.stringProperty("width")
                            val h = obj.stringProperty("height")
                            LumenPropertyRow(
                                "Dimensiones",
                                if (w.isNotBlank() && h.isNotBlank()) "${w}x$h px" else "Auto",
                                icon = Icons.Default.Straighten,
                            )
                        }
                        item {
                            LumenPropertyRow(
                                "Fecha de captura",
                                obj.createdAt.take(10).ifBlank { "Hoy" },
                                icon = Icons.Default.CalendarToday,
                            )
                        }
                        item {
                            LumenPropertyRow(
                                "Archivo local",
                                mediaPath?.let { File(it).name } ?: "No disponible",
                                icon = Icons.Default.Fingerprint,
                                showDivider = false,
                            )
                        }
                    }
                }
                2 -> {
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
                                    LumenTypeChip("image", showSwap = true)
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
                                    placeholder = "Título de la imagen",
                                )
                            }
                            item {
                                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                                    LumenTagChip("#imagen")
                                    LumenTagChip("#momento")
                                }
                            }
                            item {
                                LumenEditorField(
                                    value = notesText,
                                    onValueChange = {
                                        notesText = it
                                        vm.updateContent(obj.id, it)
                                    },
                                    source = obj,
                                    objects = vm.objects(),
                                    onOpenObject = { vm.openObject(it) },
                                    assetFor = vm::asset,
                                    onBlocksChange = { vm.updateBlocks(obj.id, it) },
                                    placeholder = "Escribe notas, transcripción OCR o contexto sobre esta imagen…",
                                    minHeight = 240.dp,
                                )
                            }
                        }

                        EditorKeyboardBar(
                            vm = vm,
                            onInsertText = { snippet ->
                                notesText = insertSnippet(notesText, snippet)
                                vm.updateContent(obj.id, notesText)
                            },
                            onIndent = {
                                notesText = indentLine(notesText)
                                vm.updateContent(obj.id, notesText)
                            },
                            onOutdent = {
                                notesText = outdentLine(notesText)
                                vm.updateContent(obj.id, notesText)
                            },
                        )
                    }
                }
                3 -> {
                    ObjectRelations(obj, vm)
                }
            }
        }
    }
}
