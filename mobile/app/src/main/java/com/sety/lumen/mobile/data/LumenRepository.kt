package com.sety.lumen.mobile.data

import android.content.Context
import android.net.Uri
import androidx.exifinterface.media.ExifInterface
import androidx.glance.appwidget.updateAll
import com.sety.lumen.mobile.net.LumenApi
import com.sety.lumen.mobile.net.SyncEngine
import com.sety.lumen.mobile.net.SyncOutcome
import com.sety.lumen.mobile.util.ImageTools
import com.sety.lumen.mobile.util.OgFetcher
import com.sety.lumen.mobile.util.UrlCleaner
import com.sety.lumen.mobile.widget.LumenWidget
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File
import java.net.URI
import java.time.LocalDate
import java.time.LocalTime
import java.time.format.DateTimeFormatter
import java.util.UUID

class LumenRepository(
    private val context: Context,
    val db: LumenDatabase,
    val vault: KeyVault,
    val settings: SettingsStore,
) {
    private val syncEngine = SyncEngine(db, vault, settings, context.filesDir)
    private val widgetScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _revision = MutableStateFlow(0L)
    val revision: StateFlow<Long> = _revision

    fun bump() {
        _revision.value = _revision.value + 1
        widgetScope.launch { runCatching { LumenWidget().updateAll(context) } }
    }

    fun objects(typeId: String? = null): List<LumenObject> = db.objects(typeId)
    fun types(): List<LumenType> = objectCatalog(db.types(), db.objects())
    fun search(query: String): List<LumenObject> = db.search(query)
    fun asset(id: String): LumenAsset? = db.asset(id)
    fun assets(): List<LumenAsset> = db.assets()
    fun widgetTasks(): List<WidgetTask> = db.widgetTasks()
    fun journalDays(prefix: String): List<JournalDay> = db.journalDays(prefix)
    fun counts(): Triple<Int, Int, Int> = db.counts()

    fun journal(date: String): LumenObject? = db.getObject("journal-$date")

    fun ensureJournal(date: String): LumenObject {
        LocalDate.parse(date)
        db.getObject("journal-$date")?.let { return it }
        val timestamp = nowIso()
        val journal = LumenObject(
            id = "journal-$date",
            typeId = "journal",
            title = date,
            content = "",
            properties = JSONObject().put("date", date),
            links = emptyList(),
            createdAt = timestamp,
            updatedAt = timestamp,
            revision = 0,
            syncStatus = "synced",
        )
        db.saveObject(journal)
        bump()
        return journal
    }

    fun saveObject(obj: LumenObject, append: Boolean = false): LumenObject {
        val updated = obj.copy(
            updatedAt = nowIso(),
            syncStatus = "pending",
            syncError = "",
        )
        if (!append) db.deleteOutboxForObject(updated.id)
        val op = putOp(updated, append)
        db.saveObject(updated, enqueue = op)
        bump()
        return updated
    }

    fun toggleTask(id: String, done: Boolean) {
        val task = db.getObject(id) ?: return
        val props = JSONObject(task.properties.toString())
        props.put("status", if (done) "done" else "todo")
        saveObject(task.copy(properties = props))
    }

    fun postponeTask(id: String) {
        val task = db.getObject(id) ?: return
        val props = JSONObject(task.properties.toString())
        props.put("due_date", LocalDate.now().plusDays(1).toString())
        saveObject(task.copy(properties = props))
    }

    fun createObject(
        typeId: String,
        title: String,
        content: String = "",
        properties: JSONObject = JSONObject(),
        links: List<String> = emptyList(),
        journalDate: String? = null,
    ): LumenObject {
        require(typeId in types().map { it.id }) { "Tipo de objeto no registrado" }
        val timestamp = nowIso()
        val initialProperties = JSONObject(properties.toString()).apply {
            if (typeId == "web" && !has("status")) put("status", webStatusForSchema(types()))
        }
        val obj = LumenObject(
            id = UUID.randomUUID().toString(),
            typeId = typeId,
            title = uniqueTitle(title.ifBlank { "Sin título" }, typeId),
            content = content,
            properties = initialProperties,
            links = links,
            createdAt = timestamp,
            updatedAt = timestamp,
            revision = 0,
            syncStatus = "pending",
        )
        db.saveObject(obj, enqueue = putOp(obj, append = false))
        if (journalDate != null) {
            appendJournal(journalDate, "- ${obj.title}", listOf(obj.id), null)
        }
        bump()
        return obj
    }

    fun appendJournal(date: String, content: String, links: List<String>, coverAssetId: String?) {
        val journal = ensureJournal(date)
        val addition = LumenObject("append", "note", "").withPlainContent(content, db.objects())
        val newBlocks = addition.properties.optJSONArray("blocks") ?: org.json.JSONArray()
        val oldBlocks = journal.properties.optJSONArray("blocks")
            ?: LumenObject("old", "note", "").withPlainContent(journal.content, db.objects()).properties.optJSONArray("blocks")
            ?: org.json.JSONArray()
        val combined = org.json.JSONArray()
        for (i in 0 until oldBlocks.length()) combined.put(JSONObject(oldBlocks.getJSONObject(i).toString()))
        for (i in 0 until newBlocks.length()) combined.put(JSONObject(newBlocks.getJSONObject(i).toString()))
        val props = JSONObject(journal.properties.toString()).put("blocks", combined).put("rich_text", JSONObject.NULL)
        if (!coverAssetId.isNullOrBlank() && props.optString("image_asset_id").isBlank()) props.put("image_asset_id", coverAssetId)
        val updated = journal.copy(content = nativeDocumentText(combined), links = (journal.links + links + nativeReferenceIds(newBlocks)).distinct(),
            properties = props, updatedAt = nowIso(), syncStatus = "pending")
        // The local row keeps the complete journal. The wire operation carries
        // only the new block/relations so the server can append without
        // duplicating everything that was already there.
        val deltaProps = JSONObject().put("blocks", newBlocks).put("rich_text", JSONObject.NULL)
        if (!coverAssetId.isNullOrBlank()) deltaProps.put("image_asset_id", coverAssetId)
        val delta = updated.copy(content = addition.content, links = (links + nativeReferenceIds(newBlocks)).distinct(), properties = deltaProps)
        db.saveObject(updated, enqueue = putOp(delta, append = true))
        bump()
    }

    suspend fun captureMoment(uri: Uri, caption: String, date: String, stripGps: Boolean, appendToJournal: Boolean = true): LumenObject =
        withContext(Dispatchers.IO) {
            val processed = ImageTools.processUri(context, uri)
            val assetId = UUID.randomUUID().toString()
            val imageId = UUID.randomUUID().toString()
            val stamp = System.currentTimeMillis()
            val ext = when {
                processed.mime.contains("png", ignoreCase = true) -> "png"
                processed.mime.contains("webp", ignoreCase = true) -> "webp"
                processed.mime.contains("gif", ignoreCase = true) -> "gif"
                else -> "jpg"
            }
            val name = "moment-$stamp.$ext"
            val assetsDir = File(context.filesDir, "assets")
            val full = File(assetsDir, name)
            val thumb = File(assetsDir, "thumb-$name")
            // EXIF needs a plaintext file, so GPS stripping runs on a
            // short-lived cache temp; the final copies are sealed at rest by
            // SecureMedia and exist only in app-private storage.
            val plain = File.createTempFile("lumen-plain", ".tmp", context.cacheDir)
            val fullBytes: ByteArray = try {
                plain.writeBytes(processed.bytes)
                if (stripGps) {
                    runCatching {
                        val exif = ExifInterface(plain.absolutePath)
                        exif.setAttribute(ExifInterface.TAG_GPS_LATITUDE, null)
                        exif.setAttribute(ExifInterface.TAG_GPS_LATITUDE_REF, null)
                        exif.setAttribute(ExifInterface.TAG_GPS_LONGITUDE, null)
                        exif.setAttribute(ExifInterface.TAG_GPS_LONGITUDE_REF, null)
                        exif.setAttribute(ExifInterface.TAG_GPS_ALTITUDE, null)
                        exif.setAttribute(ExifInterface.TAG_GPS_DATESTAMP, null)
                        exif.setAttribute(ExifInterface.TAG_GPS_TIMESTAMP, null)
                        exif.saveAttributes()
                    }
                }
                plain.readBytes()
            } finally {
                plain.delete()
            }
            SecureMedia.save(vault, full, fullBytes)
            SecureMedia.save(vault, thumb, processed.thumbBytes)
            val asset = LumenAsset(
                id = assetId,
                objectId = imageId,
                name = name,
                mime = processed.mime,
                size = fullBytes.size.toLong(),
                createdAt = nowIso(),
                localPath = full.absolutePath,
                thumbPath = thumb.absolutePath,
            )
            db.saveAsset(asset)
            val title = (caption.ifBlank { "Momento" }) + " · " + LocalTime.now().format(DateTimeFormatter.ofPattern("HH:mm"))
            val props = JSONObject().apply {
                put("mime", processed.mime)
                put("image_asset_id", assetId)
                put("asset_id", assetId)
                put("caption", caption)
                put("width", processed.width)
                put("height", processed.height)
                put("gps_stripped", stripGps)
            }
            val image = LumenObject(
                id = imageId,
                typeId = "image",
                title = uniqueTitle(title, "image"),
                content = caption,
                properties = props,
                links = emptyList(),
                createdAt = nowIso(),
                updatedAt = nowIso(),
                revision = 0,
                syncStatus = "pending",
            )
            db.saveObject(image, enqueue = putOp(image, append = false))
            db.insertOutbox(assetOp(asset, full.absolutePath))
            val block = "![${caption.ifBlank { "Foto del día" }}](lumen://object/$imageId)"
            if (appendToJournal) appendJournal(date, block, listOf(imageId), assetId)
            bump()
            image
        }

    suspend fun importFile(uri: Uri): LumenObject = withContext(Dispatchers.IO) {
        val mime = context.contentResolver.getType(uri).orEmpty().ifBlank { "application/octet-stream" }
        if (mime.startsWith("image/")) return@withContext captureMoment(uri, "Imagen", todayIso(), true, appendToJournal = false)
        val originalName = context.contentResolver.query(uri, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst()) it.getString(0) else null
        }.orEmpty().ifBlank { "Archivo" }
        val bytes = context.contentResolver.openInputStream(uri)?.use { input ->
            val output = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(8192)
            while (output.size() <= 20 * 1024 * 1024) {
                val read = input.read(buffer)
                if (read < 0) break
                output.write(buffer, 0, read)
            }
            output.toByteArray()
        }
            ?: error("No se pudo leer el archivo")
        require(bytes.size <= 20 * 1024 * 1024) { "El límite por archivo es de 20 MB" }
        val type = when { mime.startsWith("audio/") -> "audio"; mime == "application/pdf" -> "pdf"; mime.startsWith("video/") -> "video"; else -> "file" }
        val assetId = UUID.randomUUID().toString()
        val extension = originalName.substringAfterLast('.', "bin").takeIf { it.matches(Regex("[A-Za-z0-9]{1,10}")) } ?: "bin"
        val file = File(File(context.filesDir, "assets"), "$assetId.$extension")
        SecureMedia.save(vault, file, bytes)
        val asset = LumenAsset(assetId, objectId = "", name = originalName, mime = mime, size = bytes.size.toLong(), createdAt = nowIso(), localPath = file.absolutePath)
        val obj = createObject(type, originalName, properties = JSONObject().put("asset_id", assetId).put("mime", mime))
        // The object ID is generated by createObject; bind the asset to it.
        val boundAsset = asset.copy(objectId = obj.id)
        db.saveAsset(boundAsset)
        db.insertOutbox(assetOp(boundAsset, file.absolutePath))
        bump()
        obj
    }

    fun saveWebLink(
        url: String,
        title: String,
        preview: OgPreview,
        imageBytes: ByteArray?,
        journalDate: String? = null,
    ): LumenObject {
        val clean = UrlCleaner.clean(url)
        val existing = db.objects("web").firstOrNull {
            UrlCleaner.cleanOrNull(it.stringProperty("url")) == clean
        }
        if (existing != null) {
            val updated = applyWebLinkPreview(existing.id, preview, imageBytes) ?: existing
            if (journalDate != null) appendJournal(journalDate, "- ${existing.title}", listOf(existing.id), null)
            return updated
        }
        val webId = UUID.randomUUID().toString()
        val assetId = imageBytes?.let { saveWebThumbnail(webId, it) }.orEmpty()
        val finalTitle = title.ifBlank { preview.title }.ifBlank { UrlCleaner.hostOf(clean) }
        val props = JSONObject().apply {
            put("url", clean)
            put("status", webStatusForSchema(types()))
            put("source", "android-share")
            put("preview_status", if (preview.title.isNotBlank() || imageBytes != null) "ready" else "unavailable")
            if (preview.title.isNotBlank()) put("og_title", preview.title)
            if (preview.description.isNotBlank()) {
                put("og_description", preview.description)
                put("summary", preview.description)
            }
            if (preview.siteName.isNotBlank()) put("og_site_name", preview.siteName)
            if (assetId.isNotBlank()) put("image_asset_id", assetId)
        }
        val web = LumenObject(
            id = webId,
            typeId = "web",
            title = uniqueTitle(finalTitle, "web"),
            content = "",
            properties = props,
            links = emptyList(),
            createdAt = nowIso(),
            updatedAt = nowIso(),
            revision = 0,
            syncStatus = "pending",
        )
        db.saveObject(web, enqueue = putOp(web, append = false))
        if (journalDate != null) {
            appendJournal(journalDate, "- ${web.title}\n  $clean", listOf(web.id), null)
        }
        bump()
        return web
    }

    /** Enrich only preview properties; preserve notes, tags and review status. */
    fun applyWebLinkPreview(id: String, preview: OgPreview, imageBytes: ByteArray? = preview.imageBytes): LumenObject? {
        val current = db.getObject(id) ?: return null
        if (!current.isWeb) return current
        val props = JSONObject(current.properties.toString())
        if (preview.title.isNotBlank()) props.put("og_title", preview.title)
        if (preview.description.isNotBlank()) {
            props.put("og_description", preview.description)
            if (props.optString("summary").isBlank()) props.put("summary", preview.description)
        }
        if (preview.siteName.isNotBlank()) props.put("og_site_name", preview.siteName)
        if (imageBytes != null && current.assetId.isBlank()) {
            props.put("image_asset_id", saveWebThumbnail(id, imageBytes))
        }
        if (preview.title.isBlank() && imageBytes == null) return current
        props.put("preview_status", "ready")
        val hostTitle = UrlCleaner.hostOf(current.stringProperty("url"))
        val title = if (current.title == hostTitle && preview.title.isNotBlank()) uniqueTitle(preview.title, "web") else current.title
        return saveObject(current.copy(title = title, properties = props))
    }

    private fun saveWebThumbnail(webId: String, bytes: ByteArray): String {
        val assetId = UUID.randomUUID().toString()
        val file = File(File(context.filesDir, "assets"), "og-$assetId.webp")
        SecureMedia.save(vault, file, bytes)
        val asset = LumenAsset(
            id = assetId, objectId = webId, name = file.name, mime = "image/webp",
            size = bytes.size.toLong(), createdAt = nowIso(), localPath = file.absolutePath, thumbPath = file.absolutePath,
        )
        db.saveAsset(asset)
        db.insertOutbox(assetOp(asset, file.absolutePath))
        return assetId
    }

    fun quickNote(text: String, isTask: Boolean, dueDate: String?, journalDate: String?): LumenObject {
        val lines = text.trim().lines()
        val title = lines.firstOrNull().orEmpty().trim().ifBlank { if (isTask) "Tarea rápida" else "Nota rápida" }
        val content = if (lines.size > 1) lines.drop(1).joinToString("\n").trim() else ""
        val props = JSONObject()
        if (isTask) {
            props.put("status", "todo")
            if (!dueDate.isNullOrBlank()) props.put("due_date", dueDate)
        }
        return createObject(if (isTask) "task" else "note", title, content, props, emptyList(), journalDate)
    }

    fun tasks(): List<LumenObject> = db.objects("task").filter { !it.isDone }.sortedWith(
        compareBy<LumenObject> { it.dueDate.ifBlank { "9999-12-31" } }.thenBy { it.title.lowercase() }
    )

    fun webLinks(status: String? = null): List<LumenObject> = db.objects("web")
        .filter { status == null || canonicalReviewStatus(it.status) == canonicalReviewStatus(status) }
        .sortedByDescending { it.updatedAt }

    fun photosFor(date: String): List<LumenObject> {
        val journal = journal(date) ?: return emptyList()
        return journal.links.mapNotNull { db.getObject(it) }.filter { it.isImage }
    }

    fun deleteObject(id: String) {
        val opId = UUID.randomUUID().toString()
        val payload = JSONObject().apply {
            put("op_id", opId)
            put("action", "delete")
            put("object_id", id)
            put("client_time", nowIso())
        }
        db.insertOutbox(OutboxOp(opId, "delete", id, payload, nowIso()))
        db.deleteObjectLocal(id)
        bump()
    }

    suspend fun syncNow(): SyncOutcome {
        val outcome = syncEngine.syncNow()
        bump()
        return outcome
    }

    fun pair(apiUrl: String, token: String, fingerprint: String): SyncStatus {
        val uri = URI(apiUrl.trim())
        require(uri.scheme == "http" || uri.scheme == "https") { "La dirección debe empezar por http:// o https://" }
        require(uri.userInfo == null && !uri.host.isNullOrBlank()) { "Dirección de servidor no válida" }
        require(token.trim().length >= 32) { "El token debe tener al menos 32 caracteres" }
        settings.serverUrl = apiUrl.trim()
        settings.tokenEncrypted = vault.encryptToken(token.trim())
        settings.certFingerprint = fingerprint
        settings.lastSeq = 0
        settings.serverId = ""
        settings.mediaSinceDate = LocalDate.now().minusDays(90).toString()
        val status = LumenApi(settings, token.trim(), settings.serverUrl).status()
        settings.serverId = status.serverId
        bump()
        return status
    }

    fun testConnection(): SyncStatus {
        val token = vault.decryptToken()
        require(token.isNotBlank()) { "Falta el token" }
        val status = LumenApi(settings, token, settings.serverUrl).status()
        settings.serverId = status.serverId
        bump()
        return status
    }

    fun disconnect() {
        settings.tokenEncrypted = ""
        settings.serverId = ""
        settings.lastSeq = 0
        bump()
    }

    fun wipe() {
        db.wipe()
        File(context.filesDir, "assets").deleteRecursively()
        settings.clear()
        vault.lock()
        runCatching { db.close() }
        context.deleteDatabase("lumen.db")
        bump()
    }

        /**
     * Re-enqueues the whole local library so a newly paired server receives it
     * (the "resend everything" option). Operations already waiting in the
     * outbox are not duplicated. Puts carry the current revision as
     * base_revision: an up-to-date server answers with a no-op and a divergent
     * one merges both versions, so this never overwrites newer server data.
     * Assets with a local sealed copy are re-uploaded too.
     *
     * Returns (objects, assets) queued.
     */
    fun enqueueAllForResend(): Pair<Int, Int> {
        val pending = db.outbox(Int.MAX_VALUE).map { it.objectId to it.action }.toHashSet()
        var queuedObjects = 0
        var queuedAssets = 0
        db.objects().forEach { obj ->
            if ((obj.id to "put") in pending) return@forEach
            db.insertOutbox(putOp(obj, append = false))
            queuedObjects++
        }
        db.assets().forEach { asset ->
            val path = asset.localPath
            if (path.isBlank() || !File(path).exists()) return@forEach
            if ((asset.id to "asset") in pending) return@forEach
            db.insertOutbox(assetOp(asset, path))
            queuedAssets++
        }
        bump()
        return queuedObjects to queuedAssets
    }

    private fun putOp(obj: LumenObject, append: Boolean): OutboxOp {
        val opId = UUID.randomUUID().toString()
        val payload = JSONObject().apply {
            put("op_id", opId)
            put("action", "put")
            put("base_revision", obj.revision)
            put("append", append)
            put("client_time", nowIso())
            put("object", obj.toJson())
        }
        return OutboxOp(opId, "put", obj.id, payload, nowIso())
    }

    private fun assetOp(asset: LumenAsset, localPath: String): OutboxOp {
        val opId = UUID.randomUUID().toString()
        val payload = JSONObject().apply {
            put("op_id", opId)
            put("action", "asset")
            put("local_path", localPath)
            put(
                "asset",
                JSONObject().apply {
                    put("id", asset.id)
                    put("object_id", asset.objectId)
                    put("name", asset.name)
                    put("mime", asset.mime)
                    put("created_at", asset.createdAt)
                    put("data_base64", "")
                },
            )
        }
        return OutboxOp(opId, "asset", asset.objectId, payload, nowIso())
    }

    fun getObject(id: String): LumenObject? = db.getObject(id)

    fun books(): List<LumenObject> = (db.objects("book") + db.objects("libro")).distinctBy { it.id }.sortedBy { it.title.lowercase() }
    fun quotes(): List<LumenObject> = (db.objects("quote") + db.objects("cita")).distinctBy { it.id }.sortedByDescending { it.updatedAt }
    fun images(): List<LumenObject> = db.objects("image").sortedByDescending { it.updatedAt }

    fun saveBook(
        title: String,
        author: String = "",
        rating: String = "",
        recommendedBy: String = "",
        mediaType: String = "",
        coverUrl: String = "",
        content: String = "",
        journalDate: String? = null,
    ): LumenObject {
        val props = JSONObject().apply {
            if (author.isNotBlank()) put("author", author)
            if (rating.isNotBlank()) put("rating", rating)
            if (recommendedBy.isNotBlank()) put("recommended_by", recommendedBy)
            if (mediaType.isNotBlank()) put("media_type", mediaType)
            if (coverUrl.isNotBlank()) put("cover_url", coverUrl)
        }
        return createObject("book", title, content, props, emptyList(), journalDate)
    }

    fun saveQuote(
        title: String,
        author: String = "",
        source: String = "",
        content: String = "",
        journalDate: String? = null,
    ): LumenObject {
        val props = JSONObject().apply {
            if (author.isNotBlank()) put("author", author)
            if (source.isNotBlank()) put("source", source)
        }
        return createObject("quote", title, content, props, emptyList(), journalDate)
    }

    fun updateObjectContent(id: String, newContent: String): LumenObject? {
        val obj = db.getObject(id) ?: return null
        if (obj.content == newContent) return obj
        return saveObject(obj.withPlainContent(newContent, db.objects()))
    }

    fun updateObjectBlocks(id: String, blocks: org.json.JSONArray): LumenObject? {
        val obj = db.getObject(id) ?: return null
        return saveObject(obj.withNativeBlocks(blocks))
    }

    fun saveView(name: String, spec: JSONObject, queryId: String? = null): LumenObject {
        require(name.isNotBlank()) { "Escribe un nombre para la vista" }
        val existing = queryId?.let(db::getObject)
        require(existing == null || existing.typeId == "query")
        val props = JSONObject(existing?.properties?.toString() ?: "{}").put("view", JSONObject(spec.toString()))
        return if (existing != null) saveObject(existing.copy(title = name.trim(), properties = props))
            else createObject("query", name.trim(), properties = props)
    }

    fun updateObjectProperty(id: String, key: String, value: Any): LumenObject? {
        val obj = db.getObject(id) ?: return null
        val props = JSONObject(obj.properties.toString())
        props.put(key, value)
        val fields = types().firstOrNull { it.id == obj.typeId }?.let(::objectFields).orEmpty()
        val propertyLinks = fields.filter { it.kind in setOf("object", "relation") }.flatMap { relationIds(props.opt(it.key)) }.distinct()
        return saveObject(obj.copy(properties = props, propertyLinks = propertyLinks))
    }

    fun createObjectType(name: String): LumenType {
        require(name.isNotBlank()) { "Escribe un nombre para el tipo" }
        require(types().none { it.name.equals(name.trim(), true) }) { "Ya existe un tipo con ese nombre" }
        if (db.types().isEmpty()) objectCatalog(emptyList()).forEach { db.saveType(it) }
        val type = LumenType(UUID.randomUUID().toString(), name.trim(), properties = org.json.JSONArray().put(
            JSONObject().put("key", "tags").put("name", "Etiquetas").put("type", "object").put("target_type", "tag").put("multiple", true)))
        db.saveType(type)
        bump()
        return type
    }

    private fun uniqueTitle(candidate: String, typeId: String): String {
        val base = candidate.trim().ifBlank { "Sin título" }
        val existing = db.objects(typeId).map { it.title.lowercase() }.toSet()
        if (base.lowercase() !in existing) return base
        var index = 2
        while ("$base ($index)".lowercase() in existing) index++
        return "$base ($index)"
    }
}
