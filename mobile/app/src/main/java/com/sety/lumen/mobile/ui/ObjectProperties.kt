package com.sety.lumen.mobile.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import java.time.LocalDate

@Composable
fun ObjectProperties(obj: LumenObject, vm: LumenViewModel, exclude: Set<String> = emptySet()) {
    val type = vm.types().firstOrNull { it.id == obj.typeId } ?: return
    val fields = objectFields(type).filter { it.key !in exclude }
    var editing by remember(obj.id) { mutableStateOf<ObjectField?>(null) }
    fields.forEach { field ->
        val raw = obj.property(field.key)
        val ids = if (field.kind in setOf("object", "relation")) relationIds(raw) else emptyList()
        val value = when {
            ids.isNotEmpty() -> ids.joinToString { vm.getObject(it)?.title ?: "Objeto no disponible" }
            raw == null || raw == JSONObject.NULL -> ""
            field.kind in setOf("boolean", "bool") -> if (raw == true) "Sí" else "No"
            field.kind == "date" && raw is JSONObject -> listOf(raw.optString("start"), raw.optString("end")).filter { it.isNotBlank() }.joinToString(" → ")
            raw is JSONArray -> (0 until raw.length()).joinToString { raw.optString(it) }
            else -> raw.toString()
        }
        LumenPropertyRow(field.name, if (obj.isWeb && field.key == "status") canonicalReviewStatus(value) else value, onClick = { editing = field })
        ids.mapNotNull(vm::getObject).forEach { target ->
            TextButton(onClick = { vm.openObject(target) }) { Text("Abrir ${target.title}") }
        }
    }
    editing?.let { field ->
        EditObjectFieldDialog(field, obj.property(field.key), vm.objects().filter { it.id != obj.id },
            onDismiss = { editing = null }, onSave = { value -> vm.updateProperty(obj.id, field.key, value); editing = null })
    }
}

@Composable
fun EditObjectFieldDialog(field: ObjectField, current: Any?, objects: List<LumenObject>, onDismiss: () -> Unit, onSave: (Any) -> Unit, confirmLabel: String = "Guardar") {
    var text by remember(field.key) { mutableStateOf(if (field.kind == "date" && current is JSONObject) current.optString("start") else if (current == null || current == JSONObject.NULL) "" else current.toString()) }
    var endDate by remember(field.key) { mutableStateOf((current as? JSONObject)?.optString("end").orEmpty()) }
    var includeTime by remember(field.key) { mutableStateOf((current as? JSONObject)?.optBoolean("include_time", false) == true) }
    var selected by remember(field.key) { mutableStateOf(relationIds(current).toSet()) }
    var enabled by remember(field.key) { mutableStateOf(current == true) }
    var query by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    val relation = field.kind in setOf("object", "relation")
    val choices = objects.filter { (field.targetType.isBlank() || it.typeId == field.targetType) && it.title.contains(query, true) }
    AlertDialog(onDismissRequest = onDismiss, title = { Text(field.name) }, text = {
        Column(Modifier.fillMaxWidth()) {
            when {
                relation -> {
                    OutlinedTextField(query, { query = it }, label = { Text("Buscar objeto") }, singleLine = true)
                    LazyColumn(Modifier.heightIn(max = 320.dp)) {
                        items(choices, key = { it.id }) { target ->
                            Row(Modifier.fillMaxWidth().clickable {
                                selected = if (field.multiple) { if (target.id in selected) selected - target.id else selected + target.id } else setOf(target.id)
                            }.padding(vertical = 8.dp)) {
                                Checkbox(target.id in selected, onCheckedChange = null)
                                Column(Modifier.weight(1f)) { Text(target.title); Text(target.typeId, style = MaterialTheme.typography.labelSmall) }
                            }
                        }
                    }
                    TextButton(onClick = { selected = emptySet() }) { Text("Quitar relación") }
                }
                field.options.isNotEmpty() -> Column(Modifier.heightIn(max = 340.dp).verticalScroll(rememberScrollState())) {
                    field.options.forEach { option ->
                        Row(Modifier.fillMaxWidth().clickable {
                            if (field.multiple) selected = if (option in selected) selected - option else selected + option else text = option
                        }.padding(vertical = 6.dp)) {
                            if (field.multiple) Checkbox(option in selected, onCheckedChange = null)
                            else RadioButton(selected = text == option || canonicalReviewStatus(text) == option, onClick = null)
                            Text(option, Modifier.padding(12.dp))
                        }
                    }
                }
                field.kind == "date" -> Column {
                    OutlinedTextField(text, { text = it; error = "" }, label = { Text("Inicio · AAAA-MM-DD") }, singleLine = true)
                    OutlinedTextField(endDate, { endDate = it; error = "" }, label = { Text("Fin opcional · AAAA-MM-DD") }, singleLine = true)
                    Row { Checkbox(includeTime, { includeTime = it }); Text("Incluir hora (THH:mm)", Modifier.padding(top = 12.dp)) }
                }
                field.kind in setOf("bool", "boolean") -> Row { Switch(enabled, { enabled = it }); Text(if (enabled) "Sí" else "No", Modifier.padding(14.dp)) }
                else -> OutlinedTextField(text, { text = it; error = "" }, label = { Text(if (field.kind == "date") "AAAA-MM-DD" else field.name) }, isError = error.isNotEmpty(), singleLine = field.kind != "text")
            }
            if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error)
        }
    }, confirmButton = { TextButton(onClick = {
        val value: Any? = when {
            relation -> if (field.multiple) JSONArray(selected.toList()) else selected.firstOrNull().orEmpty()
            field.options.isNotEmpty() && field.multiple -> JSONArray(selected.toList())
            field.kind in setOf("bool", "boolean") -> enabled
            field.kind == "number" -> if (text.isBlank()) JSONObject.NULL else text.toDoubleOrNull()?.takeIf { it.isFinite() }
            field.kind == "date" && text.isNotBlank() -> runCatching {
                fun date(value: String) { if (includeTime) java.time.LocalDateTime.parse(value) else LocalDate.parse(value) }
                date(text)
                if (endDate.isNotBlank()) { date(endDate); require(endDate >= text) }
                if (current is JSONObject || endDate.isNotBlank() || includeTime) JSONObject((current as? JSONObject)?.toString() ?: "{}")
                    .put("start", text).put("end", endDate).put("include_time", includeTime)
                else text
            }.getOrNull()
            else -> text
        }
        if (value == null) error = if (field.kind == "date") "Usa una fecha válida: AAAA-MM-DD" else "Escribe un número válido"
        else onSave(value)
    }) { Text(confirmLabel) } }, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancelar") } })
}

@Composable
fun ObjectRelations(obj: LumenObject, vm: LumenViewModel) {
    val all = vm.objects()
    val types = vm.types()
    val outgoing = relatedObjectIds(obj, types).mapNotNull(vm::getObject)
    val incoming = all.filter { obj.id in relatedObjectIds(it, types) }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(20.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        item { Text(obj.title, style = MaterialTheme.typography.headlineSmall) }
        item { LumenSectionHeader("Objetos vinculados", trailing = "${outgoing.size}") }
        if (outgoing.isEmpty()) item { Text("Este objeto todavía no enlaza a otros objetos.") }
        items(outgoing, key = { "out-${it.id}" }) { target -> LumenObjectRow(target, subtitle = vm.types().firstOrNull { it.id == target.typeId }?.name ?: target.typeId, onClick = { vm.openObject(target) }) }
        item { LumenSectionHeader("Enlaces entrantes", trailing = "${incoming.size}") }
        if (incoming.isEmpty()) item { Text("Ningún objeto de este dispositivo enlaza aquí todavía.") }
        items(incoming, key = { "in-${it.id}" }) { target -> LumenObjectRow(target, subtitle = vm.types().firstOrNull { it.id == target.typeId }?.name ?: target.typeId, onClick = { vm.openObject(target) }) }
    }
}

@Composable
fun CreateObjectDialog(vm: LumenViewModel, typeId: String? = null, onDismiss: () -> Unit, onCreated: (LumenObject) -> Unit) {
    val types = vm.types().filter { it.id !in setOf("journal", "query", "collection", "image", "pdf", "file", "audio", "video") }
    var selected by remember { mutableStateOf(types.firstOrNull { it.id == typeId } ?: types.firstOrNull()) }
    var title by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Nuevo objeto") }, text = {
        Column(Modifier.heightIn(max = 420.dp).verticalScroll(rememberScrollState())) {
            if (typeId == null) types.forEach { type ->
                Row(Modifier.fillMaxWidth().clickable { selected = type }.padding(vertical = 4.dp)) {
                    RadioButton(selected?.id == type.id, onClick = null); Text(type.name, Modifier.padding(12.dp))
                }
            } else Text(selected?.name.orEmpty())
            OutlinedTextField(title, { title = it }, label = { Text("Título") }, singleLine = true)
            if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error)
        }
    }, confirmButton = { TextButton(enabled = title.isNotBlank() && selected != null, onClick = {
        runCatching { vm.repository.createObject(selected!!.id, title.trim()) }.onSuccess(onCreated).onFailure { error = it.message.orEmpty() }
    }) { Text("Crear") } }, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancelar") } })
}

@Composable
fun CreateTypeDialog(vm: LumenViewModel, onDismiss: () -> Unit, onCreated: (LumenType) -> Unit) {
    var name by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    AlertDialog(onDismissRequest = onDismiss, title = { Text("Nuevo tipo de objeto") }, text = {
        Column { OutlinedTextField(name, { name = it }, label = { Text("Nombre del tipo") }, singleLine = true)
            if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error) }
    }, confirmButton = { TextButton(enabled = name.isNotBlank(), onClick = {
        runCatching { vm.repository.createObjectType(name) }.onSuccess(onCreated).onFailure { error = it.message.orEmpty() }
    }) { Text("Crear tipo") } }, dismissButton = { TextButton(onClick = onDismiss) { Text("Cancelar") } })
}
