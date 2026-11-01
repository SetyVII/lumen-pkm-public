package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject
import java.text.Collator

/** Android implementation of the shared view spec used by desktop LumenQuery.
 * Keep operators and ordering in step with desktop/src/views/lumen_views.cpp.
 * This runs over the decrypted local library, including unsynchronised edits. */
object LumenQuery {
    private fun array(value: Any?) = (value as? JSONArray)?.let { a -> (0 until a.length()).map { a.opt(it) } }.orEmpty()
    private fun canonicalJson(value: Any?): String = when (value) {
        null, JSONObject.NULL -> "null"
        is String -> JSONObject.quote(value)
        is JSONObject -> value.keys().asSequence().sorted().joinToString(",", "{", "}") { JSONObject.quote(it) + ":" + canonicalJson(value.opt(it)) }
        is JSONArray -> array(value).joinToString(",", "[", "]", transform = ::canonicalJson)
        else -> value.toString()
    }
    fun display(value: Any?): String = when (value) {
        null, JSONObject.NULL -> ""
        is Boolean -> if (value) "Sí" else "No"
        is JSONArray -> array(value).joinToString(", ") { display(it) }
        is JSONObject -> canonicalJson(value) // QJsonObject emits sorted keys.
        is Number -> if (value.toDouble() == value.toLong().toDouble()) value.toLong().toString() else value.toString()
        else -> value.toString()
    }
    private fun plain(obj: LumenObject) = obj.content.replace(Regex("<[^>]+>"), "").replace(Regex("\\s+"), " ").trim()
    fun field(obj: LumenObject, name: String, all: List<LumenObject>): Any? = when (name) {
        "content", "text" -> plain(obj)
        "links" -> JSONArray((obj.links + obj.propertyLinks).distinct())
        "backlinks" -> JSONArray(all.filter { obj.id in it.links || obj.id in it.propertyLinks }.map { it.id })
        else -> if (name.startsWith("properties.")) obj.property(name.removePrefix("properties.")) else obj.toJson().opt(name) ?: obj.property(name)
    }
    private fun blank(v: Any?) = v == null || v == JSONObject.NULL || (v is String && v.isBlank()) || (v is JSONArray && v.length() == 0)
    private fun equal(got: Any?, wanted: Any?): Boolean {
        if (got is JSONArray && wanted !is JSONArray) return array(got).any { it == wanted }
        if (got is Number) display(wanted).toDoubleOrNull()?.let { return got.toDouble() == it }
        if (got is Boolean) when (display(wanted).lowercase()) {
            "true", "1", "sí", "si", "yes" -> return got
            "false", "0", "no" -> return !got
        }
        return display(got).equals(display(wanted), true)
    }
    fun matches(obj: LumenObject, rule: JSONObject, all: List<LumenObject>): Boolean {
        rule.optJSONArray("rules")?.let { rules ->
            if (rules.length() == 0) return true
            val results = (0 until rules.length()).map { matches(obj, rules.getJSONObject(it), all) }
            return if (rule.optString("mode", "and") == "or") results.any { it } else results.all { it }
        }
        val got = field(obj, rule.optString("field"), all)
        val wanted = rule.opt("value")
        val a = display(got); val b = display(wanted)
        return when (val op = rule.optString("op", "contains")) {
            "empty" -> blank(got)
            "not_empty", "exists" -> !blank(got)
            "equals" -> equal(got, wanted)
            "not_equals" -> !equal(got, wanted)
            "in" -> array(wanted).any { if (got is JSONArray) array(got).contains(it) else a.equals(display(it), true) }
            "starts_with" -> a.startsWith(b, true)
            "ends_with" -> a.endsWith(b, true)
            "not_contains" -> !a.contains(b, true)
            "before", "after", "lt", "gt", "lte", "gte" -> {
                val number = b.toDoubleOrNull()
                val cmp = if (got is Number && number != null) got.toDouble().compareTo(number) else a.compareTo(b, true)
                when (op) { "before", "lt" -> cmp < 0; "after", "gt" -> cmp > 0; "lte" -> cmp <= 0; else -> cmp >= 0 }
            }
            else -> a.contains(b, true) // Desktop's forward-compatible default.
        }
    }
    fun evaluate(all: List<LumenObject>, spec: JSONObject): List<LumenObject> {
        val scope = spec.optString("type_id")
        val search = spec.optString("search").trim()
        val ids = spec.optJSONArray("ids")?.let(::array)
        val filtered = all.filter { obj ->
            (scope.isBlank() || obj.typeId == scope) &&
                (spec.optBoolean("include_journals") || scope == "journal" || !obj.isJournal) &&
                (search.isEmpty() || obj.title.contains(search, true) || plain(obj).contains(search, true) ||
                    array(obj.property("aliases")).any { display(it).contains(search, true) }) &&
                (ids == null || obj.id in ids) && matches(obj, spec.optJSONObject("filter") ?: JSONObject(), all)
        }
        val sorts = array(spec.optJSONArray("sort")).filterIsInstance<JSONObject>()
        val collator = Collator.getInstance()
        val sorted = filtered.sortedWith { a, b ->
            var result = 0
            if (spec.optString("order") == "manual" && !ids.isNullOrEmpty() && sorts.isEmpty()) {
                result = ids.indexOf(a.id).compareTo(ids.indexOf(b.id))
            } else {
                val order = sorts.ifEmpty { listOf(JSONObject().put("field", "updated_at").put("direction", "desc")) }
                for (sort in order) {
                    val av = field(a, sort.optString("field"), all); val bv = field(b, sort.optString("field"), all)
                    result = if (av is Number && bv is Number) av.toDouble().compareTo(bv.toDouble())
                        else collator.compare(display(av).lowercase(), display(bv).lowercase())
                    if (result != 0) { if (sort.optString("direction") == "desc") result = -result; break }
                }
            }
            if (result == 0) a.id.compareTo(b.id) else result
        }
        return spec.optInt("limit").takeIf { it > 0 }?.let(sorted::take) ?: sorted
    }
}
