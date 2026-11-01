package com.sety.lumen.mobile.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircleOutline
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.UnfoldMore
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.ui.theme.LumenColors

enum class LumenNavTab {
    Home,
    Journal,
    Tasks,
    Search,
}

@Composable
fun LumenDock(
    currentTab: LumenNavTab,
    onSelectTab: (LumenNavTab) -> Unit,
    onOpenCreate: () -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
) {
    var workspaceMenuExpanded by remember { mutableStateOf(false) }

    Box(
        modifier = modifier
            .fillMaxWidth()
            .navigationBarsPadding()
            .padding(horizontal = 24.dp, vertical = 10.dp),
        contentAlignment = Alignment.Center,
    ) {
        Surface(
            modifier = Modifier
                .height(52.dp)
                .fillMaxWidth(),
            shape = CircleShape,
            color = Color(0xFF161619),
            border = BorderStroke(1.dp, Color(0xFF26262A)),
            shadowElevation = 12.dp,
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 6.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Box(modifier = Modifier.weight(1.45f)) {
                    Surface(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(38.dp)
                            .clip(CircleShape)
                            .clickable { onSelectTab(LumenNavTab.Home) },
                        shape = CircleShape,
                        color = if (currentTab == LumenNavTab.Home) {
                            LumenColors.WorkspacePill
                        } else {
                            Color.Transparent
                        },
                    ) {
                        Row(
                            modifier = Modifier.padding(horizontal = 10.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Icon(
                                Icons.Default.Home,
                                contentDescription = null,
                                tint = if (currentTab == LumenNavTab.Home) Color.White else Color(0xFFA7A2A1),
                                modifier = Modifier.size(16.dp),
                            )
                            Spacer(Modifier.width(6.dp))
                            Text(
                                "Lumen",
                                style = MaterialTheme.typography.labelMedium,
                                fontWeight = FontWeight.SemiBold,
                                color = if (currentTab == LumenNavTab.Home) Color.White else Color(0xFFA7A2A1),
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.weight(1f),
                            )
                            Box(
                                modifier = Modifier
                                    .size(24.dp)
                                    .clip(CircleShape)
                                    .clickable { workspaceMenuExpanded = true },
                                contentAlignment = Alignment.Center,
                            ) {
                                Icon(
                                    Icons.Default.UnfoldMore,
                                    contentDescription = "Cambiar espacio",
                                    tint = if (currentTab == LumenNavTab.Home) Color.White.copy(alpha = 0.8f) else Color(0xFFA7A2A1),
                                    modifier = Modifier.size(15.dp),
                                )
                            }
                        }
                    }
                    DropdownMenu(
                        expanded = workspaceMenuExpanded,
                        onDismissRequest = { workspaceMenuExpanded = false },
                        containerColor = MaterialTheme.colorScheme.surface,
                    ) {
                        DropdownMenuItem(
                            text = { Text("Lumen") },
                            leadingIcon = { Icon(Icons.Default.Home, contentDescription = null) },
                            trailingIcon = { Icon(Icons.Default.Check, contentDescription = null) },
                            onClick = {
                                workspaceMenuExpanded = false
                                onSelectTab(LumenNavTab.Home)
                            },
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

                DockItem(
                    icon = Icons.Default.CalendarMonth,
                    label = "Diario",
                    selected = currentTab == LumenNavTab.Journal,
                    onClick = { onSelectTab(LumenNavTab.Journal) },
                    modifier = Modifier.weight(1f),
                )
                DockItem(
                    icon = Icons.Default.Add,
                    label = "Crear",
                    selected = false,
                    onClick = onOpenCreate,
                    modifier = Modifier.weight(1f),
                    iconSize = 26.dp,
                )
                DockItem(
                    icon = Icons.Default.CheckCircleOutline,
                    label = "Tareas",
                    selected = currentTab == LumenNavTab.Tasks,
                    onClick = { onSelectTab(LumenNavTab.Tasks) },
                    modifier = Modifier.weight(1f),
                )
                DockItem(
                    icon = Icons.Default.Search,
                    label = "Buscar",
                    selected = currentTab == LumenNavTab.Search,
                    onClick = { onSelectTab(LumenNavTab.Search) },
                    modifier = Modifier.weight(1f),
                )
            }
        }
    }
}

@Composable
private fun DockItem(
    icon: ImageVector,
    label: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    iconSize: Dp = 24.dp,
) {
    Box(
        modifier = modifier
            .fillMaxHeight()
            .clip(CircleShape)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Icon(
            icon,
            contentDescription = label,
            tint = if (selected) Color.White else Color(0xFFA7A2A1),
            modifier = Modifier.size(iconSize),
        )
    }
}
