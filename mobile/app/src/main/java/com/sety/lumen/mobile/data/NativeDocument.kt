package com.sety.lumen.mobile.data

import org.commonmark.node.*
import org.commonmark.parser.Parser
import org.commonmark.ext.gfm.strikethrough.Strikethrough
import org.commonmark.ext.gfm.strikethrough.StrikethroughExtension
import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

data class NativeBlockSpan(val block: JSONObject, val start: Int, val end: Int, val prefix: Int = 0, val suffix: Int = 0)

fun embeddedImageIds(source: LumenObject?): Set<String> {
    val result = mutableSetOf<String>()
    fun visit(value: Any?) {
        when (value) {
            is JSONArray -> for (i in 0 until value.length()) visit(value.opt(i))
            is JSONObject -> {
                if (value.optString("type") == "image" && value.optString("image_name").startsWith("lumen://image/")) {
                    result += value.optString("object_id").ifBlank { value.optString("image_name").removePrefix("lumen://image/") }
                }
                value.keys().forEach { key -> if (value.opt(key) is JSONArray || value.opt(key) is JSONObject) visit(value.opt(key)) }
            }
        }
    }
    visit(source?.properties?.optJSONArray("blocks"))
    return result
}

fun nativeBlockText(block: JSONObject): String {
    val text = block.optString("text").replace('\u2028', '\n')
    return when (block.optString("type")) {
        "table" -> block.optJSONArray("rows")?.let { rows -> (0 until rows.length()).joinToString("\n") { r ->
            val cells = rows.optJSONArray(r) ?: JSONArray()
            (0 until cells.length()).joinToString("\t") { c ->
                val runs = cells.optJSONObject(c)?.optJSONArray("runs") ?: JSONArray()
                (0 until runs.length()).joinToString("") { runs.getJSONObject(it).optString("text").replace('\u2028', '\n') }
            }
        } }.orEmpty()
        "columns" -> block.optJSONArray("columns")?.let { columns -> (0 until columns.length()).joinToString("\n\n") {
            nativeDocumentText(columns.optJSONArray(it) ?: JSONArray())
        } }.orEmpty()
        "toggle" -> text + (block.optJSONArray("children")?.takeIf { it.length() > 0 }?.let { "\n" + nativeDocumentText(it) } ?: "")
        "group" -> block.optJSONArray("children")?.let(::nativeDocumentText) ?: text
        "divider" -> "---"
        "code" -> "```" + block.optString("language") + "\n" + text + "\n```"
        "math" -> {
            val run = block.optJSONArray("runs")?.optJSONObject(0)
            val mark = if (run?.optBoolean("display", true) != false) "$$" else "$"
            mark + run?.optString("latex").orEmpty() + mark
        }
        else -> text
    }
}

fun nativeDocumentText(blocks: JSONArray): String = (0 until blocks.length()).joinToString("\n") {
    nativeBlockText(blocks.getJSONObject(it))
}

fun editorDocumentText(source: LumenObject?): String {
    val blocks = source?.properties?.optJSONArray("blocks")
    return if (blocks != null && blocks.length() > 0) nativeDocumentText(blocks) else source?.content.orEmpty()
}

fun nativeBlockSpans(blocks: JSONArray): List<NativeBlockSpan> {
    var offset = 0
    return (0 until blocks.length()).map { index ->
        val block = blocks.getJSONObject(index)
        val text = nativeBlockText(block)
        val prefix = if (block.optString("type") == "code") 4 + block.optString("language").length else 0
        NativeBlockSpan(block, offset, offset + text.length, prefix, if (prefix > 0) 4 else 0)
            .also { offset += text.length + 1 }
    }
}

private val markdownParser = Parser.builder().extensions(listOf(StrikethroughExtension.create())).build()

private fun inlineRuns(node: Node, inherited: JSONObject = JSONObject()): List<JSONObject> {
    val result = mutableListOf<JSONObject>()
    var current = node.firstChild
    while (current != null) {
        val style = JSONObject(inherited.toString())
        when (current) {
            is Text -> result += style.put("text", current.literal)
            is SoftLineBreak, is HardLineBreak -> result += style.put("text", "\u2028")
            is Code -> result += style.put("text", current.literal).put("code", true)
            is StrongEmphasis -> result += inlineRuns(current, style.put("bold", true))
            is Emphasis -> result += inlineRuns(current, style.put("italic", true))
            is Strikethrough -> result += inlineRuns(current, style.put("strike", true))
            is Link -> {
                style.put("href", current.destination)
                if (current.destination.startsWith("lumen://object/")) style.put("object_id", current.destination.removePrefix("lumen://object/"))
                result += inlineRuns(current, style)
            }
            is Image -> {
                val destination = current.destination
                val id = when {
                    destination.startsWith("lumen://object/") -> destination.removePrefix("lumen://object/")
                    destination.startsWith("lumen://image/") -> destination.removePrefix("lumen://image/")
                    else -> null
                }
                if (id != null) result += style.put("text", "\uFFFC").put("type", "image")
                    .put("object_id", id).put("type_id", "image").put("href", destination).put("image_name", "lumen://image/$id")
                else result += style.put("text", "![${(current.firstChild as? Text)?.literal.orEmpty()}]($destination)")
            }
            is HtmlInline -> result += style.put("text", current.literal)
            else -> result += inlineRuns(current, style)
        }
        current = current.next
    }
    return result
}

private fun parsedInlineText(text: String, style: JSONObject): List<JSONObject> {
    val leading = text.takeWhile(Char::isWhitespace)
    val trailing = text.takeLastWhile(Char::isWhitespace)
    val core = text.trim()
    if (core.isEmpty()) return listOf(JSONObject(style.toString()).put("text", text))
    val result = mutableListOf<JSONObject>()
    if (leading.isNotEmpty()) result += JSONObject(style.toString()).put("text", leading)
    result += inlineRuns(markdownParser.parse(core).firstChild ?: Document(), style)
    if (trailing.isNotEmpty()) result += JSONObject(style.toString()).put("text", trailing)
    return result
}

private fun newNativeBlock(type: String, runs: List<JSONObject>): JSONObject = JSONObject()
    .put("id", UUID.randomUUID().toString()).put("type", type)
    .put("text", runs.joinToString("") { it.optString("text") }).put("runs", JSONArray(runs))

private fun parsedBlocks(markdown: String): List<JSONObject> {
    val result = mutableListOf<JSONObject>()
    fun visit(node: Node, listType: String? = null, indent: Int = 0) {
        when (node) {
            is Heading -> result += newNativeBlock("heading", inlineRuns(node)).put("level", node.level)
            is Paragraph -> {
                var runs = inlineRuns(node)
                var kind = listType ?: "paragraph"
                val text = runs.joinToString("") { it.optString("text") }
                if (listType != null && Regex("^\\[[ xX]] ").containsMatchIn(text)) {
                    val checked = text[1] != ' '
                    runs = inlineRuns(markdownParser.parse(text.substring(4)).firstChild ?: Document())
                    runs = listOf(JSONObject().put("text", if (checked) "✔ " else "○ ")) + runs
                    kind = "todo"
                }
                val lines = mutableListOf(mutableListOf<JSONObject>())
                runs.forEach { run ->
                    run.optString("text").split('\u2028').forEachIndexed { index, part ->
                        if (index > 0) lines.add(mutableListOf())
                        if (part.isNotEmpty()) lines.last() += JSONObject(run.toString()).put("text", part)
                    }
                }
                lines.forEach { line -> result += newNativeBlock(kind, line).apply { if (listType != null) put("indent", indent.coerceAtLeast(1)) } }
            }
            is BulletList, is OrderedList -> {
                var child = node.firstChild
                while (child != null) {
                    visit(child, if (node is OrderedList) "numbered_list" else "bullet_list", indent + 1)
                    child = child.next
                }
            }
            is BlockQuote -> {
                var child = node.firstChild
                while (child != null) { visit(child, "quote", indent); child = child.next }
            }
            is FencedCodeBlock -> result += newNativeBlock("code", listOf(JSONObject().put("text", node.literal.removeSuffix("\n").replace('\n', '\u2028'))))
                .put("language", node.info.orEmpty())
            is IndentedCodeBlock -> result += newNativeBlock("code", listOf(JSONObject().put("text", node.literal.removeSuffix("\n").replace('\n', '\u2028'))))
            is ThematicBreak -> result += newNativeBlock("divider", listOf(JSONObject().put("text", "\uFFFC").put("type", "divider")))
            is HtmlBlock -> result += newNativeBlock("paragraph", listOf(JSONObject().put("text", node.literal.replace('\n', '\u2028'))))
            else -> {
                var child = node.firstChild
                while (child != null) { visit(child, listType, indent); child = child.next }
            }
        }
    }
    // The native Insert divider action is unambiguous even immediately after
    // text; do not reinterpret its --- as a Setext heading underline.
    var inCode = false
    val prepared = markdown.lines().joinToString("\n") { line ->
        if (line.trimStart().startsWith("```")) inCode = !inCode
        if (!inCode && line.trim() == "---") "\n---\n" else line
    }
    visit(markdownParser.parse(prepared))
    return result.ifEmpty { listOf(newNativeBlock("paragraph", listOf(JSONObject().put("text", "")))) }
}

internal fun editedRuns(block: JSONObject, replacement: String): JSONArray {
    val before = block.optString("text")
    var prefix = 0
    while (prefix < minOf(before.length, replacement.length) && before[prefix] == replacement[prefix]) prefix++
    var suffix = 0
    while (suffix < minOf(before.length - prefix, replacement.length - prefix) &&
        before[before.lastIndex - suffix] == replacement[replacement.lastIndex - suffix]) suffix++
    val runs = block.optJSONArray("runs") ?: JSONArray().put(JSONObject().put("text", before))
    fun slice(start: Int, end: Int): List<JSONObject> {
        var offset = 0
        return (0 until runs.length()).mapNotNull { index ->
            val run = runs.getJSONObject(index)
            val text = run.optString("text")
            val a = maxOf(start - offset, 0)
            val b = minOf(end - offset, text.length)
            offset += text.length
            if (a < b) JSONObject(run.toString()).put("text", text.substring(a, b)) else null
        }
    }
    val inserted = replacement.substring(prefix, replacement.length - suffix)
    val emptyTail = runs.optJSONObject(runs.length() - 1)?.takeIf { prefix == before.length && it.optString("text").isEmpty() }
    val inherited = emptyTail ?: slice((prefix - 1).coerceAtLeast(0), prefix.coerceAtLeast(1)).firstOrNull()
        ?: slice(prefix, (prefix + 1).coerceAtMost(before.length)).firstOrNull() ?: JSONObject()
    val middle = if (inserted.isEmpty()) emptyList() else {
        val style = JSONObject(inherited.toString()).apply {
            remove("text"); remove("type"); remove("image_name"); remove("width"); remove("height")
            remove("href"); remove("object_id"); remove("type_id"); remove("reference_icon")
        }
        if (Regex("(?:\\*\\*.+\\*\\*|_.+_|`.+`|~~.+~~|\\[.+]\\(.+\\))").containsMatchIn(inserted))
            parsedInlineText(inserted, style)
        else listOf(style.put("text", inserted))
    }
    val combined = slice(0, prefix) + middle + slice(before.length - suffix, before.length)
    val merged = mutableListOf<JSONObject>()
    combined.forEach { run ->
        fun styleOf(value: JSONObject) = JSONObject(value.toString()).apply { remove("text") }.toString()
        val prior = merged.lastOrNull()
        if (prior != null && styleOf(prior) == styleOf(run)) prior.put("text", prior.optString("text") + run.optString("text"))
        else merged += JSONObject(run.toString())
    }
    // Closing a delimiter typed one character at a time must behave like paste.
    val balanced = Regex("(?:\\*\\*[^*]+\\*\\*|__[^_]+__|(?<!\\*)\\*[^*\\n]+\\*(?!\\*)|_[^_\\n]+_|`[^`]+`|~~[^~]+~~|!?\\[[^]]+]\\([^)]+\\))")
    return JSONArray(merged.flatMap { run ->
        val text = run.optString("text")
        if (!run.optBoolean("code") && !run.has("href") && !run.has("type") && balanced.containsMatchIn(text))
            parsedInlineText(text, JSONObject(run.toString()).apply { remove("text") }) +
                if (run === merged.last() && Regex("(?:\\*\\*|__|_|\\*|`|~~|\\))$").containsMatchIn(text)) listOf(JSONObject(run.toString()).put("text", "")) else emptyList()
        else listOf(run)
    })
}

/** Edit only the affected native blocks; untouched metadata is copied verbatim. */
fun reconcileNativeDocument(source: LumenObject, proposed: String, objects: List<LumenObject>): JSONArray {
    val old = source.properties.optJSONArray("blocks")
    if (old == null || old.length() == 0) {
        // Legacy text-only documents still use the common Markdown parser.
        return JSONArray(parsedBlocks(proposed))
    }
    if (nativeDocumentText(old) != source.content) return reconcileNativeDocument(source.copy(content = nativeDocumentText(old)), proposed, objects)
    var start = 0
    while (start < minOf(source.content.length, proposed.length) && source.content[start] == proposed[start]) start++
    var suffix = 0
    while (suffix < minOf(source.content.length - start, proposed.length - start) &&
        source.content[source.content.lastIndex - suffix] == proposed[proposed.lastIndex - suffix]) suffix++
    val end = source.content.length - suffix
    val spans = nativeBlockSpans(old)
    var first = spans.indexOfFirst { it.end >= start }.coerceAtLeast(0)
    var last = spans.indexOfLast { it.start <= end }.coerceAtLeast(first)
    val delta = proposed.length - source.content.length
    if (start == end && delta > 0 && spans[first].start == start && proposed.substring(start, start + delta).endsWith('\n')) {
        val prior = spans.getOrNull(first - 1)
        if (prior?.block?.optString("type") in listOf("bullet_list", "numbered_list", "alphabetic_list", "todo")) {
            first--; last = first
        } else {
            val result = JSONArray()
            for (i in 0 until first) result.put(JSONObject(old.getJSONObject(i).toString()))
            parsedBlocks(proposed.substring(start, start + delta).removeSuffix("\n")).forEach(result::put)
            for (i in first until old.length()) result.put(JSONObject(old.getJSONObject(i).toString()))
            return result
        }
    }
    val a = spans[first]
    val z = spans[last]
    val chunk = proposed.substring(a.start, (z.end + proposed.length - source.content.length).coerceIn(a.start, proposed.length))
    val original = a.block
    val type = original.optString("type")
    val replacement: List<JSONObject>
    val structural = Regex("^(?:#{1,4} |[-*+] |\\d+[.)] |> |```|---$)").containsMatchIn(chunk)
    if (first == last && !chunk.contains('\n') && type !in listOf("divider", "math", "table", "columns", "group") && !structural) {
        val updated = JSONObject(original.toString())
        val runs = editedRuns(original, chunk)
        updated.put("runs", runs).put("text", (0 until runs.length()).joinToString("") { runs.getJSONObject(it).optString("text") })
        replacement = listOf(updated)
    } else if (first == last && type == "code" && chunk.startsWith("```")) {
        replacement = parsedBlocks(chunk).map { JSONObject(original.toString()).put("text", it.optString("text")).put("runs", it.optJSONArray("runs")).put("language", it.optString("language")) }
    } else {
        // Enter in a list keeps the list type on each newly created item.
        replacement = if (first == last && type in listOf("paragraph", "bullet_list", "numbered_list", "alphabetic_list", "todo", "heading", "quote", "small") && !structural) {
            chunk.split('\n').mapIndexed { index, line ->
                val runs = editedRuns(original, line)
                JSONObject(original.toString()).put("id", if (index == 0) original.optString("id") else UUID.randomUUID().toString())
                    .put("text", (0 until runs.length()).joinToString("") { runs.getJSONObject(it).optString("text") }).put("runs", runs)
            }
        } else parsedBlocks(chunk).mapIndexed { index, block ->
            block.apply { if (index == 0 && type == optString("type")) put("id", original.optString("id")) }
        }
    }
    val result = JSONArray()
    for (i in 0 until first) result.put(JSONObject(old.getJSONObject(i).toString()))
    replacement.forEach(result::put)
    for (i in last + 1 until old.length()) result.put(JSONObject(old.getJSONObject(i).toString()))
    // Old title-only mentions are resolved only when unambiguous; new ones
    // already carry an explicit ID. Keep the existing converter for those runs.
    for (i in 0 until result.length()) {
        val block = result.getJSONObject(i)
        if (block.optString("text").contains("[[")) {
            val converted = editorNativeBlocks(block.optString("text"), source.copy(properties = JSONObject()), objects)
            converted?.optJSONObject(0)?.let { block.put("runs", it.optJSONArray("runs")).put("text", it.optString("text")) }
        }
    }
    return result
}
