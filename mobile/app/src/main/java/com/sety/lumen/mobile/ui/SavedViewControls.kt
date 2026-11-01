package com.sety.lumen.mobile.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.data.LumenObject
import org.json.JSONObject

internal data class ViewDraft(val base: JSONObject, val review: String = "Todos", val propertyKey: String = "", val propertyValue: String = "")

/** Expose simple saved conditions in the existing controls so changing a
 * saved pending view to Reviewed replaces its condition instead of ANDing both.
 * Complex OR/nested conditions remain intact in the shared spec. */
internal fun editableViewDraft(spec: JSONObject, filterKeys: Set<String>, web: Boolean): ViewDraft {
    val base = JSONObject(spec.toString())
    val filter = base.optJSONObject("filter") ?: return ViewDraft(base)
    val rules = filter.optJSONArray("rules")
    if (rules != null && filter.optString("mode", "and") != "and") return ViewDraft(base)
    val leaves = if (rules == null) listOf(filter) else (0 until rules.length()).map { rules.getJSONObject(it) }
    var review = "Todos"; var propertyKey = ""; var propertyValue = ""
    val kept = org.json.JSONArray()
    for (rule in leaves) {
        val key = rule.optString("field").removePrefix("properties.")
        val raw = rule.opt("value")
        val values = if (raw is org.json.JSONArray) (0 until raw.length()).map { raw.optString(it) } else listOf(raw as? String ?: "")
        val status = values.map { if (web) com.sety.lumen.mobile.data.canonicalReviewStatus(it) else it }.distinct()
        if (key == "status" && review == "Todos" && rule.optString("op") in setOf("equals", "in") && status.size == 1 && status[0].isNotBlank()) review = status[0]
        else if (key in filterKeys && propertyKey.isEmpty() && rule.optString("op") == "equals" && raw is String) { propertyKey = key; propertyValue = raw }
        else kept.put(rule)
    }
    if (kept.length() == 0) base.remove("filter")
    else base.put("filter", if (rules != null) JSONObject(filter.toString()).put("rules", kept)
        else JSONObject().put("mode", "and").put("rules", kept))
    return ViewDraft(base, review, propertyKey, propertyValue)
}

@Composable
internal fun SavedViewControls(
    vm: LumenViewModel, scope: String, activeId: String, spec: JSONObject,
    onSelect: (LumenObject?) -> Unit, onSaved: (LumenObject) -> Unit,
    onClear: () -> Unit, onDeleted: () -> Unit, onSort: (String, String) -> Unit,
) {
    val active = vm.getObject(activeId)
    val views = vm.objects().filter { it.typeId == "query" && it.properties.optJSONObject("view")?.optString("type_id").orEmpty() == scope }
    var chooser by remember { mutableStateOf(false) }
    var actions by remember { mutableStateOf(false) }
    var sort by remember { mutableStateOf(false) }
    var naming by remember { mutableStateOf(false) }
    var saveAs by remember { mutableStateOf(false) }
    var deleting by remember { mutableStateOf(false) }
    var name by rememberSaveable { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
        Box(Modifier.weight(1f)) {
            TextButton(onClick = { chooser = true }) { Text(active?.title ?: "Vistas guardadas", maxLines = 1, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis) }
            DropdownMenu(chooser, { chooser = false }) {
                DropdownMenuItem(text = { Text("Todos los objetos") }, onClick = { chooser = false; onSelect(null) })
                views.forEach { view -> DropdownMenuItem(text = { Text(view.title) }, onClick = { chooser = false; onSelect(view) }) }
                if (views.isEmpty()) DropdownMenuItem(text = { Text("Todavía no hay vistas guardadas") }, enabled = false, onClick = {})
            }
        }
        Box {
            TextButton(onClick = { sort = true }) { Text("Ordenar") }
            DropdownMenu(sort, { sort = false }) {
                listOf(Triple("title", "asc", "Nombre A–Z"), Triple("title", "desc", "Nombre Z–A"),
                    Triple("updated_at", "desc", "Última modificación"), Triple("created_at", "desc", "Más recientes"),
                    Triple("created_at", "asc", "Más antiguos")).forEach { (field, direction, label) ->
                    DropdownMenuItem(text = { Text(label) }, onClick = { sort = false; onSort(field, direction) })
                }
            }
        }
        Box {
            TextButton(onClick = { actions = true }) { Text("Guardar vista") }
            DropdownMenu(actions, { actions = false }) {
                if (active != null) DropdownMenuItem(text = { Text("Actualizar ${active.title}") }, onClick = {
                    actions = false; onSaved(vm.repository.saveView(active.title, spec, active.id))
                })
                DropdownMenuItem(text = { Text("Guardar como nueva vista") }, onClick = {
                    actions = false; saveAs = true; name = ""; naming = true; error = ""
                })
                if (active != null) {
                    DropdownMenuItem(text = { Text("Renombrar vista") }, onClick = {
                        actions = false; saveAs = false; name = active.title; naming = true; error = ""
                    })
                    DropdownMenuItem(text = { Text("Eliminar vista") }, onClick = { actions = false; deleting = true })
                }
                DropdownMenuItem(text = { Text("Quitar filtros y búsqueda") }, onClick = { actions = false; onClear() })
            }
        }
    }
    if (naming) AlertDialog(onDismissRequest = { naming = false }, title = { Text(if (saveAs) "Guardar vista" else "Renombrar vista") },
        text = { Column {
            OutlinedTextField(name, { name = it; error = "" }, label = { Text("Nombre de la vista") }, singleLine = true)
            if (error.isNotEmpty()) Text(error, color = MaterialTheme.colorScheme.error)
            Text("Se guardan los filtros, la búsqueda y el orden. Los objetos se actualizan al cambiar sus propiedades.", Modifier.padding(top = 8.dp), style = MaterialTheme.typography.bodySmall)
        } }, confirmButton = { TextButton(enabled = name.isNotBlank(), onClick = {
            runCatching { vm.repository.saveView(name, spec, if (saveAs) null else activeId) }
                .onSuccess { naming = false; onSaved(it) }.onFailure { error = it.message.orEmpty() }
        }) { Text("Guardar") } }, dismissButton = { TextButton(onClick = { naming = false }) { Text("Cancelar") } })
    if (deleting && active != null) AlertDialog(onDismissRequest = { deleting = false }, title = { Text("¿Eliminar ${active.title}?") },
        text = { Text("Los objetos de la vista se conservarán.") }, confirmButton = { TextButton(onClick = {
            vm.deleteObject(active.id); deleting = false; onDeleted()
        }) { Text("Eliminar") } }, dismissButton = { TextButton(onClick = { deleting = false }) { Text("Cancelar") } })
}

internal fun viewFilterDescription(rule: JSONObject, vm: LumenViewModel): String {
    rule.optJSONArray("rules")?.let { rules -> return (0 until rules.length()).joinToString(if (rule.optString("mode") == "or") " o " else " y ") {
        "(${viewFilterDescription(rules.getJSONObject(it), vm)})"
    } }
    val key = rule.optString("field").removePrefix("properties.")
    val field = vm.types().flatMap { com.sety.lumen.mobile.data.objectFields(it) }.firstOrNull { it.key == key }?.name
        ?: mapOf("title" to "Nombre", "content" to "Contenido", "links" to "Enlaces", "backlinks" to "Referencias")[key] ?: key
    val op = mapOf("equals" to "es", "not_equals" to "no es", "contains" to "contiene", "not_contains" to "no contiene", "in" to "es uno de",
        "empty" to "está vacío", "not_empty" to "tiene valor", "exists" to "tiene valor", "before" to "antes de", "after" to "después de",
        "starts_with" to "empieza por", "ends_with" to "termina en", "lt" to "<", "gt" to ">", "lte" to "≤", "gte" to "≥")[rule.optString("op", "contains")] ?: rule.optString("op")
    fun label(value: Any?): String = if (value is String) vm.getObject(value)?.title ?: value else com.sety.lumen.mobile.data.LumenQuery.display(value)
    val value = rule.opt("value")
    val shown = if (value is org.json.JSONArray) (0 until value.length()).joinToString(", ") { label(value.opt(it)) } else label(value)
    return "$field $op $shown".trim()
}
