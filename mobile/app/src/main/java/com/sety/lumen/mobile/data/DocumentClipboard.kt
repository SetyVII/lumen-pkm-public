package com.sety.lumen.mobile.data

import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.jsoup.Jsoup
import org.jsoup.nodes.Element
import org.jsoup.nodes.TextNode

const val DOCUMENT_CLIPBOARD_MIME = "application/vnd.lumen.document+json"

fun selectedDocumentBlocks(document: CanonicalDocument, selected: Set<String>): JSONArray {
    val result = JSONArray()
    fun visit(nodes: JSONArray) {
        for (i in 0 until nodes.length()) {
            val node = nodes.getJSONObject(i)
            if (node.getString("id") in selected) result.put(node)
            else node.optJSONArray("children")?.let(::visit)
        }
    }
    visit(document.blocks())
    return result
}

fun removeSelectedDocumentBlocks(
    document: CanonicalDocument,
    selected: Set<String>,
): CanonicalDocument {
    fun filter(nodes: JSONArray): JSONArray =
        JSONArray().apply {
            for (i in 0 until nodes.length()) {
                val node = nodes.getJSONObject(i)
                if (node.getString("id") !in selected) {
                    node.optJSONArray("children")?.let { node.put("children", filter(it)) }
                    put(node)
                }
            }
        }
    return document.withBlocks(filter(document.blocks()))
}

fun documentClipboard(sourceObjectId: String, nodes: JSONArray, move: Boolean = false): JSONObject =
    JSONObject()
        .put("clipboard_version", 1)
        .put("source_object_id", sourceObjectId)
        .put("operation", if (move) "move" else "copy")
        .put("document", CanonicalDocument.empty().withBlocks(nodes).toJson())

/** Copy remaps structural identities and references inside the copied subtree only. */
fun pasteDocumentClipboard(
    envelope: JSONObject,
    targetObjectId: String,
    existing: CanonicalDocument? = null,
    allocateId: (String) -> String = { UUID.randomUUID().toString() },
): JSONArray {
    if (envelope.has("document")) {
        require(
            envelope.keys().asSequence().toSet() ==
                setOf("clipboard_version", "source_object_id", "operation", "document")
        ) {
            "Frame de portapapeles inválido"
        }
        val version = envelope.opt("clipboard_version")
        require(version is Number && version.toDouble() == 1.0) {
            "Versión de portapapeles incompatible"
        }
        require(
            envelope.opt("source_object_id") is String &&
                envelope.opt("operation") in setOf("copy", "move") &&
                envelope.opt("document") is JSONObject
        ) {
            "Frame de portapapeles inválido"
        }
    }
    val document = CanonicalDocument.parse(envelope.optJSONObject("document") ?: envelope)
    val source = envelope.optString("source_object_id")
    val blocks = document.blocks()
    val ids = mutableMapOf<String, String>()
    fun collect(nodes: JSONArray) {
        for (i in 0 until nodes.length()) {
            val node = nodes.getJSONObject(i)
            ids[node.getString("id")] = allocateId(node.getString("id"))
            if (node.optString("type") == "table")
                node.optJSONObject("attrs")?.optJSONArray("columns")?.let { cols ->
                    for (c in 0 until cols.length()) ids[cols.getJSONObject(c).getString("id")] =
                        allocateId(cols.getJSONObject(c).getString("id"))
                }
            node.optJSONArray("children")?.let(::collect)
        }
    }
    fun reference(value: JSONObject) {
        val next = ids[value.optString("block_id")]
        if (next != null && source.isNotBlank() && value.optString("object_id") == source)
            value.put("block_id", next).put("object_id", targetObjectId)
    }
    fun rewrite(nodes: JSONArray) {
        for (i in 0 until nodes.length()) {
            val node = nodes.getJSONObject(i)
            node.put("id", ids.getValue(node.getString("id")))
            val attrs = node.optJSONObject("attrs")
            if (node.optString("type") == "table")
                attrs?.optJSONArray("columns")?.let { cols ->
                    for (c in 0 until cols.length()) {
                        val col = cols.getJSONObject(c)
                        col.put("id", ids.getValue(col.getString("id")))
                    }
                }
            if (node.optString("type") == "object_embed" && attrs != null) reference(attrs)
            node.optJSONArray("content")?.let { content ->
                for (c in 0 until content.length()) if (
                    content.getJSONObject(c).optString("type") == "block_ref"
                )
                    reference(content.getJSONObject(c))
            }
            node.optJSONArray("children")?.let(::rewrite)
        }
    }
    collect(blocks)
    // A cut is a move only once its IDs have actually left the destination.
    // Repeated paste, or copy, mints new IDs instead of creating duplicates.
    if (
        envelope.optString("operation") == "move" &&
            existing != null &&
            ids.keys.none { existing.block(it) != null }
    ) {
        for (id in ids.keys.toList()) ids[id] = id
    }
    rewrite(blocks)
    return CanonicalDocument.empty().withBlocks(blocks).blocks()
}

/** External HTML never executes scripts or fetches resources. Unuploaded images remain links. */
fun normalizeDocumentPaste(text: String, html: String? = null): JSONArray {
    require(text.toByteArray().size + (html?.toByteArray()?.size ?: 0) <= 8 * 1024 * 1024) {
        "Portapapeles demasiado grande"
    }
    if (html.isNullOrBlank()) return CanonicalDocument.fromText(text).blocks()
    val body = Jsoup.parseBodyFragment(html).body()
    body.select("script,style,iframe,object,embed").remove()
    val pending = java.util.ArrayDeque<Pair<org.jsoup.nodes.Node, Int>>()
    pending.add(body to 0)
    var count = 0
    while (pending.isNotEmpty()) {
        val (node, depth) = pending.removeFirst()
        require(depth <= 32 && ++count <= 20_000) { "HTML demasiado complejo" }
        node.childNodes().forEach { pending.add(it to depth + 1) }
    }
    fun safeLink(value: String) =
        runCatching { java.net.URI(value).scheme in setOf("https", "http", "mailto") }
            .getOrDefault(false)
    fun inline(element: Element, marks: JSONObject = JSONObject()): JSONArray {
        val result = JSONArray()
        fun visit(node: org.jsoup.nodes.Node, style: JSONObject, href: String? = null) {
            if (node is TextNode) {
                val item =
                    JSONObject()
                        .put("type", if (href != null) "link" else "text")
                        .put("text", node.wholeText)
                        .put("marks", style)
                if (href != null) item.put("href", href)
                result.put(item)
                return
            }
            if (node !is Element) return
            val next = JSONObject(style.toString())
            when (node.normalName()) {
                "strong",
                "b" -> next.put("bold", true)
                "em",
                "i" -> next.put("italic", true)
                "u" -> next.put("underline", true)
                "s",
                "del",
                "strike" -> next.put("strike", true)
                "code" -> next.put("code", true)
                "br" -> {
                    result.put(JSONObject().put("type", "break"))
                    return
                }
                "img" -> {
                    val src = node.attr("src")
                    if (safeLink(src))
                        result.put(
                            JSONObject()
                                .put("type", "link")
                                .put("href", src)
                                .put("text", node.attr("alt").ifBlank { "Imagen externa" })
                        )
                    return
                }
            }
            val link =
                if (node.normalName() == "a" && safeLink(node.attr("href"))) node.attr("href")
                else href
            node.childNodes().forEach { visit(it, next, link) }
        }
        element.childNodes().forEach { visit(it, marks) }
        return result
    }
    fun block(element: Element): JSONArray {
        val result = JSONArray()
        val tag = element.normalName()
        val children = element.children()
        when (tag) {
            "ul",
            "ol" -> {
                element
                    .children()
                    .filter { it.normalName() == "li" }
                    .forEachIndexed { index, li ->
                        val own = li.clone()
                        own.children()
                            .filter { it.normalName() in setOf("ul", "ol") }
                            .forEach { it.remove() }
                        val item =
                            documentNode(
                                    "list_item",
                                    JSONObject()
                                        .put("kind", if (tag == "ol") "decimal" else "bullet"),
                                )
                                .put("content", inline(own))
                        if (tag == "ol" && index == 0)
                            item
                                .getJSONObject("attrs")
                                .put(
                                    "start",
                                    element.attr("start").toIntOrNull()?.coerceAtLeast(1) ?: 1,
                                )
                        val nested = JSONArray()
                        li.children()
                            .filter { it.normalName() in setOf("ul", "ol") }
                            .forEach { list ->
                                val nodes = block(list)
                                for (i in 0 until nodes.length()) nested.put(nodes.getJSONObject(i))
                            }
                        if (nested.length() > 0) item.put("children", nested)
                        result.put(item)
                    }
            }
            "table" -> {
                val rows = element.select("tr")
                val count =
                    rows.maxOfOrNull {
                        it.children().count { cell -> cell.normalName() in setOf("td", "th") }
                    } ?: 0
                if (count > 0) {
                    require(count <= 100 && rows.size.toLong() * count <= 10_000) {
                        "Tabla demasiado grande"
                    }
                    val table = documentTable(rows.size, count)
                    val target = table.getJSONArray("children")
                    rows.forEachIndexed { r, row ->
                        val cells = row.children().filter { it.normalName() in setOf("td", "th") }
                        cells.forEachIndexed { c, cell ->
                            target
                                .getJSONObject(r)
                                .getJSONArray("children")
                                .getJSONObject(c)
                                .put(
                                    "children",
                                    JSONArray()
                                        .put(documentParagraph().put("content", inline(cell))),
                                )
                        }
                        if (cells.all { it.normalName() == "th" })
                            target.getJSONObject(r).put("attrs", JSONObject().put("header", true))
                    }
                    result.put(table)
                }
            }
            "pre" ->
                result.put(documentNode("code", JSONObject().put("source", element.wholeText())))
            "hr" -> result.put(documentNode("divider"))
            "blockquote" -> {
                val inside = JSONArray()
                if (children.any { it.normalName() in setOf("p", "div", "ul", "ol") })
                    children.forEach {
                        val nodes = block(it)
                        for (i in 0 until nodes.length()) inside.put(nodes.getJSONObject(i))
                    }
                else inside.put(documentParagraph().put("content", inline(element)))
                result.put(documentNode("quote", children = inside))
            }
            "body",
            "div",
            "section",
            "article" ->
                if (
                    children.any {
                        it.normalName() in
                            setOf(
                                "p",
                                "div",
                                "ul",
                                "ol",
                                "table",
                                "blockquote",
                                "pre",
                                "hr",
                                "h1",
                                "h2",
                                "h3",
                                "h4",
                                "h5",
                                "h6",
                            )
                    }
                ) {
                    for (child in element.childNodes()) {
                        if (child is Element) {
                            val nodes = block(child)
                            for (i in 0 until nodes.length()) result.put(nodes.getJSONObject(i))
                        } else if (child is TextNode && child.text().isNotBlank())
                            result.put(documentParagraph(child.text()))
                    }
                } else result.put(documentParagraph().put("content", inline(element)))
            else -> {
                val node = documentParagraph().put("content", inline(element))
                if (tag.matches(Regex("h[1-6]")))
                    node
                        .put("type", "heading")
                        .put("attrs", JSONObject().put("level", tag.last().digitToInt()))
                result.put(node)
            }
        }
        return result
    }
    return CanonicalDocument.empty().withBlocks(block(body)).blocks()
}

/** Reparent selected sibling subtrees; moving preserves every existing ID and attribute. */
fun groupSelectedDocumentBlocks(
    document: CanonicalDocument,
    selected: Set<String>,
): CanonicalDocument {
    val roots = selectedDocumentBlocks(document, selected)
    require(roots.length() > 0) { "Selecciona uno o varios bloques" }
    val ids = (0 until roots.length()).map { roots.getJSONObject(it).getString("id") }.toSet()
    var grouped = false
    fun visit(nodes: JSONArray): JSONArray {
        val matches =
            (0 until nodes.length()).filter { nodes.getJSONObject(it).getString("id") in ids }
        if (matches.size == ids.size) {
            grouped = true
            return JSONArray().apply {
                for (i in 0 until nodes.length()) {
                    if (i == matches.first()) put(documentNode("group", children = roots))
                    if (i !in matches) put(nodes.getJSONObject(i))
                }
            }
        }
        return JSONArray().apply {
            for (i in 0 until nodes.length()) {
                val node = nodes.getJSONObject(i)
                node.optJSONArray("children")?.let { node.put("children", visit(it)) }
                put(node)
            }
        }
    }
    val result = visit(document.blocks())
    require(grouped) { "Para agrupar, selecciona bloques del mismo nivel" }
    return document.withBlocks(result)
}
