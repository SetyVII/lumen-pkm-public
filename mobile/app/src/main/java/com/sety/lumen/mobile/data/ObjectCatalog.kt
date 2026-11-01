package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject

// Offline defaults mirror the server's built-in IDs; received schemas win.
private val offlineTypes = listOf(
    "note" to "Nota", "page" to "Página", "atomic" to "Nota atómica", "idea" to "Idea",
    "book" to "Libro", "person" to "Persona", "project" to "Proyecto", "task" to "Tarea",
    "quote" to "Cita", "tag" to "Etiqueta", "podcast" to "Podcast", "tweet" to "Tweet",
    "audio" to "Audio", "file" to "Archivo", "web" to "Enlace web", "video" to "Vídeo",
    "journal" to "Diario", "image" to "Imagen", "pdf" to "PDF", "query" to "Consulta", "collection" to "Colección",
).map { (id, name) ->
    val props = JSONArray()
    if (id != "tag") props.put(JSONObject().put("key", "tags").put("name", "Etiquetas").put("type", "object").put("target_type", "tag").put("multiple", true))
    val fields = when (id) {
        "book" -> listOf("author", "rating", "recommended_by", "medium", "status")
        "web" -> listOf("url", "status", "category", "topic")
        "task" -> listOf("status", "due_date", "context_id")
        "quote" -> listOf("author", "source")
        "podcast" -> listOf("host", "url")
        "project" -> listOf("status")
        "journal" -> listOf("date")
        "audio" -> listOf("duration", "mime")
        "video" -> listOf("url", "duration", "mime")
        "file", "image", "pdf" -> listOf("mime")
        "tweet" -> listOf("author", "url")
        else -> emptyList()
    }
    fields.forEach(props::put)
    LumenType(id, name, properties = props)
}

fun objectCatalog(received: List<LumenType>, objects: List<LumenObject> = emptyList()): List<LumenType> {
    val types = if (received.isEmpty()) offlineTypes else received
    return (types + objects.filter { obj -> types.none { it.id == obj.typeId } }
        .map { LumenType(it.typeId, it.typeId) }).distinctBy { it.id }.sortedBy { it.name.lowercase() }
}

val reviewStatuses = listOf("Para revisar", "En curso", "Revisado")
fun canonicalReviewStatus(value: String): String = when (value.trim().lowercase()) {
    "", "por revisar", "para revisar", "unread", "to review" -> "Para revisar"
    "leído", "leido", "revisado", "read", "reviewed" -> "Revisado"
    "en curso", "reading", "in progress" -> "En curso"
    else -> value
}

data class ObjectField(val key: String, val name: String, val kind: String = "text",
    val options: List<String> = emptyList(), val targetType: String = "", val multiple: Boolean = false)

fun objectFields(type: LumenType): List<ObjectField> = (0 until type.properties.length()).mapNotNull { i ->
    val raw = type.properties.opt(i)
    val definition = raw as? JSONObject
    val key = definition?.optString("key") ?: (raw as? String) ?: return@mapNotNull null
    if (key in setOf("is_template", "view", "item_type_id")) return@mapNotNull null
    val labels = mapOf("author" to "Autor", "rating" to "Valoración", "recommended_by" to "Recomendado por", "medium" to "Medio",
        "status" to "Estado", "due_date" to "Fecha límite", "date" to "Fecha", "url" to "URL", "source" to "Fuente",
        "category" to "Categoría", "topic" to "Tema", "host" to "Presentador", "context_id" to "Contexto")
    val options = definition?.optJSONArray("options")?.let { a -> (0 until a.length()).map { a.optString(it) } }.orEmpty()
    ObjectField(key, definition?.optString("name")?.ifBlank { null } ?: labels[key] ?: key,
        definition?.optString("type")?.ifBlank { null } ?: if (key == "status" && type.id == "web") "select" else if (key in setOf("date", "due_date")) "date" else "text",
        if (key == "status" && type.id == "web" && options.isEmpty()) reviewStatuses else options,
        definition?.optString("target_type").orEmpty(), definition?.optBoolean("multiple", false) == true)
}

fun relationIds(value: Any?): List<String> = when (value) {
    is JSONArray -> (0 until value.length()).mapNotNull { (value.opt(it) as? String)?.takeIf(String::isNotBlank) }
    is String -> listOf(value).filter(String::isNotBlank)
    else -> emptyList()
}

fun relatedObjectIds(obj: LumenObject, types: List<LumenType>): Set<String> {
    val type = objectCatalog(types).firstOrNull { it.id == obj.typeId }
    val propertyIds = type?.let(::objectFields).orEmpty().filter { it.kind in setOf("object", "relation") }
        .flatMap { relationIds(obj.property(it.key)) }
    return (obj.links + obj.propertyLinks + propertyIds).toSet() - obj.id
}

fun nativeReferenceIds(blocks: JSONArray?): Set<String> {
    val result = mutableSetOf<String>()
    fun visit(value: Any?) {
        when (value) {
            is JSONArray -> for (i in 0 until value.length()) visit(value.opt(i))
            is JSONObject -> {
                val id = value.optString("object_id").ifBlank { value.optString("href").takeIf { it.startsWith("lumen://object/") }?.removePrefix("lumen://object/").orEmpty() }
                if (id.isNotBlank()) result += id
                value.keys().forEach { key -> if (value.opt(key) is JSONObject || value.opt(key) is JSONArray) visit(value.opt(key)) }
            }
        }
    }
    visit(blocks)
    return result
}

fun webStatusForSchema(types: List<LumenType>, requested: String = "Para revisar"): String {
    val options = types.firstOrNull { it.id == "web" }?.let(::objectFields)?.firstOrNull { it.key == "status" }?.options.orEmpty()
    return options.firstOrNull { canonicalReviewStatus(it) == canonicalReviewStatus(requested) } ?: options.firstOrNull() ?: requested
}
