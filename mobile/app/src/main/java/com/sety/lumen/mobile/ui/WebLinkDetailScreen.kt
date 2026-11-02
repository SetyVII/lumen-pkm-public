package com.sety.lumen.mobile.ui

import android.annotation.SuppressLint
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.view.ViewGroup
import android.webkit.WebChromeClient
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.OpenInNew
import androidx.compose.material.icons.filled.CalendarToday
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.Language
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.RadioButtonChecked
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import coil.compose.AsyncImage
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.util.UrlCleaner
import java.io.File

@SuppressLint("SetJavaScriptEnabled")
@Composable
fun WebLinkDetailScreen(
    obj: LumenObject,
    vm: LumenViewModel,
    onBack: () -> Unit,
) {
    val revision by vm.revision.collectAsStateWithLifecycle()
    val context = LocalContext.current
    var selectedTab by rememberSaveable { mutableIntStateOf(1) }
    val tabs = listOf("Propiedades", "Contenido", "Incrustar", "Gráfico")
    val url = obj.sourceUrl
    val title = obj.title
    val description = obj.ogDescription
    val site = obj.siteName.ifBlank { UrlCleaner.hostOf(url) }
    val asset = remember(obj.assetId, revision) { if (obj.assetId.isNotBlank()) vm.asset(obj.assetId) else null }

    var notesText by remember(obj.id, obj.content) { mutableStateOf(com.sety.lumen.mobile.data.editorDocumentText(obj)) }
    var reviewStatus by remember(obj.id, obj.status) { mutableStateOf(obj.status.ifBlank { "Para revisar" }) }
    var showReviewDialog by remember { mutableStateOf(false) }
    if (showReviewDialog) EditObjectFieldDialog(
        vm.types().firstOrNull { it.id == obj.typeId }?.let { com.sety.lumen.mobile.data.objectFields(it) }?.firstOrNull { it.key == "status" }
            ?: com.sety.lumen.mobile.data.ObjectField("status", "Estado", "select", com.sety.lumen.mobile.data.reviewStatuses),
        com.sety.lumen.mobile.data.canonicalReviewStatus(reviewStatus), emptyList(),
        onDismiss = { showReviewDialog = false }, onSave = { reviewStatus = it.toString(); vm.setWebStatus(obj.id, reviewStatus); showReviewDialog = false })
    var showDelete by remember { mutableStateOf(false) }

    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    if (showDelete) {
        ConfirmDialog(
            title = "¿Eliminar este enlace?",
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
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        IconButton(
                            onClick = {
                                runCatching {
                                    context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)))
                                }
                            },
                        ) {
                            Icon(
                                Icons.AutoMirrored.Filled.OpenInNew,
                                contentDescription = "Abrir enlace",
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        LumenMoreButton(
                            pinned = obj.isPinned,
                            onTogglePin = { vm.updateProperty(obj.id, "pinned", !obj.isPinned) },
                            onCopyId = { copyId(context, obj.id) },
                            onDelete = { showDelete = true },
                        )
                    }
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
                        item { ObjectProperties(obj, vm, exclude = setOf("url", "status")) }
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
                                "URL original",
                                url,
                                icon = Icons.Default.Link,
                                onClick = {
                                    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                                    clipboard.setPrimaryClip(ClipData.newPlainText("URL", url))
                                    Toast.makeText(context, "URL copiada", Toast.LENGTH_SHORT).show()
                                },
                            )
                        }
                        item {
                            LumenPropertyRow("Dominio / Sitio", site, icon = Icons.Default.Language)
                        }
                        item {
                            LumenPropertyRow(
                                "Estado",
                                com.sety.lumen.mobile.data.canonicalReviewStatus(reviewStatus),
                                icon = Icons.Default.RadioButtonChecked,
                                onClick = {
                                    showReviewDialog = true
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
                                    verticalAlignment = Alignment.CenterVertically,
                                ) {
                                    LumenTypeChip("web", showSwap = true)
                                    LumenCollectionsChip()
                                }
                            }

                            item {
                                Card(
                                    modifier = Modifier.fillMaxWidth(),
                                    shape = MaterialTheme.shapes.medium,
                                    colors = CardDefaults.cardColors(
                                        containerColor = MaterialTheme.colorScheme.surface,
                                    ),
                                ) {
                                    Column {
                                        val coverModel = rememberMediaModel(asset?.localPath)
                                            ?: obj.coverUrl.ifBlank { null }
                                        if (coverModel != null) {
                                            Box(
                                                modifier = Modifier
                                                    .fillMaxWidth()
                                                    .aspectRatio(16f / 9f)
                                                    .background(MaterialTheme.colorScheme.background),
                                                contentAlignment = Alignment.Center,
                                            ) {
                                                AsyncImage(
                                                    model = coverModel,
                                                    contentDescription = title,
                                                    modifier = Modifier.fillMaxSize(),
                                                    contentScale = ContentScale.Fit,
                                                )
                                            }
                                        }

                                        Column(Modifier.padding(14.dp)) {
                                            Surface(
                                                shape = RoundedCornerShape(6.dp),
                                                color = MaterialTheme.colorScheme.surfaceVariant,
                                            ) {
                                                Row(
                                                    modifier = Modifier.padding(horizontal = 8.dp, vertical = 4.dp),
                                                    verticalAlignment = Alignment.CenterVertically,
                                                ) {
                                                    Icon(
                                                        if (url.contains("youtube.com") || url.contains("youtu.be")) {
                                                            Icons.Default.PlayArrow
                                                        } else {
                                                            Icons.Default.Language
                                                        },
                                                        contentDescription = null,
                                                        tint = MaterialTheme.colorScheme.tertiary,
                                                        modifier = Modifier.size(14.dp),
                                                    )
                                                    Spacer(Modifier.width(4.dp))
                                                    Text(
                                                        site,
                                                        style = MaterialTheme.typography.labelSmall,
                                                        fontWeight = FontWeight.SemiBold,
                                                        color = MaterialTheme.colorScheme.onSurface,
                                                        maxLines = 1,
                                                        overflow = TextOverflow.Ellipsis,
                                                    )
                                                    Spacer(Modifier.width(6.dp))
                                                    Icon(
                                                        Icons.Default.ContentCopy,
                                                        contentDescription = "Copiar URL",
                                                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                                        modifier = Modifier
                                                            .size(13.dp)
                                                            .clickable {
                                                                val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                                                                clipboard.setPrimaryClip(ClipData.newPlainText("URL", url))
                                                                Toast.makeText(context, "URL copiada", Toast.LENGTH_SHORT).show()
                                                            },
                                                    )
                                                }
                                            }

                                            Spacer(Modifier.size(8.dp))
                                            Text(
                                                title,
                                                style = MaterialTheme.typography.titleLarge,
                                                fontWeight = FontWeight.Bold,
                                                color = MaterialTheme.colorScheme.onSurface,
                                            )

                                            if (description.isNotBlank()) {
                                                Spacer(Modifier.size(6.dp))
                                                Text(
                                                    description,
                                                    style = MaterialTheme.typography.bodyMedium,
                                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                                )
                                            }
                                        }
                                    }
                                }
                            }

                            item {
                                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                                    LumenTagChip("#enlaces")
                                    if (site.isNotBlank()) LumenTagChip("#$site")
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
                                    placeholder = "Escribe notas, transcripciones o citas sobre este enlace…",
                                    minHeight = 240.dp,
                                )
                            }
                        }

                        // Canonical block controls are owned by CanonicalDocumentEditor.
                    }
                }
                2 -> {
                    if (url.isNotBlank()) {
                        AndroidView(
                            factory = { ctx ->
                                WebView(ctx).apply {
                                    layoutParams = ViewGroup.LayoutParams(
                                        ViewGroup.LayoutParams.MATCH_PARENT,
                                        ViewGroup.LayoutParams.MATCH_PARENT,
                                    )
                                    setBackgroundColor(android.graphics.Color.parseColor("#0D0D10"))
                                    settings.javaScriptEnabled = true
                                    settings.domStorageEnabled = true
                                    settings.loadWithOverviewMode = true
                                    settings.useWideViewPort = true
                                    webViewClient = WebViewClient()
                                    webChromeClient = WebChromeClient()
                                    loadUrl(url)
                                }
                            },
                            modifier = Modifier
                                .fillMaxSize()
                                .background(MaterialTheme.colorScheme.background),
                        )
                    } else {
                        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                            Text(
                                "URL no disponible para incrustar",
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
                3 -> {
                    ObjectRelations(obj, vm)
                }
            }
        }
    }
}
