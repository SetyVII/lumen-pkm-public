package com.sety.lumen.mobile.ui

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject

@Composable
fun LinksScreen(vm: LumenViewModel, revision: Long) {
    var filter by remember { mutableStateOf("Para revisar") }
    var selected by remember { mutableStateOf<LumenObject?>(null) }
    var showAdd by remember { mutableStateOf(false) }
    val context = LocalContext.current
    val filters = listOf("Todos", "Para revisar", "En curso", "Revisado")
    val links = remember(revision, filter) { vm.webLinks(if (filter == "Todos") null else filter) }

    LazyColumn(
        modifier = Modifier.fillMaxWidth().statusBarsPadding(),
        contentPadding = PaddingValues(18.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text("Enlaces", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.SemiBold)
                TextButton(onClick = { showAdd = true }) {
                    Icon(Icons.Default.Add, contentDescription = null)
                    Text("Añadir")
                }
            }
            Text(
                "Guarda ahora, lee cuando quieras. La portada se descarga en el teléfono.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        item {
            LazyRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                items(filters) { option ->
                    FilterChip(selected = filter == option, onClick = { filter = option }, label = { Text(option) })
                }
            }
        }
        if (links.isEmpty()) {
            item {
                EmptyState(
                    "No hay enlaces aquí",
                    "Comparte una URL desde el navegador o desde otra app. Se guardará con estado «Para revisar».",
                )
            }
        } else {
            items(links, key = { it.id }) { link ->
                ObjectCard(
                    obj = link,
                    asset = vm.asset(link.assetId),
                    onClick = { selected = link },
                )
            }
        }
    }

    if (showAdd) {
        AddLinkDialog(
            onDismiss = { showAdd = false },
            onSave = { url, title ->
                showAdd = false
                vm.addLink(url, title) { }
            },
        )
    }
    selected?.let { link ->
        ObjectDetailDialog(
            obj = link,
            asset = vm.asset(link.assetId),
            onDismiss = { selected = null },
            onOpen = {
                val url = link.stringProperty("url")
                if (url.isNotBlank()) runCatching { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }
            },
            onStatus = { vm.setWebStatus(link.id, it); selected = null },
            onDelete = { vm.deleteObject(link.id); selected = null },
            onToggleTask = { vm.toggleTask(link.id, it); selected = null },
            onPostponeTask = { vm.postponeTask(link.id); selected = null },
        )
    }
}
