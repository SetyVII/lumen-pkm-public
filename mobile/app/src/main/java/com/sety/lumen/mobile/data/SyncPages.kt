package com.sety.lumen.mobile.data

import org.json.JSONObject

fun decodeSyncPullPage(json: JSONObject): SyncPullResult {
    fun items(key: String): List<JSONObject> {
        if (!json.has(key) || json.isNull(key)) return emptyList()
        val list = json.getJSONArray(key)
        return (0 until list.length()).map { list.getJSONObject(it) }
    }
    val head = json.getLong("head_seq")
    return SyncPullResult(
        headSeq = head,
        objects = items("objects").map(LumenObject::fromJson),
        types = items("types").map(LumenType::fromJson),
        tombstones =
            items("tombstones").map {
                Tombstone(it.getString("id"), it.getLong("revision"), it.getString("deleted_at"))
            },
        assets = items("assets").map(LumenAsset::fromJson),
        deleted = items("deleted").map { DeletedRecord(it.getString("kind"), it.getString("id")) },
        nextSeq = if (json.has("next_seq")) json.getLong("next_seq") else head,
        hasMore = json.optBoolean("has_more", false),
    )
}

fun appliedPullCursor(previous: Long, page: SyncPullResult): Long {
    require(page.nextSeq >= previous && page.nextSeq <= page.headSeq) {
        "Cursor de página inválido"
    }
    require(!page.hasMore || page.nextSeq > previous) {
        "El servidor no avanzó la página de sincronización"
    }
    return page.nextSeq
}

data class SyncPullProgress(val cursor: Long, val hasMore: Boolean, val pages: Int)

/**
 * Advance only after the page's durable apply succeeds; interruption resumes at that exact cursor.
 */
fun consumeSyncPullPages(
    initialCursor: Long,
    maxPages: Int = 100,
    fetch: (Long) -> SyncPullResult,
    apply: (SyncPullResult) -> Unit,
): SyncPullProgress {
    require(maxPages > 0) { "Límite de páginas inválido" }
    var cursor = initialCursor
    repeat(maxPages) { index ->
        val page = fetch(cursor)
        val next = appliedPullCursor(cursor, page)
        apply(page)
        cursor = next
        if (!page.hasMore) return SyncPullProgress(cursor, false, index + 1)
    }
    return SyncPullProgress(cursor, true, maxPages)
}
