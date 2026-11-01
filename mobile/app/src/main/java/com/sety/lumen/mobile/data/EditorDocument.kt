package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

enum class EditorTokenKind { REFERENCE, DIVIDER, LINK }

data class EditorToken(
    val start: Int,
    val end: Int,
    val kind: EditorTokenKind,
    val label: String = "",
    val objectId: String = "",
    val url: String = "",
)

/** Interpret existing wire formats without changing the editable source text. */
fun editorTokens(text: String, source: LumenObject? = null, objects: List<LumenObject> = emptyList()): List<EditorToken> {
    val catalog = objects.associateBy { it.id }
    val candidates = mutableListOf<EditorToken>()
    val codeRanges = mutableListOf<IntRange>()
    var fenceStart: Int? = null
    var offset = 0
    text.split('\n').forEach { line ->
        if (line.trimStart().startsWith("```")) {
            if (fenceStart == null) fenceStart = offset
            else { codeRanges += fenceStart until offset + line.length; fenceStart = null }
        }
        offset += line.length + 1
    }
    fenceStart?.let { codeRanges += it until text.length }
    fun inCode(start: Int, end: Int) = codeRanges.any { start <= it.last && end > it.first }
    fun resolveTitle(title: String): String {
        val matching = objects.filter { it.title == title }
        val linked = matching.filter { it.id in source?.links.orEmpty() }
        return linked.singleOrNull()?.id ?: matching.singleOrNull()?.id.orEmpty()
    }
    // Explicit IDs win over title lookups, including when titles collide.
    Regex("\\[((?:\\\\.|[^\\]\\n])*)]\\(lumen://object/([^()\\s]+)\\)").findAll(text).forEach { match ->
        val id = match.groupValues[2]
        val title = match.groupValues[1].replace(Regex("\\\\(.)"), "$1")
        candidates += EditorToken(match.range.first, match.range.last + 1, EditorTokenKind.REFERENCE,
            catalog[id]?.title ?: title, id)
    }
    Regex("\uFFFC?\\[\\[([^]\\n]+)]]").findAll(text).forEach { match ->
        val title = match.groupValues[1]
        candidates += EditorToken(match.range.first, match.range.last + 1, EditorTokenKind.REFERENCE,
            title, resolveTitle(title))
    }
    // Qt writes object IDs on runs, including U+FFFC image/reference icons.
    // Match blocks in order so repeated labels keep their original targets.
    var searchFrom = 0
    val blocks = source?.properties?.optJSONArray("blocks")
    for (i in 0 until (blocks?.length() ?: 0)) {
        val block = blocks?.optJSONObject(i) ?: continue
        if (block.optString("type") in listOf("divider", "math", "code")) continue
        val body = block.optString("text").replace('\u2028', '\n')
        if (body.isEmpty()) continue
        val start = text.indexOf(body, searchFrom)
        if (start < 0) continue
        searchFrom = start + body.length
        val runs = block.optJSONArray("runs") ?: continue
        var runOffset = 0
        for (r in 0 until runs.length()) {
            val run = runs.optJSONObject(r) ?: continue
            val runText = run.optString("text").replace('\u2028', '\n')
            val id = run.optString("object_id").ifBlank {
                run.optString("href").takeIf { it.startsWith("lumen://object/") }?.removePrefix("lumen://object/").orEmpty()
            }
            val href = run.optString("href")
            if (id.isBlank() && !run.optBoolean("code") && (href.startsWith("https://") || href.startsWith("http://")) && runText.isNotEmpty()) {
                candidates += EditorToken(start + runOffset, start + runOffset + runText.length, EditorTokenKind.LINK, runText, url = href)
            }
            if (id.isNotBlank() && runText.isNotEmpty()) {
                val label = catalog[id]?.title ?: runText.replace("\uFFFC", "").ifBlank { "Objeto enlazado" }
                candidates += EditorToken(start + runOffset, start + runOffset + runText.length,
                    EditorTokenKind.REFERENCE, label, id)
            }
            runOffset += runText.length
        }
    }
    Regex("(?m)^[ \\t]*---[ \\t]*$").findAll(text).forEach { match ->
        candidates += EditorToken(match.range.first, match.range.last + 1, EditorTokenKind.DIVIDER)
    }
    // An orphaned embed stays visible as a readable label instead of OBJ.
    text.indices.filter { text[it] == '\uFFFC' }.forEach { index ->
        candidates += EditorToken(index, index + 1, EditorTokenKind.REFERENCE, "Objeto enlazado")
    }
    val result = mutableListOf<EditorToken>()
    val resolved = candidates.map { token ->
        val native = candidates.firstOrNull { it.objectId.isNotBlank() && it.start >= token.start && it.end <= token.end }
        if (token.kind == EditorTokenKind.REFERENCE && token.objectId.isBlank() && native != null)
            token.copy(objectId = native.objectId, label = native.label) else token
    }
    resolved.sortedWith(compareBy<EditorToken> { it.start }.thenByDescending { it.end }).forEach { token ->
        if (!inCode(token.start, token.end) && token.start >= (result.lastOrNull()?.end ?: 0)) {
            val previous = result.lastOrNull()
            if (previous != null && previous.end == token.start && token.kind == EditorTokenKind.REFERENCE &&
                previous.kind == token.kind && token.objectId.isNotBlank() && previous.objectId == token.objectId) {
                result[result.lastIndex] = previous.copy(end = token.end, label = token.label)
            } else result += token
        }
    }
    return result
}

fun objectMentionText(obj: LumenObject): String {
    val title = obj.title.replace("\n", " ").replace("\\", "\\\\").replace("]", "\\]")
    return "[$title](lumen://object/${obj.id})"
}

/** The existing blocks contract is also understood by the desktop, offline. */
fun editorNativeBlocks(text: String, source: LumenObject, objects: List<LumenObject>): JSONArray? {
    val tokens = editorTokens(text, source, objects)
    if (tokens.isEmpty()) return null
    val result = JSONArray()
    val old = source.properties.optJSONArray("blocks")
    val usedIds = mutableSetOf<String>()
    val lines = text.split('\n')
    var lineStart = 0
    lines.forEachIndexed { lineIndex, line ->
        val lineEnd = lineStart + line.length
        val lineTokens = tokens.filter { it.start >= lineStart && it.end <= lineEnd }
        val divider = lineTokens.singleOrNull()?.takeIf { it.kind == EditorTokenKind.DIVIDER }
        val runs = JSONArray()
        val visible = StringBuilder()
        if (divider != null) {
            visible.append('\uFFFC')
            runs.put(JSONObject().put("text", "\uFFFC").put("type", "divider"))
        } else {
            var position = lineStart
            lineTokens.forEach { token ->
                if (position < token.start) {
                    val part = text.substring(position, token.start)
                    runs.put(JSONObject().put("text", part)); visible.append(part)
                }
                val run = JSONObject().put("text", token.label)
                if (token.objectId.isNotBlank()) {
                    run.put("href", "lumen://object/${token.objectId}").put("object_id", token.objectId)
                    objects.firstOrNull { it.id == token.objectId }?.let { run.put("type_id", it.typeId) }
                }
                runs.put(run); visible.append(token.label)
                position = token.end
            }
            if (position < lineEnd || runs.length() == 0) {
                val part = text.substring(position, lineEnd)
                runs.put(JSONObject().put("text", part)); visible.append(part)
            }
        }
        val type = if (divider != null) "divider" else "paragraph"
        fun targetIds(array: JSONArray): List<String> = (0 until array.length()).mapNotNull {
            array.optJSONObject(it)?.optString("object_id")?.takeIf(String::isNotBlank)
        }
        val same = (0 until (old?.length() ?: 0)).mapNotNull { old?.optJSONObject(it) }.firstOrNull {
            it.optString("id") !in usedIds && it.optString("text") == visible.toString() &&
                (it.optString("type") == "divider") == (divider != null) &&
                targetIds(it.optJSONArray("runs") ?: JSONArray()) == targetIds(runs)
        }
        val priorId = if (old?.length() == lines.size) old.optJSONObject(lineIndex)?.optString("id").orEmpty() else ""
        val id = same?.optString("id")?.takeIf(String::isNotBlank)
            ?: priorId.takeIf { it.isNotBlank() && it !in usedIds }
            ?: UUID.randomUUID().toString()
        usedIds += id
        result.put(same?.let { JSONObject(it.toString()).put("id", id) } ?: JSONObject().put("id", id)
            .put("type", type)
            .put("text", visible.toString()).put("runs", runs))
        lineStart = lineEnd + 1
    }
    return result
}
