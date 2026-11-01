package com.sety.lumen.mobile.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ChevronLeft
import androidx.compose.material.icons.filled.ChevronRight
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import coil.compose.AsyncImage
import java.io.File
import java.time.LocalDate
import java.time.YearMonth
import java.time.format.DateTimeFormatter
import java.util.Locale

@Composable
fun CalendarScreen(
    vm: LumenViewModel,
    revision: Long,
    onBack: () -> Unit,
    onSelect: (String) -> Unit,
) {
    var month by remember { mutableStateOf(YearMonth.now()) }
    val cells = remember(revision, month) { vm.calendar(month) }
    val monthLabel = month.atDay(1)
        .format(DateTimeFormatter.ofPattern("MMMM yyyy", Locale.forLanguageTag("es")))

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
                    icon = Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = "Volver",
                    onClick = onBack,
                )
                Text(
                    monthLabel.replaceFirstChar { it.uppercase() },
                    modifier = Modifier
                        .weight(1f)
                        .padding(horizontal = 12.dp),
                    style = MaterialTheme.typography.titleLarge,
                    fontWeight = FontWeight.SemiBold,
                    color = MaterialTheme.colorScheme.onSurface,
                )
                IconButton(onClick = { month = month.minusMonths(1) }) {
                    Icon(
                        Icons.Default.ChevronLeft,
                        contentDescription = "Mes anterior",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                IconButton(onClick = { month = month.plusMonths(1) }) {
                    Icon(
                        Icons.Default.ChevronRight,
                        contentDescription = "Mes siguiente",
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 10.dp),
        ) {
            listOf("L", "M", "X", "J", "V", "S", "D").forEach { label ->
                Text(
                    label,
                    modifier = Modifier.weight(1f),
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    textAlign = TextAlign.Center,
                )
            }
        }

        LazyVerticalGrid(
            columns = GridCells.Fixed(7),
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(start = 14.dp, end = 14.dp, bottom = 24.dp),
            horizontalArrangement = Arrangement.spacedBy(3.dp),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            items(cells) { cell ->
                val asset = if (cell.coverAssetId.isBlank()) null else vm.asset(cell.coverAssetId)
                val path = asset?.thumbPath?.takeIf { it.isNotBlank() && File(it).exists() }
                    ?: asset?.localPath?.takeIf { it.isNotBlank() && File(it).exists() }
                    ?: ""
                val isToday = cell.date == LocalDate.now()
                Box(
                    Modifier
                        .aspectRatio(0.82f)
                        .clip(RoundedCornerShape(9.dp))
                        .background(
                            if (cell.inMonth) {
                                MaterialTheme.colorScheme.surface
                            } else {
                                Color.Transparent
                            }
                        )
                        .clickable(enabled = cell.inMonth) { onSelect(cell.date.toString()) },
                ) {
                    if (path.isNotBlank()) {
                        AsyncImage(
                            model = rememberMediaModel(path),
                            contentDescription = cell.date.toString(),
                            modifier = Modifier.fillMaxSize(),
                            contentScale = ContentScale.Crop,
                            alpha = if (cell.inMonth) 1f else 0.3f,
                        )
                    }
                    if (isToday) {
                        Box(
                            Modifier
                                .align(Alignment.TopEnd)
                                .padding(5.dp)
                                .size(6.dp)
                                .clip(CircleShape)
                                .background(MaterialTheme.colorScheme.primary),
                        )
                    }
                    if (cell.hasContent && path.isBlank()) {
                        Box(
                            Modifier
                                .align(Alignment.BottomCenter)
                                .padding(bottom = 7.dp)
                                .size(4.dp)
                                .clip(CircleShape)
                                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.8f)),
                        )
                    }
                    Text(
                        cell.date.dayOfMonth.toString(),
                        modifier = Modifier
                            .align(Alignment.TopStart)
                            .padding(5.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = when {
                            path.isNotBlank() -> Color.White
                            isToday -> MaterialTheme.colorScheme.primary
                            cell.inMonth -> MaterialTheme.colorScheme.onSurface
                            else -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                        },
                        fontWeight = if (isToday) FontWeight.Bold else FontWeight.Normal,
                    )
                }
            }
        }
    }
}
