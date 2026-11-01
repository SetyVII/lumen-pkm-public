package com.sety.lumen.mobile.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.EditNote
import androidx.compose.material.icons.filled.PhotoCamera
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material3.Card
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.humanDate
import com.sety.lumen.mobile.data.todayIso
import java.time.LocalDate

@Composable
fun TodayScreen(
    vm: LumenViewModel,
    revision: Long,
    onCapture: () -> Unit,
    onQuickNote: () -> Unit,
    onOpenJournal: () -> Unit,
    onOpenLink: (String) -> Unit,
    onOpenCalendar: () -> Unit,
) {
    val today = todayIso()
    val journal = remember(revision, today) { vm.journal(today) }
    val photos = remember(revision, today) { vm.photos(today) }
    val tasks = remember(revision) { vm.tasks() }
    val links = remember(revision) { vm.webLinks("Para revisar").take(3) }
    val pending = remember(revision) { vm.counts().second }
    var selectedTask by remember { mutableStateOf<LumenObject?>(null) }
    var selectedPhoto by remember { mutableStateOf<LumenObject?>(null) }

    LazyColumn(
        modifier = Modifier.fillMaxWidth().statusBarsPadding(),
        contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 14.dp, bottom = 28.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        item {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Hoy", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.SemiBold)
                    Text(
                        humanDate(LocalDate.now()).replaceFirstChar { it.uppercase() },
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                IconButton(onClick = onOpenCalendar) {
                    Icon(Icons.Default.CalendarMonth, contentDescription = "Calendario")
                }
                IconButton(onClick = { vm.syncNow() }) {
                    Icon(Icons.Default.Refresh, contentDescription = "Sincronizar")
                }
            }
        }
        item {
            SyncBadge(vm.syncMessage.ifBlank { "Listo para sincronizar" }, pending)
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                QuickAction("Foto del día", Icons.Default.PhotoCamera, Modifier.weight(1f), onCapture)
                QuickAction("Nota / Tarea", Icons.Default.EditNote, Modifier.weight(1f), onQuickNote)
                QuickAction("Sincronizar", Icons.Default.Sync, Modifier.weight(1f)) { vm.syncNow() }
            }
        }
        item {
            SectionTitle("Tareas pendientes", if (tasks.isEmpty()) null else "${tasks.size}")
        }
        if (tasks.isEmpty()) {
            item { EmptyState("Nada pendiente", "Crea una tarea desde la nota rápida o desde el widget.") }
        } else {
            items(tasks.take(8), key = { it.id }) { task ->
                TaskRow(
                    task = task,
                    onToggle = { vm.toggleTask(task.id, it) },
                    onOpen = { selectedTask = task },
                )
            }
        }
        item {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                Text(
                    "Diario visual" + if (photos.isEmpty()) "" else " · ${photos.size} foto(s)",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                )
                Text("Abrir", Modifier.clickable(onClick = onOpenJournal), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
            }
        }
        if (photos.isNotEmpty()) {
            item {
                LazyRow(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    items(photos, key = { it.id }) { photo ->
                        PhotoCard(
                            asset = vm.asset(photo.assetId),
                            caption = photo.content.ifBlank { photo.title },
                            onClick = { selectedPhoto = photo },
                            modifier = Modifier.width(250.dp),
                        )
                    }
                }
            }
        } else {
            item {
                Card(Modifier.fillMaxWidth().clickable(onClick = onCapture)) {
                    Column(Modifier.padding(20.dp)) {
                        Text("La foto del día empieza aquí", style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                        Text(
                            "Toca para capturar un momento. Se guarda en el diario de hoy, sin GPS.",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 4.dp),
                        )
                    }
                }
            }
        }
        val journalText = journal?.content.orEmpty()
        if (journalText.isNotBlank()) {
            item {
                Card(Modifier.fillMaxWidth().clickable(onClick = onOpenJournal)) {
                    Text(
                        journalText,
                        modifier = Modifier.padding(16.dp),
                        style = MaterialTheme.typography.bodyMedium,
                        maxLines = 6,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        item {
            SectionTitle("Enlaces por revisar", if (links.isEmpty()) null else "${links.size}")
        }
        if (links.isEmpty()) {
            item { EmptyState("Bandeja vacía", "Comparte un enlace desde cualquier app y aparecerá aquí con su portada.") }
        } else {
            items(links, key = { it.id }) { link ->
                ObjectCard(
                    obj = link,
                    asset = vm.asset(link.assetId),
                    onClick = { onOpenLink(link.stringProperty("url")) },
                )
            }
        }
    }

    selectedTask?.let { task ->
        TaskDetailDialog(
            task = task,
            onDismiss = { selectedTask = null },
            onToggle = { vm.toggleTask(task.id, it); selectedTask = null },
            onPostpone = { vm.postponeTask(task.id); selectedTask = null },
            onDelete = { vm.deleteObject(task.id); selectedTask = null },
        )
    }
    selectedPhoto?.let { photo ->
        ImageDialog(vm.asset(photo.assetId), photo.content.ifBlank { photo.title }, onDismiss = { selectedPhoto = null })
    }
}

@Composable
private fun QuickAction(label: String, icon: androidx.compose.ui.graphics.vector.ImageVector, modifier: Modifier = Modifier, onClick: () -> Unit) {
    Card(modifier.clickable(onClick = onClick)) {
        Column(
            Modifier.fillMaxWidth().padding(vertical = 16.dp, horizontal = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
            Spacer(Modifier.height(6.dp))
            Text(label, style = MaterialTheme.typography.labelMedium, maxLines = 1)
        }
    }
}
