package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter

data class LumenObject(
    val id: String,
    val typeId: String,
    val title: String,
    val content: String = "",
    val properties: JSONObject = JSONObject(),
    val links: List<String> = emptyList(),
    val createdAt: String = "",
    val updatedAt: String = "",
    val revision: Long = 0,
    val syncStatus: String = "synced",
    val syncError: String = "",
    val incarnation: String = "",
    val propertyLinks: List<String> = emptyList(),
) {
    fun property(key: String): Any? = if (properties.has(key) && !properties.isNull(key)) properties.get(key) else null
    fun stringProperty(key: String): String = properties.optString(key, "")
    fun boolProperty(key: String): Boolean = properties.optBoolean(key, false)
    fun longProperty(key: String): Long = properties.optLong(key, 0L)
    val status: String get() = stringProperty("status")
    val dueDate: String get() = stringProperty("due_date")
    val assetId: String get() = stringProperty("image_asset_id").ifBlank { stringProperty("asset_id") }
        .ifBlank { properties.optJSONArray("assets")?.optJSONObject(0)?.optString("id").orEmpty() }
    val isTask: Boolean get() = typeId == "task"
    val isJournal: Boolean get() = typeId == "journal"
    val isWeb: Boolean get() = typeId == "web"
    val isImage: Boolean get() = typeId == "image"
    val isBook: Boolean get() = typeId == "book" || typeId == "libro"
    val isQuote: Boolean get() = typeId == "quote" || typeId == "cita"
    val isPage: Boolean get() = typeId == "page" || typeId == "note"
    val isDone: Boolean get() = status.equals("done", true) || status.equals("hecho", true) || status.equals("completada", true)
    val isPinned: Boolean get() = boolProperty("pinned")

    val coverUrl: String get() = stringProperty("cover_url").ifBlank { stringProperty("cover_image") }
    val author: String get() = stringProperty("author").ifBlank { stringProperty("autor") }
    val rating: String get() = stringProperty("rating").ifBlank { stringProperty("calificacion") }
    val recommendedBy: String get() = stringProperty("recommended_by").ifBlank { stringProperty("recomendado_por") }
    val mediaType: String get() = stringProperty("media_type").ifBlank { stringProperty("medio") }
    val sourceUrl: String get() = stringProperty("url").ifBlank { stringProperty("source_url") }
    val ogDescription: String get() = stringProperty("summary").ifBlank { stringProperty("description") }.ifBlank { stringProperty("og_description") }
    val siteName: String get() = stringProperty("site_name").ifBlank { stringProperty("og_site_name") }

    fun withPlainContent(text: String, objects: List<LumenObject> = emptyList()): LumenObject {
        if (text == content) return this
        // Sync merges properties: reconcile native blocks and invalidate only
        // the stale HTML snapshot, preserving untouched rich formatting.
        val props = JSONObject(properties.toString())
        val blocks = reconcileNativeDocument(this, text, objects)
        props.put("blocks", blocks)
        props.put("rich_text", JSONObject.NULL)
        val references = editorTokens(text, this, objects).map { it.objectId }.filter { it.isNotBlank() }
        val blockReferences = nativeReferenceIds(blocks)
        return copy(content = nativeDocumentText(blocks), properties = props, links = ((links - nativeReferenceIds(properties.optJSONArray("blocks"))) + references + blockReferences).distinct())
    }

    fun toJson(): JSONObject = JSONObject().apply {
        if (propertyLinks.isNotEmpty()) put("property_links", JSONArray(propertyLinks))
        put("id", id)
        put("type_id", typeId)
        put("title", title)
        put("content", content)
        put("properties", properties)
        put("links", JSONArray(links))
        put("created_at", createdAt)
        put("updated_at", updatedAt)
        put("revision", revision)
        put("sync_status", syncStatus)
        if (syncError.isNotBlank()) put("sync_error", syncError)
        if (incarnation.isNotBlank()) put("incarnation", incarnation)
    }

    fun copyWith(
        title: String = this.title,
        content: String = this.content,
        properties: JSONObject = this.properties,
        links: List<String> = this.links,
        updatedAt: String = this.updatedAt,
        revision: Long = this.revision,
        syncStatus: String = this.syncStatus,
        syncError: String = this.syncError,
    ): LumenObject = copy(
        title = title,
        content = content,
        properties = properties,
        links = links,
        updatedAt = updatedAt,
        revision = revision,
        syncStatus = syncStatus,
        syncError = syncError,
    )

    companion object {
        fun fromJson(raw: JSONObject): LumenObject {
            val links = mutableListOf<String>()
            raw.optJSONArray("links")?.let { array ->
                for (i in 0 until array.length()) links += array.optString(i)
            }
            return LumenObject(
                id = raw.optString("id"),
                typeId = raw.optString("type_id", "note"),
                title = raw.optString("title"),
                content = raw.optString("content"),
                properties = raw.optJSONObject("properties") ?: JSONObject(),
                links = links,
                createdAt = raw.optString("created_at"),
                updatedAt = raw.optString("updated_at"),
                revision = raw.optLong("revision", 0L),
                syncStatus = raw.optString("sync_status", "synced"),
                syncError = raw.optString("sync_error"),
                incarnation = raw.optString("incarnation"),
                propertyLinks = relationIds(raw.optJSONArray("property_links")),
            )
        }

        fun fromJson(text: String): LumenObject = fromJson(JSONObject(text))
    }
}

data class LumenType(
    val id: String,
    val name: String,
    val icon: String = "",
    val color: String = "#7b91ae",
    val properties: JSONArray = JSONArray(),
) {
    fun toJson(): JSONObject = JSONObject().apply {
        put("id", id)
        put("name", name)
        put("icon", icon)
        put("color", color)
        put("properties", properties)
    }

    companion object {
        fun fromJson(raw: JSONObject): LumenType = LumenType(
            id = raw.optString("id"),
            name = raw.optString("name"),
            icon = raw.optString("icon"),
            color = raw.optString("color", "#7b91ae"),
            properties = raw.optJSONArray("properties") ?: JSONArray(),
        )

        fun fromJson(text: String): LumenType = fromJson(JSONObject(text))
    }
}

data class LumenAsset(
    val id: String,
    val objectId: String = "",
    val name: String = "",
    val mime: String = "",
    val size: Long = 0,
    val createdAt: String = "",
    val localPath: String = "",
    val thumbPath: String = "",
    val pinned: Boolean = false,
) {
    val isImage: Boolean get() = mime.startsWith("image/")
    val isPdf: Boolean get() = mime == "application/pdf"

    fun toJson(): JSONObject = JSONObject().apply {
        put("id", id)
        put("object_id", objectId)
        put("name", name)
        put("mime", mime)
        put("size", size)
        put("created_at", createdAt)
        if (localPath.isNotBlank()) put("local_path", localPath)
        if (thumbPath.isNotBlank()) put("thumb_path", thumbPath)
        put("pinned", pinned)
    }

    companion object {
        fun fromJson(raw: JSONObject): LumenAsset = LumenAsset(
            id = raw.optString("id"),
            objectId = raw.optString("object_id"),
            name = raw.optString("name"),
            mime = raw.optString("mime"),
            size = raw.optLong("size"),
            createdAt = raw.optString("created_at"),
            localPath = raw.optString("local_path"),
            thumbPath = raw.optString("thumb_path"),
            pinned = raw.optBoolean("pinned"),
        )
    }
}

data class Tombstone(
    val id: String,
    val revision: Long = 0,
    val deletedAt: String = "",
)

data class OutboxOp(
    val opId: String,
    val action: String,
    val objectId: String,
    val payload: JSONObject,
    val createdAt: String,
    val attempts: Int = 0,
)

data class WidgetTask(
    val id: String,
    val title: String,
    val dueDate: String,
    val completed: Boolean,
    val syncPending: Boolean,
)

data class JournalDay(
    val date: String,
    val coverAssetId: String,
    val hasContent: Boolean,
)

data class SyncStatus(
    val serverId: String,
    val headSeq: Long,
    val pendingLogseq: Int,
    val protocol: String,
    val tlsFingerprint: String,
)

data class SyncConflict(
    val opId: String,
    val objectId: String,
    val resolution: String,
)

data class SyncOpError(
    val opId: String,
    val error: String,
)

data class SyncPullResult(
    val headSeq: Long,
    val objects: List<LumenObject>,
    val types: List<LumenType>,
    val tombstones: List<Tombstone>,
    val assets: List<LumenAsset>,
    val deleted: List<DeletedRecord>,
)

data class DeletedRecord(val kind: String, val id: String)

data class SyncPushResult(
    val headSeq: Long,
    val applied: List<String>,
    val conflicts: List<SyncConflict>,
    val errors: List<SyncOpError>,
)

data class OgPreview(
    val title: String = "",
    val description: String = "",
    val siteName: String = "",
    val imageBytes: ByteArray? = null,
    val imageMime: String = "image/webp",
    val finalUrl: String = "",
)

fun nowIso(): String = Instant.now().toString()

fun todayIso(): String = LocalDate.now().format(DateTimeFormatter.ISO_LOCAL_DATE)

fun dateFromIso(value: String): LocalDate? = runCatching { Instant.parse(value).atZone(ZoneId.systemDefault()).toLocalDate() }.getOrNull()

fun humanDate(date: LocalDate): String = date.format(DateTimeFormatter.ofPattern("EEEE d 'de' MMMM", java.util.Locale.forLanguageTag("es")))
