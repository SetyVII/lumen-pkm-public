package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.util.UUID

/** Wire document, independent of Compose. Accessors return detached JSON. */
class CanonicalDocument private constructor(private val wire: JSONObject) {
    fun toJson(): JSONObject = JSONObject(wire.toString())
    fun blocks(): JSONArray = JSONArray(wire.getJSONArray("blocks").toString())
    fun plainText(): String = documentNodesText(wire.getJSONArray("blocks"))
    /** Detached subtree for block embeds; never substitute the whole object for a missing block. */
    fun block(id: String): JSONObject? {
        fun find(nodes: JSONArray): JSONObject? {
            for (index in 0 until nodes.length()) {
                val node = nodes.getJSONObject(index)
                if (node.getString("id") == id) return JSONObject(node.toString())
                node.optJSONArray("children")?.let { children -> find(children)?.let { return it } }
            }
            return null
        }
        return find(wire.getJSONArray("blocks"))
    }
    fun semanticallyEquals(other: CanonicalDocument): Boolean = documentJsonEqual(wire, other.wire)
    fun withBlocks(blocks: JSONArray): CanonicalDocument = parse(JSONObject().put("schema_version", 2).put("blocks", blocks))
    fun references(): Set<String> = buildSet {
        fun visit(nodes: JSONArray) {
            for (i in 0 until nodes.length()) {
                val node = nodes.getJSONObject(i)
                if (node.optString("type") in setOf("image", "object_embed")) node.optJSONObject("attrs")?.optString("object_id")?.takeIf(String::isNotBlank)?.let { add(it) }
                node.optJSONArray("content")?.let { content -> for (c in 0 until content.length()) {
                    val item = content.getJSONObject(c)
                    if (item.optString("type") in setOf("object_ref", "block_ref")) add(item.getString("object_id"))
                } }
                node.optJSONArray("children")?.let(::visit)
            }
        }
        visit(wire.getJSONArray("blocks"))
    }

    companion object {
        const val VERSION = 2
        val nodeTypes = setOf("paragraph", "heading", "list_item", "quote", "group", "toggle", "code", "math", "divider", "image", "object_embed", "columns", "column", "table", "table_row", "table_cell")
        val inlineTypes = setOf("text", "break", "link", "object_ref", "block_ref", "math", "temporal")
        fun empty(): CanonicalDocument = parse(JSONObject().put("schema_version", VERSION).put("blocks", JSONArray()))
        fun fromText(text: String): CanonicalDocument = empty().withBlocks(JSONArray(text.split('\n').map { documentParagraph(it) }))
        fun parse(raw: JSONObject): CanonicalDocument {
            require(raw.toString().toByteArray(Charsets.UTF_8).size <= 8 * 1024 * 1024) { "Document exceeds 8 MiB" }
            fun keys(value: JSONObject, allowed: Set<String>) { require(value.keys().asSequence().all { it in allowed }) { "Unknown structural document field" } }
            keys(raw, setOf("schema_version", "blocks"))
            require(raw.optInt("schema_version") == VERSION) { "Unsupported document version" }
            val blocks = raw.optJSONArray("blocks") ?: error("Document blocks must be an array")
            val ids = mutableSetOf<String>()
            var count = 0
            fun validateInline(value: JSONObject) {
                keys(value, setOf("type", "text", "marks", "href", "object_id", "block_id", "label", "latex", "attrs"))
                val type = value.optString("type")
                require(type in inlineTypes) { "Unsupported inline type: $type" }
                when (type) {
                    "text", "link" -> require(value.has("text") && value.opt("text") is String) { "Text is required" }
                    "object_ref", "block_ref" -> require(value.optString("object_id").isNotBlank()) { "Reference object is required" }
                    "math" -> require(value.has("latex") && value.opt("latex") is String) { "Math source is required" }
                }
                if (type == "block_ref") require(value.optString("block_id").isNotBlank()) { "Reference block is required" }
                if (type == "link") {
                    val url = java.net.URI(value.getString("href"))
                    require(url.scheme in setOf("http", "https") && !url.rawAuthority.isNullOrBlank() || url.scheme == "mailto" && !url.schemeSpecificPart.isNullOrBlank()) { "Invalid link" }
                }
                value.optJSONObject("marks")?.let { marks ->
                    for (name in listOf("bold", "italic", "underline", "strike", "code")) if (marks.has(name)) require(marks.opt(name) is Boolean) { "Invalid mark $name" }
                    for (name in listOf("color", "highlight")) if (marks.has(name)) require(Regex("#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?").matches(marks.getString(name))) { "Invalid color" }
                    if (marks.has("point_size")) require(marks.getDouble("point_size").let { it.isFinite() && it > 0 }) { "Invalid font size" }
                }
                if (type == "temporal") {
                    val attrs = value.optJSONObject("attrs") ?: error("Temporal attributes are required")
                    val start = attrs.optString("start")
                    val end = attrs.optString("end")
                    if (attrs.optString("kind") == "date") {
                        val first = LocalDate.parse(start)
                        require(end.isBlank() || !LocalDate.parse(end).isBefore(first)) { "End precedes start" }
                    } else {
                        require(attrs.optString("kind") == "datetime") { "Invalid temporal kind" }
                        ZoneId.of(attrs.getString("timezone"))
                        val first = Instant.parse(start)
                        require(end.isBlank() || !Instant.parse(end).isBefore(first)) { "End precedes start" }
                    }
                }
            }
            fun visit(array: JSONArray, depth: Int, parent: String = "") {
                require(depth <= 32) { "Document nesting exceeds 32" }
                for (i in 0 until array.length()) {
                    require(++count <= 20_000) { "Document exceeds node limit" }
                    val node = array.optJSONObject(i) ?: error("Each node must be an object")
                    keys(node, setOf("id", "type", "attrs", "content", "children"))
                    val type = node.optString("type")
                    require(type in nodeTypes) { "Unsupported node type: $type" }
                    require(node.optString("id").isNotBlank() && ids.add(node.getString("id"))) { "Node IDs must be unique" }
                    val attrs = node.optJSONObject("attrs") ?: JSONObject()
                    node.optJSONArray("content")?.let { content ->
                        for (c in 0 until content.length()) validateInline(content.getJSONObject(c))
                    }
                    val children = node.optJSONArray("children") ?: JSONArray()
                    require(parent != "columns" || type == "column") { "Columns only contain column nodes" }
                    require(parent != "table" || type == "table_row") { "Tables only contain rows" }
                    require(parent != "table_row" || type == "table_cell") { "Rows only contain cells" }
                    require(parent != "table_cell" || type == "paragraph") { "Cells only contain paragraphs" }
                    if (type in setOf("code", "math", "divider", "image", "object_embed")) require(children.length() == 0) { "Atomic block cannot contain children" }
                    if (node.optJSONArray("content")?.length()?.let { it > 0 } == true) require(type in setOf("paragraph", "heading", "list_item", "quote", "toggle")) { "This block cannot contain inline content" }
                    when (type) {
                        "heading" -> require(attrs.optInt("level") in 1..6) { "Heading level must be 1–6" }
                        "list_item" -> require(attrs.optString("kind") in setOf("bullet", "decimal", "alpha", "roman", "task")) { "Invalid list kind" }
                        "image" -> require(attrs.optString("asset_id").isNotBlank() && children.length() == 0) { "Image requires an asset and is atomic" }
                        "object_embed" -> require(attrs.optString("object_id").isNotBlank()) { "Object target is required" }
                        "column" -> require(parent == "columns") { "Column needs a columns parent" }
                        "table_row" -> require(parent == "table") { "Row needs a table parent" }
                        "table_cell" -> require(parent == "table_row") { "Cell needs a row parent" }
                        "code", "math" -> require(attrs.opt("source") is String) { "Source is required" }
                    }
                    if (type == "columns") require(children.length() in 1..8) { "Expected 1–8 columns" }
                    if (type == "toggle") require(attrs.opt("expanded") is Boolean) { "Toggle state is required" }
                    if (type == "list_item" && attrs.optString("kind") == "task") require(attrs.opt("checked") is Boolean) { "Task state is required" }
                    if (type == "table") {
                        val cols = attrs.optJSONArray("columns") ?: error("Table columns are required")
                        require(cols.length() in 1..100 && children.length() >= 1 && cols.length() * children.length() <= 10000) { "Table size is invalid" }
                        for (c in 0 until cols.length()) {
                            val col = cols.getJSONObject(c)
                            require(col.optString("id").isNotBlank() && ids.add(col.getString("id"))) { "Duplicate column ID" }
                            require(col.getDouble("width").let { it.isFinite() && it > 0 }) { "Invalid column width" }
                        }
                        for (r in 0 until children.length()) require(children.getJSONObject(r).optJSONArray("children")?.length() == cols.length()) { "Table row width is inconsistent" }
                    }
                    for (key in listOf("width", "height")) if (attrs.has(key)) require(attrs.getDouble(key).let { it.isFinite() && it > 0 }) { "Invalid $key" }
                    visit(children, depth + 1, type)
                }
            }
            visit(blocks, 0)
            return CanonicalDocument(JSONObject(raw.toString()))
        }
    }
}

private fun documentJsonEqual(a: Any?, b: Any?): Boolean = when {
    a is JSONObject && b is JSONObject -> a.keys().asSequence().toSet().let { keys -> keys == b.keys().asSequence().toSet() && keys.all { documentJsonEqual(a.opt(it), b.opt(it)) } }
    a is JSONArray && b is JSONArray -> a.length() == b.length() && (0 until a.length()).all { documentJsonEqual(a.opt(it), b.opt(it)) }
    a is Number && b is Number -> a.toDouble() == b.toDouble()
    else -> a == b
}

fun documentNode(type: String, attrs: JSONObject = JSONObject(), children: JSONArray? = null): JSONObject = JSONObject()
    .put("id", UUID.randomUUID().toString()).put("type", type).apply {
        if (attrs.length() > 0) put("attrs", attrs)
        children?.let { put("children", it) }
    }

fun documentParagraph(text: String = ""): JSONObject = documentNode("paragraph")
    .put("content", JSONArray().put(JSONObject().put("type", "text").put("text", text)))

fun documentInlineText(content: JSONArray): String = (0 until content.length()).joinToString("") { index ->
    val run = content.getJSONObject(index)
    when (run.optString("type")) {
        "break" -> "\n"
        "object_ref", "block_ref" -> run.optString("label").ifBlank { run.optString("object_id") }
        "math" -> run.optString("latex")
        "temporal" -> run.optJSONObject("attrs")?.let { it.optString("start") + it.optString("end").takeIf(String::isNotBlank)?.let { end -> " - $end" }.orEmpty() }.orEmpty()
        else -> run.optString("text")
    }
}

fun documentNodeText(node: JSONObject): String {
    val children = node.optJSONArray("children") ?: JSONArray()
    if (node.optString("type") == "table") return (0 until children.length()).joinToString("\n") { row ->
        val cells = children.getJSONObject(row).optJSONArray("children") ?: JSONArray()
        (0 until cells.length()).joinToString("\t") { documentNodeText(cells.getJSONObject(it)) }
    }
    val attrs = node.optJSONObject("attrs") ?: JSONObject()
    val own = buildString {
        node.optJSONArray("content")?.let { append(documentInlineText(it)) }
        when (node.optString("type")) {
            "code", "math" -> append(attrs.optString("source"))
            "image" -> append(attrs.optString("caption"))
        }
    }
    return buildList {
        if (own.isNotEmpty()) add(own)
        for (i in 0 until children.length()) add(documentNodeText(children.getJSONObject(i)))
    }.joinToString("\n")
}

fun documentNodesText(blocks: JSONArray): String = (0 until blocks.length()).joinToString("\n") { documentNodeText(blocks.getJSONObject(it)) }

/** Retain unaffected inline spans and marks. Editing an atomic reference as text is rejected. */
fun editDocumentText(node: JSONObject, text: String, typingMarks: JSONObject? = null): JSONObject {
    val result = JSONObject(node.toString())
    val content = result.optJSONArray("content") ?: JSONArray()
    val before = documentInlineText(content)
    if (before == text) return result
    val prefix = before.zip(text).takeWhile { it.first == it.second }.size
    var suffix = 0
    while (suffix < before.length - prefix && suffix < text.length - prefix && before[before.lastIndex - suffix] == text[text.lastIndex - suffix]) suffix++
    val end = before.length - suffix
    val output = mutableListOf<JSONObject>()
    var offset = 0
    var inherited = JSONObject()
    var inserted = false
    fun insert() {
        if (inserted) return
        inserted = true
        val added = text.substring(prefix, text.length - suffix)
        if (added.isNotEmpty()) output += JSONObject().put("type", "text").put("text", added).apply {
            val marks = typingMarks ?: inherited
            if (marks.length() > 0) put("marks", JSONObject(marks.toString()))
        }
    }
    for (i in 0 until content.length()) {
        val run = content.getJSONObject(i)
        val runText = documentInlineText(JSONArray().put(run))
        val next = offset + runText.length
        val editable = run.optString("type") in setOf("text", "link")
        if (offset < end && next > prefix && !editable) error("Remove references using their block controls")
        if (next <= prefix) {
            output += JSONObject(run.toString())
            inherited = run.optJSONObject("marks") ?: JSONObject()
        } else {
            if (editable && prefix > offset) output += JSONObject(run.toString()).put("text", runText.take(prefix - offset))
            if (!inserted) { inherited = run.optJSONObject("marks") ?: inherited; insert() }
            if (next > end) output += if (offset >= end) JSONObject(run.toString()) else JSONObject(run.toString()).put("text", runText.drop(end - offset))
        }
        offset = next
    }
    insert()
    result.put("content", JSONArray(output))
    return result
}

fun documentTable(rows: Int = 3, columns: Int = 3): JSONObject = documentNode("table", JSONObject().put("columns", JSONArray((0 until columns).map {
    JSONObject().put("id", UUID.randomUUID().toString()).put("width", 180)
})), JSONArray((0 until rows).map { documentNode("table_row", children = JSONArray((0 until columns).map {
    documentNode("table_cell", children = JSONArray().put(documentParagraph()))
})) }))

fun documentColumns(): JSONObject = documentNode("columns", children = JSONArray((0..1).map {
    documentNode("column", JSONObject().put("width", 1), JSONArray().put(documentParagraph()))
}))

/** Selection offsets are JVM/Compose UTF-16 offsets. Atomic inline objects are not split. */
fun formatDocumentSelection(node: JSONObject, start: Int, end: Int, mark: String, value: Any): JSONObject {
    require(mark in setOf("bold", "italic", "underline", "strike", "code", "color", "highlight", "point_size"))
    val content = node.optJSONArray("content") ?: JSONArray()
    val length = documentInlineText(content).length
    require(start in 0..length && end in start..length)
    val result = JSONArray()
    var offset = 0
    for (i in 0 until content.length()) {
        val item = content.getJSONObject(i)
        val text = documentInlineText(JSONArray().put(item))
        val next = offset + text.length
        if (item.optString("type") !in setOf("text", "link") || start >= next || end <= offset) result.put(JSONObject(item.toString()))
        else {
            val a = (start - offset).coerceIn(0, text.length)
            val b = (end - offset).coerceIn(0, text.length)
            fun validBoundary(index: Int) = index == 0 || index == text.length || !(text[index - 1].isHighSurrogate() && text[index].isLowSurrogate())
            require(validBoundary(a) && validBoundary(b)) { "Selection splits a Unicode character" }
            if (a > 0) result.put(JSONObject(item.toString()).put("text", text.take(a)))
            result.put(JSONObject(item.toString()).put("text", text.substring(a, b)).apply {
                put("marks", JSONObject(item.optJSONObject("marks")?.toString() ?: "{}").put(mark, value))
            })
            if (b < text.length) result.put(JSONObject(item.toString()).put("text", text.drop(b)))
        }
        offset = next
    }
    return JSONObject(node.toString()).put("content", result)
}

fun documentListLabel(siblings: JSONArray, index: Int): String {
    val item = siblings.getJSONObject(index)
    val attrs = item.optJSONObject("attrs") ?: JSONObject()
    val kind = attrs.optString("kind")
    if (kind == "bullet") return "•"
    if (kind == "task") return ""
    var start = index
    while (start > 0) {
        val previous = siblings.getJSONObject(start - 1)
        if (siblings.getJSONObject(start).optJSONObject("attrs")?.has("start") == true || previous.optString("type") != "list_item" || previous.optJSONObject("attrs")?.optString("kind") != kind) break
        start--
    }
    val ordinal = siblings.getJSONObject(start).optJSONObject("attrs")?.optInt("start", 1)?.coerceAtLeast(1)?.plus(index - start) ?: 1
    val value = when (kind) {
        "alpha" -> buildString { var n = ordinal; while (n > 0) { n--; insert(0, ('a'.code + n % 26).toChar()); n /= 26 } }
        "roman" -> if (ordinal > 4999) ordinal.toString() else buildString {
            var n = ordinal
            for ((number, symbol) in listOf(1000 to "m", 900 to "cm", 500 to "d", 400 to "cd", 100 to "c", 90 to "xc", 50 to "l", 40 to "xl", 10 to "x", 9 to "ix", 5 to "v", 4 to "iv", 1 to "i")) while (n >= number) { append(symbol); n -= number }
        }
        else -> ordinal.toString()
    }
    return (if (attrs.optString("case") == "upper") value.uppercase(java.util.Locale.ROOT) else value) + "."
}
