package com.sety.lumen.mobile.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import coil.compose.AsyncImage
import com.sety.lumen.mobile.data.LumenAsset
import com.sety.lumen.mobile.data.LumenObject
import java.io.File

@Composable
fun TaskDetailDialog(
    task: LumenObject,
    onDismiss: () -> Unit,
    onToggle: (Boolean) -> Unit,
    onPostpone: () -> Unit,
    onDelete: () -> Unit,
) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = MaterialTheme.shapes.large) {
            Column(Modifier.padding(22.dp)) {
                Text(task.title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                if (task.content.isNotBlank()) {
                    Spacer(Modifier.height(8.dp))
                    Text(task.content, style = MaterialTheme.typography.bodyMedium)
                }
                Spacer(Modifier.height(12.dp))
                Text(
                    if (task.dueDate.isBlank()) "Sin fecha" else "Vence ${task.dueDate}",
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(18.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { onToggle(!task.isDone) }) { Text(if (task.isDone) "Reabrir" else "Completar") }
                    TextButton(onClick = onPostpone) { Text("Posponer") }
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onDelete) { Text("Eliminar", color = MaterialTheme.colorScheme.error) }
                    TextButton(onClick = onDismiss) { Text("Cerrar") }
                }
            }
        }
    }
}

@Composable
fun ImageDialog(asset: LumenAsset?, caption: String, onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = MaterialTheme.shapes.large) {
            Column(Modifier.padding(12.dp)) {
                val path = asset?.localPath.orEmpty()
                if (path.isNotBlank()) {
                    AsyncImage(
                        model = rememberMediaModel(path),
                        contentDescription = caption,
                        modifier = Modifier.fillMaxWidth().heightIn(max = 620.dp),
                        contentScale = ContentScale.Fit,
                    )
                } else {
                    Text("La foto aún no está descargada. Conéctate a casa para verla.", Modifier.padding(18.dp))
                }
                Text(caption.ifBlank { "Momento" }, Modifier.padding(10.dp), style = MaterialTheme.typography.bodyMedium)
                TextButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) { Text("Cerrar") }
            }
        }
    }
}

@Composable
fun ObjectDetailDialog(
    obj: LumenObject,
    asset: LumenAsset?,
    onDismiss: () -> Unit,
    onOpen: () -> Unit,
    onStatus: (String) -> Unit,
    onDelete: () -> Unit,
    onToggleTask: (Boolean) -> Unit,
    onPostponeTask: () -> Unit,
) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = MaterialTheme.shapes.large) {
            Column(
                Modifier.padding(22.dp).heightIn(max = 680.dp).verticalScroll(rememberScrollState()),
            ) {
                Text(obj.title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                Text(
                    obj.typeId.replaceFirstChar { it.uppercase() },
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (obj.isImage && asset != null) {
                    Spacer(Modifier.height(12.dp))
                    AsyncImage(
                        model = rememberMediaModel(asset.localPath),
                        contentDescription = obj.title,
                        modifier = Modifier.fillMaxWidth().height(280.dp),
                        contentScale = ContentScale.Crop,
                    )
                }
                if (obj.isWeb) {
                    Spacer(Modifier.height(10.dp))
                    Text(obj.stringProperty("url"), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.primary)
                    val description = obj.stringProperty("summary").ifBlank { obj.stringProperty("description") }.ifBlank { obj.stringProperty("og_description") }
                    if (description.isNotBlank()) {
                        Spacer(Modifier.height(8.dp))
                        Text(description, style = MaterialTheme.typography.bodyMedium)
                    }
                    Spacer(Modifier.height(12.dp))
                    Text("Estado", style = MaterialTheme.typography.labelMedium)
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        listOf("Para revisar", "En curso", "Revisado").forEach { status ->
                            TextButton(onClick = { onStatus(status) }) {
                                Text(status, color = if (obj.status == status) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                        }
                    }
                }
                if (obj.content.isNotBlank() && !obj.isImage) {
                    Spacer(Modifier.height(10.dp))
                    Text(obj.content, style = MaterialTheme.typography.bodyMedium)
                }
                if (obj.isTask) {
                    Spacer(Modifier.height(12.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { onToggleTask(!obj.isDone) }) { Text(if (obj.isDone) "Reabrir" else "Completar") }
                        TextButton(onClick = onPostponeTask) { Text("Posponer") }
                    }
                }
                Spacer(Modifier.height(14.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onDelete) { Text("Eliminar", color = MaterialTheme.colorScheme.error) }
                    if (obj.isWeb) TextButton(onClick = onOpen) { Text("Abrir enlace") }
                    TextButton(onClick = onDismiss) { Text("Cerrar") }
                }
            }
        }
    }
}

@Composable
fun AddLinkDialog(onDismiss: () -> Unit, onSave: (String, String) -> Unit) {
    var url by remember { mutableStateOf("") }
    var title by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = MaterialTheme.shapes.large) {
            Column(Modifier.padding(22.dp)) {
                Text("Guardar enlace", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(12.dp))
                OutlinedTextField(
                    value = url,
                    onValueChange = { url = it; error = "" },
                    label = { Text("URL") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(
                    value = title,
                    onValueChange = { title = it },
                    label = { Text("Título (opcional)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                if (error.isNotBlank()) {
                    Text(error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
                }
                Row(Modifier.fillMaxWidth().padding(top = 12.dp), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onDismiss) { Text("Cancelar") }
                    Button(onClick = {
                        if (!url.startsWith("http://") && !url.startsWith("https://")) {
                            error = "El enlace debe empezar por http:// o https://"
                        } else {
                            onSave(url, title)
                        }
                    }) { Text("Guardar") }
                }
            }
        }
    }
}

@Composable
fun ConfirmDialog(
    title: String,
    body: String,
    confirmLabel: String,
    onDismiss: () -> Unit,
    onConfirm: () -> Unit,
) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(shape = MaterialTheme.shapes.large) {
            Column(Modifier.padding(22.dp)) {
                Text(title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                Spacer(Modifier.height(8.dp))
                Text(body, style = MaterialTheme.typography.bodyMedium)
                Row(Modifier.fillMaxWidth().padding(top = 18.dp), horizontalArrangement = Arrangement.End) {
                    TextButton(onClick = onDismiss) { Text("Cancelar") }
                    Button(onClick = onConfirm) { Text(confirmLabel) }
                }
            }
        }
    }
}

@Composable
fun MiniStat(label: String, value: String) {
    Card {
        Column(Modifier.padding(14.dp)) {
            Text(value, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
            Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
