package com.sety.lumen.mobile.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Lightbulb
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.UnfoldMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
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
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.ui.theme.LumenColors

data class ObjectTypeItem(
    val key: String,
    val title: String,
    val count: Int,
)

@Composable
fun HomeScreen(
    vm: LumenViewModel,
    revision: Long,
    onOpenObject: (LumenObject) -> Unit,
    onOpenSettings: () -> Unit,
    onOpenCreate: () -> Unit,
) {
    val selectedTypeKey = vm.selectedTypeKey
    var workspaceMenuExpanded by remember { mutableStateOf(false) }

    val allObjects = remember(revision) { vm.objects() }
    val typeItems = remember(revision) {
        vm.types().map { type -> ObjectTypeItem(type.id, type.name, allObjects.count { it.typeId == type.id }) }
    }

    if (selectedTypeKey != null) {
        val currentType = typeItems.firstOrNull { it.key == selectedTypeKey }
        val objectsForType = allObjects.filter { it.typeId == selectedTypeKey }

        ObjectTypeCollectionScreen(
            vm = vm,
            typeItem = currentType ?: ObjectTypeItem("other", "Objetos", 0),
            objects = objectsForType,
            onBack = { vm.selectedTypeKey = null },
            onOpenObject = onOpenObject,
        )
        return
    }

    val pinned = remember(revision, allObjects) { allObjects.filter { it.isPinned }.take(6) }
    val carousel = if (pinned.isNotEmpty()) pinned else allObjects.take(4)
    val pending = vm.counts().second

    val homeBackground = if (isSystemInDarkTheme()) {
        LumenColors.BackgroundDeep
    } else {
        MaterialTheme.colorScheme.background
    }
    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(homeBackground),
    ) {
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .statusBarsPadding(),
            contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 8.dp, bottom = 100.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box {
                    Surface(
                        shape = CircleShape,
                        color = LumenColors.WorkspacePill,
                        modifier = Modifier.clickable { workspaceMenuExpanded = true },
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(
                                Icons.Default.Lightbulb,
                                contentDescription = null,
                                tint = Color.White,
                                modifier = Modifier.size(16.dp),
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                "Lumen",
                                style = MaterialTheme.typography.titleSmall,
                                fontWeight = FontWeight.SemiBold,
                                color = Color.White,
                            )
                            Spacer(Modifier.width(2.dp))
                            Icon(
                                Icons.Default.UnfoldMore,
                                contentDescription = null,
                                tint = Color.White.copy(alpha = 0.7f),
                                modifier = Modifier.size(16.dp),
                            )
                        }
                    }
                    DropdownMenu(
                        expanded = workspaceMenuExpanded,
                        onDismissRequest = { workspaceMenuExpanded = false },
                        containerColor = MaterialTheme.colorScheme.surface,
                    ) {
                        DropdownMenuItem(
                            text = { Text("Lumen") },
                            leadingIcon = { Icon(Icons.Default.Lightbulb, contentDescription = null) },
                            trailingIcon = { Icon(Icons.Default.Check, contentDescription = null) },
                            onClick = { workspaceMenuExpanded = false },
                        )
                        DropdownMenuItem(
                            text = { Text("Ajustes") },
                            leadingIcon = { Icon(Icons.Default.Settings, contentDescription = null) },
                            onClick = {
                                workspaceMenuExpanded = false
                                onOpenSettings()
                            },
                        )
                    }
                }

                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = { vm.syncNow() }) {
                        Icon(
                            Icons.Default.Refresh,
                            contentDescription = "Sincronizar",
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    IconButton(onClick = onOpenSettings) {
                        Icon(
                            Icons.Default.Settings,
                            contentDescription = "Ajustes",
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }

        item {
            LumenSyncLine(
                message = vm.syncMessage.ifBlank { "Lumen sincronizado" },
                pending = pending,
            )
        }

        if (allObjects.isEmpty()) {
            item {
                Surface(
                    shape = MaterialTheme.shapes.medium,
                    color = MaterialTheme.colorScheme.surface,
                    border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
                ) {
                    Column(Modifier.padding(16.dp)) {
                        Text(
                            "Tu espacio está listo",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface,
                        )
                        Spacer(Modifier.height(4.dp))
                        Text(
                            "Crea una página, escribe la nota de hoy o guarda un enlace.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Spacer(Modifier.height(12.dp))
                        androidx.compose.material3.FilledTonalButton(
                            onClick = onOpenCreate,
                            shape = MaterialTheme.shapes.small,
                            colors = androidx.compose.material3.ButtonDefaults.filledTonalButtonColors(
                                containerColor = MaterialTheme.colorScheme.surfaceVariant,
                                contentColor = MaterialTheme.colorScheme.onSurface,
                            ),
                        ) {
                            Icon(
                                Icons.Default.Add,
                                contentDescription = null,
                                modifier = Modifier.size(18.dp),
                            )
                            Spacer(Modifier.width(6.dp))
                            Text("Crear")
                        }
                    }
                }
            }
        }

        if (carousel.isNotEmpty()) {
            item {
                LumenSectionHeader(
                    title = if (pinned.isNotEmpty()) "Fijados" else "Recientes",
                    trailing = "${carousel.size}",
                    action = {
                        IconButton(onClick = onOpenCreate, modifier = Modifier.size(28.dp)) {
                            Icon(
                                Icons.Default.Add,
                                contentDescription = "Crear",
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.size(18.dp),
                            )
                        }
                    },
                )
            }
            item {
                LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    items(carousel, key = { it.id }) { obj ->
                        PinnedObjectCard(obj = obj, onClick = { onOpenObject(obj) })
                    }
                }
            }
        }

        item {
            Text(
                "Tipos de objeto",
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
            )
        }

            item {
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    typeItems.forEach { type ->
                        ObjectTypeRow(type = type, onClick = { vm.selectedTypeKey = type.key })
                    }
                    CreateObjectRow(onClick = onOpenCreate)
                }
            }
        }
    }
}

@Composable
private fun PinnedObjectCard(obj: LumenObject, onClick: () -> Unit) {
    val visual = LumenTypeVisuals.forType(obj.typeId)
    Surface(
        modifier = Modifier
            .width(156.dp)
            .height(86.dp)
            .clickable(onClick = onClick),
        shape = MaterialTheme.shapes.medium,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        Column(Modifier.padding(12.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                LumenTypeBadge(obj.typeId, size = 22.dp, iconSize = 12.dp)
                Spacer(Modifier.width(8.dp))
                Text(
                    visual.label,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
            Spacer(Modifier.weight(1f))
            Text(
                obj.title.ifBlank { "Sin título" },
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun ObjectTypeRow(type: ObjectTypeItem, onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 44.dp)
            .clickable(onClick = onClick)
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        LumenTypeBadge(type.key, size = 28.dp, iconSize = 15.dp)
        Spacer(Modifier.width(12.dp))
        Text(
            type.title,
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onSurface,
            modifier = Modifier.weight(1f),
        )
        Text(
            "${type.count}",
            style = MaterialTheme.typography.labelMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.width(4.dp))
        Icon(
            Icons.AutoMirrored.Filled.KeyboardArrowRight,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f),
            modifier = Modifier.size(16.dp),
        )
    }
}

@Composable
private fun CreateObjectRow(onClick: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 44.dp)
            .clickable(onClick = onClick)
            .padding(vertical = 3.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Surface(
            modifier = Modifier.size(28.dp),
            shape = MaterialTheme.shapes.extraSmall,
            color = MaterialTheme.colorScheme.surfaceVariant,
        ) {
            Box(contentAlignment = Alignment.Center) {
                Icon(
                    Icons.Default.Add,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.size(16.dp),
                )
            }
        }
        Spacer(Modifier.width(12.dp))
        Text(
            "Crear objeto",
            style = MaterialTheme.typography.bodyLarge,
            fontWeight = FontWeight.Medium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f),
        )
        Icon(
            Icons.AutoMirrored.Filled.KeyboardArrowRight,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f),
            modifier = Modifier.size(16.dp),
        )
    }
}

@Composable
fun ObjectTypeCollectionScreen(
    vm: LumenViewModel,
    typeItem: ObjectTypeItem,
    objects: List<LumenObject>,
    onBack: () -> Unit,
    onOpenObject: (LumenObject) -> Unit,
    initialView: LumenObject? = null,
) {
    val filterFields = vm.types().firstOrNull { it.id == typeItem.key }?.let { com.sety.lumen.mobile.data.objectFields(it) }.orEmpty()
        .filter { it.key != "status" && (it.options.isNotEmpty() || it.kind in setOf("object", "relation")) }
    val initialSpec = initialView?.properties?.optJSONObject("view") ?: org.json.JSONObject().put("type_id", typeItem.key)
    var tabIndex by rememberSaveable { mutableIntStateOf(if (initialSpec.optString("presentation") == "gallery") 1 else 0) }
    val tabs = listOf("Todo", "Vista general")
    val initialDraft = editableViewDraft(initialSpec, filterFields.map { it.key }.toSet(), typeItem.key == "web")
    var baseSpec by rememberSaveable(typeItem.key, initialView?.id) { mutableStateOf(initialDraft.base.toString()) }
    var activeViewId by rememberSaveable(typeItem.key, initialView?.id) { mutableStateOf(initialView?.id.orEmpty()) }
    var query by rememberSaveable(typeItem.key) { mutableStateOf(initialSpec.optString("search")) }
    var reviewFilter by rememberSaveable(typeItem.key) { mutableStateOf(initialDraft.review) }
    var create by remember { mutableStateOf(false) }
    val statuses = if (typeItem.key == "web") (vm.types().firstOrNull { it.id == "web" }?.let { com.sety.lumen.mobile.data.objectFields(it) }?.firstOrNull { it.key == "status" }?.options.orEmpty() +
        objects.map { com.sety.lumen.mobile.data.canonicalReviewStatus(it.status) }).distinctBy { com.sety.lumen.mobile.data.canonicalReviewStatus(it) }
        else objects.map { it.status }.filter { it.isNotBlank() }.distinct()
    var propertyKey by rememberSaveable(typeItem.key) { mutableStateOf(initialDraft.propertyKey) }
    var propertyValue by rememberSaveable(typeItem.key) { mutableStateOf(initialDraft.propertyValue) }
    var chooseProperty by remember { mutableStateOf(false) }
    var propertyEditor by remember { mutableStateOf<com.sety.lumen.mobile.data.ObjectField?>(null) }
    if (chooseProperty) androidx.compose.material3.AlertDialog(onDismissRequest = { chooseProperty = false }, title = { Text("Filtrar por propiedad") },
        text = { Column { filterFields.forEach { field -> androidx.compose.material3.TextButton(onClick = { chooseProperty = false; propertyEditor = field }) { Text(field.name) } } } },
        confirmButton = { androidx.compose.material3.TextButton(onClick = { chooseProperty = false; propertyKey = ""; propertyValue = "" }) { Text("Quitar filtro") } })
    propertyEditor?.let { field -> EditObjectFieldDialog(field.copy(multiple = false), if (propertyKey == field.key) propertyValue else "", vm.objects(),
        onDismiss = { propertyEditor = null }, onSave = { propertyKey = field.key; propertyValue = it.toString(); propertyEditor = null }, confirmLabel = "Aplicar filtro") }
    val spec = org.json.JSONObject(baseSpec).put("search", query)
    val extraRules = org.json.JSONArray()
    if (propertyKey.isNotBlank() && propertyValue.isNotBlank()) extraRules.put(org.json.JSONObject().put("field", "properties.$propertyKey").put("op", "equals").put("value", propertyValue))
    if (reviewFilter != "Todos") {
        val values = (objects.map { it.status } + reviewFilter).filter { status ->
            if (typeItem.key == "web") com.sety.lumen.mobile.data.canonicalReviewStatus(status) == com.sety.lumen.mobile.data.canonicalReviewStatus(reviewFilter) else status == reviewFilter
        }.distinct()
        extraRules.put(org.json.JSONObject().put("field", "properties.status").put("op", "in").put("value", org.json.JSONArray(values)))
    }
    if (extraRules.length() > 0) {
        spec.optJSONObject("filter")?.let(extraRules::put)
        spec.put("filter", org.json.JSONObject().put("mode", "and").put("rules", extraRules))
    }
    val visible = com.sety.lumen.mobile.data.LumenQuery.evaluate(vm.objects(), spec)
    fun loadView(view: LumenObject?) {
        val next = view?.properties?.optJSONObject("view") ?: org.json.JSONObject().put("type_id", typeItem.key)
        val draft = editableViewDraft(next, filterFields.map { it.key }.toSet(), typeItem.key == "web")
        baseSpec = draft.base.toString(); activeViewId = view?.id.orEmpty(); query = next.optString("search")
        reviewFilter = draft.review; propertyKey = draft.propertyKey; propertyValue = draft.propertyValue
        tabIndex = if (next.optString("presentation") == "gallery") 1 else 0
    }
    if (create) {
        if (typeItem.key == "web") AddLinkDialog(onDismiss = { create = false }, onSave = { url, title ->
            vm.addLink(url, title) { result -> result.onSuccess { create = false; onOpenObject(it) } }
        }) else CreateObjectDialog(vm, typeItem.key, onDismiss = { create = false }, onCreated = { create = false; onOpenObject(it) })
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
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
                selectedIndex = tabIndex,
                onSelect = { tabIndex = it; baseSpec = org.json.JSONObject(baseSpec).put("presentation", if (it == 1) "gallery" else "list").toString() },
                modifier = Modifier
                    .weight(1f)
                    .padding(horizontal = 8.dp),
            )
            LumenTypeBadge(typeItem.key, size = 36.dp, iconSize = 18.dp)
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 10.dp, bottom = 100.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            typeItem.title,
                            style = MaterialTheme.typography.headlineSmall,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onSurface,
                        )
                        Text(
                            "${visible.size} de ${objects.size} objeto(s)",
                            style = MaterialTheme.typography.labelMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }

            item {
                SavedViewControls(vm, typeItem.key, activeViewId, spec,
                    onSelect = { loadView(it) }, onSaved = { loadView(it) },
                    onClear = {
                        baseSpec = org.json.JSONObject(baseSpec).apply { remove("filter"); remove("search") }.toString()
                        query = ""; reviewFilter = "Todos"; propertyKey = ""; propertyValue = ""
                    }, onDeleted = { if (initialView?.id == activeViewId) onBack() else loadView(null) },
                    onSort = { field, direction -> baseSpec = org.json.JSONObject(baseSpec).put("sort", org.json.JSONArray().put(org.json.JSONObject().put("field", field).put("direction", direction))).toString() })
                androidx.compose.material3.OutlinedTextField(query, { query = it }, placeholder = { Text("Buscar objetos") }, singleLine = true, modifier = Modifier.fillMaxWidth())
                if (filterFields.isNotEmpty()) androidx.compose.material3.TextButton(onClick = { chooseProperty = true }) {
                    Text(if (propertyKey.isBlank()) "Filtrar propiedades" else "${filterFields.firstOrNull { it.key == propertyKey }?.name}: ${vm.getObject(propertyValue)?.title ?: propertyValue}")
                }
                if (statuses.isNotEmpty()) LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    items(listOf("Todos") + statuses) { status ->
                        androidx.compose.material3.FilterChip(selected = (if (typeItem.key == "web" && status != "Todos") com.sety.lumen.mobile.data.canonicalReviewStatus(reviewFilter) == com.sety.lumen.mobile.data.canonicalReviewStatus(status) else reviewFilter == status), onClick = { reviewFilter = status }, label = { Text(status) })
                    }
                }
                if (typeItem.key !in setOf("journal", "query", "collection", "image", "pdf", "file", "audio", "video")) androidx.compose.material3.TextButton(onClick = { create = true }) { Text("Nuevo objeto") }
            }
            if (spec.optJSONObject("filter")?.optJSONArray("rules")?.length()?.let { it > 0 } == true) item {
                Text(viewFilterDescription(spec.getJSONObject("filter"), vm), style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            if (visible.isEmpty()) {
                item {
                    LumenEmptyState(
                        title = if (objects.isEmpty()) "No hay ${typeItem.title.lowercase()}" else "Sin coincidencias",
                        body = if (objects.isEmpty()) "Crea un objeto para empezar." else "Cambia la búsqueda o el estado seleccionado.",
                        icon = LumenTypeVisuals.forType(typeItem.key).icon,
                    )
                }
            } else {
                if (tabIndex == 1) item {
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        items(visible.take(8), key = { it.id }) { obj -> PinnedObjectCard(obj = obj, onClick = { onOpenObject(obj) }) }
                    }
                }
                val groupField = spec.optString("group_by")
                val groups = if (groupField.isBlank()) mapOf("" to visible) else visible.groupBy {
                    val raw = com.sety.lumen.mobile.data.LumenQuery.field(it, groupField, vm.objects())
                    com.sety.lumen.mobile.data.relationIds(raw).mapNotNull(vm::getObject).takeIf { it.isNotEmpty() }?.joinToString(", ") { related -> related.title }
                        ?: com.sety.lumen.mobile.data.LumenQuery.display(raw).ifBlank { "Sin valor" }
                }
                groups.forEach { (label, members) ->
                    if (label.isNotEmpty()) item(key = "group-$label") { LumenSectionHeader(label, trailing = "${members.size}") }
                    items(members, key = { it.id }) { obj ->
                        LumenObjectRow(obj = obj, subtitle = objectSubtitle(obj), onClick = { onOpenObject(obj) })
                    }
                }
            }
        }
    }
}

private fun objectSubtitle(obj: LumenObject): String = when {
    obj.isBook && obj.author.isNotBlank() -> "Por ${obj.author}"
    obj.isWeb && obj.sourceUrl.isNotBlank() -> obj.sourceUrl
    obj.updatedAt.isNotBlank() -> obj.updatedAt.take(10)
    else -> "Hoy"
}
