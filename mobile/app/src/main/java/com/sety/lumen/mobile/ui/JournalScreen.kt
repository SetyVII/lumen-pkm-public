package com.sety.lumen.mobile.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.EditNote
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.PhotoCamera
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CheckboxDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.todayIso
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.temporal.WeekFields
import java.util.Locale

@Composable
fun JournalScreen(
    vm: LumenViewModel,
    revision: Long,
    date: String,
    onDateChange: (String) -> Unit,
    onCapture: () -> Unit,
    onQuickNote: () -> Unit,
    onOpenCalendar: () -> Unit,
    onOpenObject: (LumenObject) -> Unit = {},
) {
    val parsed = runCatching { LocalDate.parse(date) }.getOrDefault(LocalDate.now())
    val isToday = date == todayIso()
    val journal = remember(revision, date) { vm.journal(date) }
    val photos = remember(revision, date) { vm.photos(date).filter { it.id !in com.sety.lumen.mobile.data.embeddedImageIds(journal) } }
    val tasks = remember(revision, date) { vm.tasks().filter { it.dueDate == date } }
    val dayObjects = remember(revision, date) {
        vm.objects().filter {
            it.createdAt.take(10) == date || it.updatedAt.take(10) == date
        }
    }

    var selectedTopTab by rememberSaveable { mutableIntStateOf(0) }
    val topTabs = listOf("Nota diaria", "Cronología", "Creado")

    var isReviewed by rememberSaveable(date) { mutableStateOf(false) }
    var editorValue by rememberSaveable(date, stateSaver = TextFieldValue.Saver) {
        val initial = com.sety.lumen.mobile.data.editorDocumentText(journal)
        mutableStateOf(TextFieldValue(initial, TextRange(initial.length)))
    }

    androidx.compose.runtime.LaunchedEffect(date, journal) {
        val remoteText = com.sety.lumen.mobile.data.editorDocumentText(journal)
        if (editorValue.text != remoteText) {
            editorValue = TextFieldValue(remoteText, TextRange(editorValue.selection.end.coerceAtMost(remoteText.length)))
        }
    }

    val weekOfYear = parsed.get(WeekFields.of(Locale.getDefault()).weekOfYear())
    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        Column(
            modifier = Modifier
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
                    icon = Icons.Default.CalendarMonth,
                    contentDescription = "Calendario",
                    onClick = onOpenCalendar,
                )
                SegmentedPill(
                    tabs = topTabs,
                    selectedIndex = selectedTopTab,
                    onSelect = { selectedTopTab = it },
                    modifier = Modifier
                        .weight(1f)
                        .padding(horizontal = 8.dp),
                )
                TodayCircleButton(onClick = { onDateChange(todayIso()) })
            }
        }

        WeekStrip(
            parsed = parsed,
            onDateChange = onDateChange,
            onOpenCalendar = onOpenCalendar,
        )

        if (selectedTopTab == 0) {
            LazyColumn(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 10.dp, bottom = 100.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                item {
                    JournalHeader(
                        parsed = parsed,
                        isToday = isToday,
                        weekOfYear = weekOfYear,
                        isReviewed = isReviewed,
                        onReviewedChange = { isReviewed = it },
                    )
                }

                item {
                    LumenEditorField(
                        value = editorValue,
                        onValueChange = {
                            val changed = editorValue.text != it.text
                            editorValue = it
                            if (changed) vm.updateContent(vm.ensureJournal(date).id, it.text)
                        },
                        source = journal,
                        objects = vm.objects(),
                        onOpenObject = onOpenObject,
                        assetFor = vm::asset,
                                    onBlocksChange = { vm.updateBlocks(vm.ensureJournal(date).id, it) },
                        placeholder = "Escribe tus pensamientos del día… Usa @ para vincular un libro o un enlace.",
                        minHeight = 220.dp,
                    )
                }

                item {
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        FilledTonalButton(
                            onClick = onCapture,
                            modifier = Modifier
                                .weight(1f)
                                .height(40.dp),
                            shape = MaterialTheme.shapes.small,
                            contentPadding = PaddingValues(horizontal = 12.dp),
                            colors = ButtonDefaults.filledTonalButtonColors(
                                containerColor = MaterialTheme.colorScheme.surfaceVariant,
                                contentColor = MaterialTheme.colorScheme.onSurface,
                            ),
                        ) {
                            Icon(Icons.Default.PhotoCamera, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(Modifier.width(6.dp))
                            Text("Foto", style = MaterialTheme.typography.labelLarge)
                        }
                        FilledTonalButton(
                            onClick = onQuickNote,
                            modifier = Modifier
                                .weight(1f)
                                .height(40.dp),
                            shape = MaterialTheme.shapes.small,
                            contentPadding = PaddingValues(horizontal = 12.dp),
                            colors = ButtonDefaults.filledTonalButtonColors(
                                containerColor = MaterialTheme.colorScheme.surfaceVariant,
                                contentColor = MaterialTheme.colorScheme.onSurface,
                            ),
                        ) {
                            Icon(Icons.Default.EditNote, contentDescription = null, modifier = Modifier.size(18.dp))
                            Spacer(Modifier.width(6.dp))
                            Text("Nota / Tarea", style = MaterialTheme.typography.labelLarge)
                        }
                    }
                }

                if (photos.isNotEmpty()) {
                    item {
                        LumenSectionHeader("Momentos", trailing = "${photos.size}")
                    }
                    items(photos, key = { it.id }) { photo ->
                        PhotoCard(
                            asset = vm.asset(photo.assetId),
                            caption = photo.content.ifBlank { photo.title },
                            onClick = { onOpenObject(photo) },
                        )
                    }
                }

                if (tasks.isNotEmpty()) {
                    item {
                        LumenSectionHeader("Tareas del día", trailing = "${tasks.size}")
                    }
                    items(tasks, key = { it.id }) { task ->
                        TaskRow(
                            task = task,
                            onToggle = { vm.toggleTask(task.id, it) },
                            onOpen = { onOpenObject(task) },
                        )
                    }
                }
            }
        } else {
            val timeline = remember(selectedTopTab, dayObjects) {
                if (selectedTopTab == 1) {
                    dayObjects.sortedByDescending { it.updatedAt }
                } else {
                    dayObjects.filter { it.createdAt.take(10) == date }
                        .sortedByDescending { it.createdAt }
                }
            }
            LazyColumn(
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth(),
                contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 10.dp, bottom = 100.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                item {
                    JournalHeader(
                        parsed = parsed,
                        isToday = isToday,
                        weekOfYear = weekOfYear,
                        isReviewed = isReviewed,
                        onReviewedChange = { isReviewed = it },
                    )
                }
                item {
                    LumenSectionHeader(
                        title = if (selectedTopTab == 1) "Cronología" else "Creado",
                        trailing = "${timeline.size}",
                    )
                }
                if (timeline.isEmpty()) {
                    item {
                        LumenEmptyState(
                            title = if (selectedTopTab == 1) "Sin actividad" else "Nada creado",
                            body = if (selectedTopTab == 1) {
                                "Los objetos que edites hoy aparecerán aquí."
                            } else {
                                "Los objetos creados hoy aparecerán aquí."
                            },
                            icon = Icons.Default.History,
                        )
                    }
                } else {
                    items(timeline, key = { it.id }) { obj ->
                        TimelineRow(
                            obj = obj,
                            time = if (selectedTopTab == 1) {
                                obj.updatedAt.ifBlank { obj.createdAt }
                            } else {
                                obj.createdAt
                            },
                            onClick = { onOpenObject(obj) },
                        )
                    }
                }
            }
        }

        if (selectedTopTab == 0) {
            EditorKeyboardBar(
                vm = vm,
                onInsertText = { snippet ->
                    editorValue = insertSnippet(editorValue, snippet)
                    val j = vm.ensureJournal(date)
                    vm.updateContent(j.id, editorValue.text)
                },
                onIndent = {
                    editorValue = indentLine(editorValue)
                    val j = vm.ensureJournal(date)
                    vm.updateContent(j.id, editorValue.text)
                },
                onOutdent = {
                    editorValue = outdentLine(editorValue)
                    val j = vm.ensureJournal(date)
                    vm.updateContent(j.id, editorValue.text)
                },
            )
        }
    }
}

@Composable
private fun JournalHeader(
    parsed: LocalDate,
    isToday: Boolean,
    weekOfYear: Int,
    isReviewed: Boolean,
    onReviewedChange: (Boolean) -> Unit,
) {
    Column(Modifier.fillMaxWidth()) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            val dayOfWeekName = parsed
                .format(DateTimeFormatter.ofPattern("EEEE", Locale.forLanguageTag("es")))
                .replaceFirstChar { it.uppercase() }
            Text(
                dayOfWeekName,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                color = Color(0xFF50A2FF),
            )
            if (isToday) {
                Spacer(Modifier.width(8.dp))
                Surface(
                    shape = RoundedCornerShape(6.dp),
                    color = MaterialTheme.colorScheme.primaryContainer,
                ) {
                    Text(
                        "Hoy",
                        modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.primary,
                        fontWeight = FontWeight.Bold,
                    )
                }
            }
        }

        Spacer(Modifier.height(4.dp))
        Text(
            journalTitle(parsed),
            style = MaterialTheme.typography.headlineMedium,
            fontWeight = FontWeight.Bold,
            color = MaterialTheme.colorScheme.onSurface,
        )

        Spacer(Modifier.height(2.dp))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                "Semana $weekOfYear",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row(
                verticalAlignment = Alignment.CenterVertically,
                modifier = Modifier.clickable { onReviewedChange(!isReviewed) },
            ) {
                Text(
                    "Revisado",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                Checkbox(
                    checked = isReviewed,
                    onCheckedChange = onReviewedChange,
                    modifier = Modifier.size(20.dp),
                    colors = CheckboxDefaults.colors(
                        checkedColor = MaterialTheme.colorScheme.primary,
                        uncheckedColor = MaterialTheme.colorScheme.onSurfaceVariant,
                        checkmarkColor = MaterialTheme.colorScheme.onPrimary,
                    ),
                )
            }
        }
    }
}

@Composable
private fun TimelineRow(obj: LumenObject, time: String, onClick: () -> Unit) {
    Column(Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(
                modifier = Modifier.width(48.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
            ) {
                Text(
                    timeLabel(time),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(4.dp))
                Box(
                    Modifier
                        .size(6.dp)
                        .clip(CircleShape)
                        .background(MaterialTheme.colorScheme.primary),
                )
            }
            Spacer(Modifier.width(10.dp))
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
                Text(
                    LumenTypeVisuals.forType(obj.typeId).label,
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Icon(
                Icons.AutoMirrored.Filled.KeyboardArrowRight,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f),
                modifier = Modifier.size(18.dp),
            )
        }
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
    }
}

private fun timeLabel(iso: String): String = runCatching {
    Instant.parse(iso)
        .atZone(ZoneId.systemDefault())
        .format(DateTimeFormatter.ofPattern("HH:mm"))
}.getOrDefault("")

private fun journalTitle(date: LocalDate): String =
    date.format(DateTimeFormatter.ofPattern("d 'de' MMMM 'de' yyyy", Locale.forLanguageTag("es")))
        .split(" ")
        .joinToString(" ") { word -> word.replaceFirstChar { it.uppercase() } }

@Composable
private fun TodayCircleButton(onClick: () -> Unit) {
    Surface(
        modifier = Modifier
            .size(36.dp)
            .clip(CircleShape)
            .clickable(onClick = onClick),
        shape = CircleShape,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(
                "${LocalDate.now().dayOfMonth}",
                style = MaterialTheme.typography.labelMedium,
                fontWeight = FontWeight.Bold,
                color = MaterialTheme.colorScheme.primary,
            )
        }
    }
}

@Composable
private fun WeekStrip(
    parsed: LocalDate,
    onDateChange: (String) -> Unit,
    onOpenCalendar: () -> Unit,
) {
    val startOfWeek = parsed.minusDays((parsed.dayOfWeek.value - 1).toLong())
    val daysInStrip = (0..6).map { startOfWeek.plusDays(it.toLong()) }
    val dayNames = listOf("Lu", "Ma", "Mi", "Ju", "Vi", "Sá", "Do")

    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
        ) {
            daysInStrip.forEachIndexed { index, day ->
                val isDaySelected = day == parsed
                val isDayToday = day == LocalDate.now()
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    modifier = Modifier
                        .weight(1f)
                        .padding(horizontal = 1.dp)
                        .clip(RoundedCornerShape(10.dp))
                        .background(
                            if (isDaySelected) MaterialTheme.colorScheme.surface else Color.Transparent
                        )
                        .clickable { onDateChange(day.toString()) }
                        .padding(vertical = 6.dp),
                ) {
                    Text(
                        dayNames[index],
                        style = MaterialTheme.typography.labelSmall,
                        color = if (isDaySelected || isDayToday) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                    )
                    Spacer(Modifier.height(3.dp))
                    Text(
                        "${day.dayOfMonth}",
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = if (isDaySelected || isDayToday) FontWeight.Bold else FontWeight.Normal,
                        color = if (isDaySelected || isDayToday) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurface
                        },
                    )
                }
            }
        }

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onOpenCalendar),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                Icons.Default.KeyboardArrowDown,
                contentDescription = "Expandir a mes",
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f),
                modifier = Modifier.size(18.dp),
            )
        }
        HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    }
}
