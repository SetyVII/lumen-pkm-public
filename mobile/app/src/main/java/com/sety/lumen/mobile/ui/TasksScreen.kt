package com.sety.lumen.mobile.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircle
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
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.todayIso

@Composable
fun TasksScreen(
    vm: LumenViewModel,
    revision: Long,
    onOpenTask: (LumenObject) -> Unit,
) {
    var filterIndex by remember { mutableIntStateOf(0) }
    val filters = listOf("Pendientes", "Hechas", "Todas")
    var newTaskText by remember { mutableStateOf("") }

    val allTasks = remember(revision) { vm.objects("task") }
    val tasks = remember(revision, filterIndex, allTasks) {
        when (filterIndex) {
            0 -> allTasks.filter { !it.isDone }
            1 -> allTasks.filter { it.isDone }
            else -> allTasks
        }.sortedWith(compareBy<LumenObject> { it.isDone }.thenBy { it.dueDate.ifBlank { "9999-12-31" } })
    }

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
                    .padding(horizontal = 18.dp, vertical = 10.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    "Tareas",
                    style = MaterialTheme.typography.headlineMedium,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                SegmentedPill(
                    tabs = filters,
                    selectedIndex = filterIndex,
                    onSelect = { filterIndex = it },
                )
            }
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 18.dp, end = 18.dp, top = 12.dp, bottom = 100.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                QuickAddTask(
                    value = newTaskText,
                    onValueChange = { newTaskText = it },
                    onAdd = {
                        if (newTaskText.isNotBlank()) {
                            vm.quickNote(
                                newTaskText,
                                isTask = true,
                                dueDate = todayIso(),
                                journalDate = todayIso(),
                            )
                            newTaskText = ""
                        }
                    },
                )
            }

            if (tasks.isEmpty()) {
                item {
                    LumenEmptyState(
                        title = "Sin tareas",
                        body = "Todas tus tareas están al día.",
                        icon = Icons.Default.CheckCircle,
                    )
                }
            } else {
                item {
                    LumenSectionHeader(
                        title = when (filterIndex) {
                            0 -> "Pendientes"
                            1 -> "Completadas"
                            else -> "Todas"
                        },
                        trailing = "${tasks.size}",
                    )
                }
                items(tasks, key = { it.id }) { task ->
                    TaskRow(
                        task = task,
                        onToggle = { done -> vm.toggleTask(task.id, done) },
                        onOpen = { onOpenTask(task) },
                    )
                }
            }
        }
    }
}

@Composable
private fun QuickAddTask(
    value: String,
    onValueChange: (String) -> Unit,
    onAdd: () -> Unit,
) {
    Surface(
        shape = MaterialTheme.shapes.small,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .height(48.dp)
                .padding(horizontal = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                Icons.Default.Add,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(20.dp),
            )
            Spacer(Modifier.width(10.dp))
            BasicTextField(
                value = value,
                onValueChange = onValueChange,
                modifier = Modifier.weight(1f),
                singleLine = true,
                textStyle = MaterialTheme.typography.bodyLarge.copy(
                    color = MaterialTheme.colorScheme.onSurface,
                ),
                cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                decorationBox = { innerTextField ->
                    Box {
                        if (value.isEmpty()) {
                            Text(
                                "Añadir una tarea…",
                                style = MaterialTheme.typography.bodyLarge,
                                color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.65f),
                            )
                        }
                        innerTextField()
                    }
                },
            )
            IconButton(
                onClick = onAdd,
                enabled = value.isNotBlank(),
                modifier = Modifier.size(32.dp),
            ) {
                Surface(
                    shape = CircleShape,
                    color = if (value.isNotBlank()) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.surfaceVariant
                    },
                    modifier = Modifier.size(26.dp),
                ) {
                    Box(contentAlignment = Alignment.Center) {
                        Icon(
                            Icons.Default.Check,
                            contentDescription = "Añadir tarea",
                            tint = if (value.isNotBlank()) {
                                MaterialTheme.colorScheme.onPrimary
                            } else {
                                MaterialTheme.colorScheme.onSurfaceVariant
                            },
                            modifier = Modifier.size(16.dp),
                        )
                    }
                }
            }
        }
    }
}
