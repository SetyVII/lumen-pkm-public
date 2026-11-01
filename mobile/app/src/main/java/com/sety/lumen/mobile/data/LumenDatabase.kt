package com.sety.lumen.mobile.data

import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import org.json.JSONObject

class LumenDatabase(context: Context, private val vault: KeyVault) :
    SQLiteOpenHelper(context.applicationContext, "lumen.db", null, 1) {

    private var ftsAvailable = true
    private val reads = MemoryReadCache()
    init { vault.onLock(reads::clear) }

    private fun <T> cached(key: String, load: () -> T): T {
        // Locked calls preserve the existing null/empty read behaviour and
        // never return a decrypted result left over from an unlocked session.
        if (!vault.isUnlocked) { reads.clear(); return load() }
        return reads.read(key, load)
    }

    override fun onConfigure(db: SQLiteDatabase) {
        super.onConfigure(db)
        db.enableWriteAheadLogging()
        db.setForeignKeyConstraintsEnabled(true)
    }

    override fun onCreate(db: SQLiteDatabase) {
        db.execSQL(
            """CREATE TABLE objects(
                id TEXT PRIMARY KEY,
                type_id TEXT NOT NULL,
                payload BLOB NOT NULL,
                seq INTEGER NOT NULL DEFAULT 0,
                updated_at TEXT NOT NULL
            )""".trimIndent()
        )
        db.execSQL("CREATE INDEX objects_type ON objects(type_id)")
        db.execSQL("CREATE INDEX objects_updated ON objects(updated_at)")
        db.execSQL("CREATE TABLE types(id TEXT PRIMARY KEY, payload BLOB NOT NULL, seq INTEGER NOT NULL DEFAULT 0)")
        db.execSQL("CREATE TABLE tombstones(id TEXT PRIMARY KEY, payload BLOB NOT NULL, seq INTEGER NOT NULL DEFAULT 0)")
        db.execSQL(
            """CREATE TABLE assets(
                id TEXT PRIMARY KEY,
                object_id TEXT,
                mime TEXT NOT NULL,
                size INTEGER NOT NULL,
                created_at TEXT NOT NULL,
                seq INTEGER NOT NULL DEFAULT 0,
                local_path TEXT NOT NULL DEFAULT '',
                thumb_path TEXT NOT NULL DEFAULT '',
                pinned INTEGER NOT NULL DEFAULT 0,
                payload BLOB NOT NULL
            )""".trimIndent()
        )
        db.execSQL("CREATE INDEX assets_object ON assets(object_id)")
        db.execSQL(
            """CREATE TABLE outbox(
                op_id TEXT PRIMARY KEY,
                action TEXT NOT NULL,
                object_id TEXT,
                payload BLOB NOT NULL,
                created_at TEXT NOT NULL,
                attempts INTEGER NOT NULL DEFAULT 0
            )""".trimIndent()
        )
        db.execSQL("CREATE TABLE sync_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL)")
        db.execSQL(
            """CREATE TABLE widget_tasks(
                id TEXT PRIMARY KEY,
                payload BLOB NOT NULL,
                due_date TEXT NOT NULL DEFAULT '',
                completed INTEGER NOT NULL DEFAULT 0,
                sync_pending INTEGER NOT NULL DEFAULT 0
            )""".trimIndent()
        )
        db.execSQL(
            """CREATE TABLE journal_days(
                date TEXT PRIMARY KEY,
                cover_asset_id TEXT NOT NULL DEFAULT '',
                has_content INTEGER NOT NULL DEFAULT 0
            )""".trimIndent()
        )
        ensureFts(db)
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        // Version 1 is the first mobile schema. Future migrations belong here.
    }

    override fun onOpen(db: SQLiteDatabase) {
        super.onOpen(db)
        ensureFts(db)
    }

    private fun ensureFts(db: SQLiteDatabase) {
        if (!ftsAvailable) return
        ftsAvailable = runCatching {
            db.execSQL("CREATE VIRTUAL TABLE IF NOT EXISTS objects_fts USING fts5(id UNINDEXED,title,content,type_id UNINDEXED)")
            true
        }.getOrDefault(false)
    }

    private fun key(): ByteArray = vault.requireMaster()

    private fun seal(value: String, aad: String): ByteArray =
        CryptoBox.encrypt(key(), value.toByteArray(Charsets.UTF_8), aad)

    private fun open(payload: ByteArray, aad: String): String =
        String(CryptoBox.decrypt(key(), payload, aad), Charsets.UTF_8)

    fun saveObject(obj: LumenObject, seq: Long = 0, enqueue: OutboxOp? = null) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            val values = ContentValues().apply {
                put("id", obj.id)
                put("type_id", obj.typeId)
                put("payload", seal(obj.toJson().toString(), "object/${obj.id}"))
                put("seq", seq)
                put("updated_at", obj.updatedAt.ifBlank { nowIso() })
            }
            db.insertWithOnConflict("objects", null, values, SQLiteDatabase.CONFLICT_REPLACE)
            updateFts(db, obj)
            updateWidgetProjection(db, obj)
            updateJournalDay(db, obj)
            if (enqueue != null) insertOutbox(db, enqueue)
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    fun saveObjects(objects: List<LumenObject>, seq: Long = 0) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            objects.forEach { obj ->
                val values = ContentValues().apply {
                    put("id", obj.id)
                    put("type_id", obj.typeId)
                    put("payload", seal(obj.toJson().toString(), "object/${obj.id}"))
                    put("seq", seq)
                    put("updated_at", obj.updatedAt.ifBlank { nowIso() })
                }
                db.insertWithOnConflict("objects", null, values, SQLiteDatabase.CONFLICT_REPLACE)
                updateFts(db, obj)
                updateWidgetProjection(db, obj)
                updateJournalDay(db, obj)
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    fun getObject(id: String): LumenObject? = cached("object/$id") {
        readableDatabase.query("objects", arrayOf("payload"), "id=?", arrayOf(id), null, null, null).use { cursor ->
            if (!cursor.moveToFirst()) return@cached null
            return@cached runCatching { LumenObject.fromJson(open(cursor.getBlob(0), "object/$id")) }.getOrNull()
        }
    }

    fun objects(typeId: String? = null, limit: Int = 1000): List<LumenObject> = cached("objects/${typeId.orEmpty()}/$limit") {
        val where = if (typeId == null) null else "type_id=?"
        val args = if (typeId == null) null else arrayOf(typeId)
        val out = mutableListOf<LumenObject>()
        readableDatabase.query("objects", arrayOf("id", "payload"), where, args, null, null, "updated_at DESC", limit.toString()).use { cursor ->
            while (cursor.moveToNext()) {
                val id = cursor.getString(0)
                runCatching { LumenObject.fromJson(open(cursor.getBlob(1), "object/$id")) }.getOrNull()?.let(out::add)
            }
        }
        out
    }

    fun deleteObjectLocal(id: String) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            db.delete("objects", "id=?", arrayOf(id))
            if (ftsAvailable) db.delete("objects_fts", "id=?", arrayOf(id))
            db.delete("widget_tasks", "id=?", arrayOf(id))
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    private fun updateFts(db: SQLiteDatabase, obj: LumenObject) {
        if (!ftsAvailable) return
        runCatching {
            db.delete("objects_fts", "id=?", arrayOf(obj.id))
            db.execSQL(
                "INSERT INTO objects_fts(id,title,content,type_id) VALUES(?,?,?,?)",
                arrayOf(obj.id, obj.title, obj.content, obj.typeId),
            )
        }
    }

    fun search(query: String, limit: Int = 100): List<LumenObject> {
        val q = query.trim()
        if (q.isBlank()) return emptyList()
        if (ftsAvailable) {
            val match = q.split(Regex("\\s+")).joinToString(" ") { token ->
                "\"" + token.replace("\"", "\"\"") + "\"*"
            }
            val ids = mutableListOf<String>()
            runCatching {
                readableDatabase.rawQuery("SELECT id FROM objects_fts WHERE objects_fts MATCH ? LIMIT ?", arrayOf(match, limit.toString())).use { cursor ->
                    while (cursor.moveToNext()) ids += cursor.getString(0)
                }
            }
            if (ids.isNotEmpty()) {
                return ids.mapNotNull(::getObject)
            }
        }
        val lowered = q.lowercase()
        return objects(limit = 2000).filter {
            it.title.lowercase().contains(lowered) || it.content.lowercase().contains(lowered)
        }.take(limit)
    }

    fun saveType(type: LumenType, seq: Long = 0) {
        val values = ContentValues().apply {
            put("id", type.id)
            put("payload", seal(type.toJson().toString(), "type/${type.id}"))
            put("seq", seq)
        }
        writableDatabase.insertWithOnConflict("types", null, values, SQLiteDatabase.CONFLICT_REPLACE)
        reads.clear()
    }

    fun types(): List<LumenType> = cached("types") {
        val out = mutableListOf<LumenType>()
        readableDatabase.query("types", arrayOf("id", "payload"), null, null, null, null, "id").use { cursor ->
            while (cursor.moveToNext()) {
                val id = cursor.getString(0)
                runCatching { LumenType.fromJson(open(cursor.getBlob(1), "type/$id")) }.getOrNull()?.let(out::add)
            }
        }
        out
    }

    fun deleteType(id: String) {
        writableDatabase.delete("types", "id=?", arrayOf(id))
        reads.clear()
    }

    fun saveTombstone(tombstone: Tombstone, seq: Long = 0) {
        val json = JSONObject().apply {
            put("id", tombstone.id)
            put("revision", tombstone.revision)
            put("deleted_at", tombstone.deletedAt)
        }.toString()
        val values = ContentValues().apply {
            put("id", tombstone.id)
            put("payload", seal(json, "tombstone/${tombstone.id}"))
            put("seq", seq)
        }
        writableDatabase.insertWithOnConflict("tombstones", null, values, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun isTombstoned(id: String): Boolean =
        readableDatabase.query("tombstones", arrayOf("id"), "id=?", arrayOf(id), null, null, null).use { it.moveToFirst() }

    fun saveAsset(asset: LumenAsset, seq: Long = 0) {
        val values = ContentValues().apply {
            put("id", asset.id)
            put("object_id", asset.objectId)
            put("mime", asset.mime)
            put("size", asset.size)
            put("created_at", asset.createdAt)
            put("seq", seq)
            put("local_path", asset.localPath)
            put("thumb_path", asset.thumbPath)
            put("pinned", if (asset.pinned) 1 else 0)
            put("payload", seal(asset.toJson().toString(), "asset/${asset.id}"))
        }
        writableDatabase.insertWithOnConflict("assets", null, values, SQLiteDatabase.CONFLICT_REPLACE)
        reads.clear()
    }

    fun asset(id: String): LumenAsset? = cached("asset/$id") {
        readableDatabase.query("assets", arrayOf("payload"), "id=?", arrayOf(id), null, null, null).use { cursor ->
            if (!cursor.moveToFirst()) return@cached null
            return@cached runCatching { LumenAsset.fromJson(JSONObject(open(cursor.getBlob(0), "asset/$id"))) }.getOrNull()
        }
    }

    fun assets(): List<LumenAsset> = cached("assets") {
        val out = mutableListOf<LumenAsset>()
        readableDatabase.query("assets", arrayOf("id", "payload"), null, null, null, null, "created_at DESC").use { cursor ->
            while (cursor.moveToNext()) {
                val id = cursor.getString(0)
                runCatching { LumenAsset.fromJson(JSONObject(open(cursor.getBlob(1), "asset/$id"))) }.getOrNull()?.let(out::add)
            }
        }
        out
    }

    fun deleteAssetLocal(id: String) {
        writableDatabase.delete("assets", "id=?", arrayOf(id))
        reads.clear()
    }

    fun setAssetPath(id: String, path: String) {
        val asset = asset(id) ?: return
        saveAsset(asset.copy(localPath = path))
    }

    fun setAssetPaths(id: String, localPath: String, thumbPath: String) {
        val asset = asset(id) ?: return
        saveAsset(asset.copy(localPath = localPath, thumbPath = thumbPath))
    }

    fun setAssetPinned(id: String, pinned: Boolean) {
        val asset = asset(id) ?: return
        saveAsset(asset.copy(pinned = pinned))
    }

    fun deleteOutboxForObject(objectId: String, action: String = "put") {
        writableDatabase.delete("outbox", "object_id=? AND action=?", arrayOf(objectId, action))
    }

    fun insertOutbox(op: OutboxOp) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            insertOutbox(db, op)
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    private fun insertOutbox(db: SQLiteDatabase, op: OutboxOp) {
        val values = ContentValues().apply {
            put("op_id", op.opId)
            put("action", op.action)
            put("object_id", op.objectId)
            put("payload", seal(op.payload.toString(), "outbox/${op.opId}"))
            put("created_at", op.createdAt)
            put("attempts", op.attempts)
        }
        db.insertWithOnConflict("outbox", null, values, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun outbox(limit: Int = 100): List<OutboxOp> {
        val out = mutableListOf<OutboxOp>()
        readableDatabase.query("outbox", null, null, null, null, null, "created_at", limit.toString()).use { cursor ->
            while (cursor.moveToNext()) {
                val opId = cursor.getString(cursor.getColumnIndexOrThrow("op_id"))
                val payload = runCatching { JSONObject(open(cursor.getBlob(cursor.getColumnIndexOrThrow("payload")), "outbox/$opId")) }
                    .onFailure { android.util.Log.w("LumenSync", "Operación de outbox ilegible, se omite: $opId", it) }
                    .getOrNull() ?: continue
                out += OutboxOp(
                    opId = opId,
                    action = cursor.getString(cursor.getColumnIndexOrThrow("action")),
                    objectId = cursor.getString(cursor.getColumnIndexOrThrow("object_id")) ?: "",
                    payload = payload,
                    createdAt = cursor.getString(cursor.getColumnIndexOrThrow("created_at")),
                    attempts = cursor.getInt(cursor.getColumnIndexOrThrow("attempts")),
                )
            }
        }
        return out
    }

    fun deleteOutbox(opIds: Collection<String>) {
        if (opIds.isEmpty()) return
        val db = writableDatabase
        db.beginTransaction()
        try {
            opIds.forEach { db.delete("outbox", "op_id=?", arrayOf(it)) }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    private fun hasPendingOutbox(db: SQLiteDatabase, objectId: String): Boolean =
        db.query("outbox", arrayOf("op_id"), "object_id=?", arrayOf(objectId), null, null, null, "1").use {
            it.moveToFirst()
        }

    // A successful push is the mobile-side acknowledgement. Do not wait for a
    // later pull: the server no longer exports to Logseq and does not own this
    // local pending marker.
    fun confirmPush(applied: Collection<String>, operations: List<JSONObject>) {
        if (applied.isEmpty()) return
        val appliedSet = applied.toSet()
        val db = writableDatabase
        db.beginTransaction()
        try {
            operations.forEach { op ->
                val opId = op.optString("op_id")
                if (opId !in appliedSet) return@forEach
                val objectId = op.optString("object_id")
                db.delete("outbox", "op_id=?", arrayOf(opId))
                if (objectId.isBlank() || op.optString("action") == "delete") return@forEach
                db.query("objects", arrayOf("payload"), "id=?", arrayOf(objectId), null, null, null, "1").use { cursor ->
                    if (!cursor.moveToFirst()) return@use
                    val obj = runCatching { LumenObject.fromJson(open(cursor.getBlob(0), "object/$objectId")) }.getOrNull() ?: return@use
                    val synced = obj.copy(syncStatus = "synced", syncError = "")
                    db.update(
                        "objects",
                        ContentValues().apply { put("payload", seal(synced.toJson().toString(), "object/$objectId")) },
                        "id=?",
                        arrayOf(objectId),
                    )
                    db.execSQL("UPDATE widget_tasks SET sync_pending=0 WHERE id=?", arrayOf(objectId))
                }
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    fun markOutboxAttempt(opIds: Collection<String>) {
        if (opIds.isEmpty()) return
        val db = writableDatabase
        db.beginTransaction()
        try {
            opIds.forEach { db.execSQL("UPDATE outbox SET attempts=attempts+1 WHERE op_id=?", arrayOf(it)) }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    fun meta(key: String): String? =
        readableDatabase.query("sync_meta", arrayOf("value"), "key=?", arrayOf(key), null, null, null).use {
            if (it.moveToFirst()) it.getString(0) else null
        }

    fun setMeta(key: String, value: String) {
        writableDatabase.insertWithOnConflict(
            "sync_meta",
            null,
            ContentValues().apply {
                put("key", key)
                put("value", value)
            },
            SQLiteDatabase.CONFLICT_REPLACE,
        )
    }

    private fun updateWidgetProjection(db: SQLiteDatabase, obj: LumenObject) {
        if (!obj.isTask) {
            db.delete("widget_tasks", "id=?", arrayOf(obj.id))
            return
        }
        val titlePayload = CryptoBox.encrypt(vault.deviceKey(), obj.title.toByteArray(Charsets.UTF_8), "widget/${obj.id}")
        val values = ContentValues().apply {
            put("id", obj.id)
            put("payload", titlePayload)
            put("due_date", obj.dueDate)
            put("completed", if (obj.isDone) 1 else 0)
            put("sync_pending", if (obj.syncStatus == "pending") 1 else 0)
        }
        db.insertWithOnConflict("widget_tasks", null, values, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun widgetTasks(): List<WidgetTask> {
        val out = mutableListOf<WidgetTask>()
        readableDatabase.query("widget_tasks", null, null, null, null, null, "completed ASC, due_date ASC, id ASC").use { cursor ->
            while (cursor.moveToNext()) {
                val id = cursor.getString(cursor.getColumnIndexOrThrow("id"))
                val title = runCatching {
                    String(CryptoBox.decrypt(vault.deviceKey(), cursor.getBlob(cursor.getColumnIndexOrThrow("payload")), "widget/$id"), Charsets.UTF_8)
                }.getOrDefault("Tarea")
                out += WidgetTask(
                    id = id,
                    title = title,
                    dueDate = cursor.getString(cursor.getColumnIndexOrThrow("due_date")) ?: "",
                    completed = cursor.getInt(cursor.getColumnIndexOrThrow("completed")) != 0,
                    syncPending = cursor.getInt(cursor.getColumnIndexOrThrow("sync_pending")) != 0,
                )
            }
        }
        return out
    }

    private fun updateJournalDay(db: SQLiteDatabase, obj: LumenObject) {
        if (!obj.isJournal) return
        val date = obj.title.takeIf { it.matches(Regex("\\d{4}-\\d{2}-\\d{2}")) }
            ?: obj.stringProperty("date").takeIf { it.matches(Regex("\\d{4}-\\d{2}-\\d{2}")) }
            ?: return
        val hasContent = obj.content.isNotBlank() || obj.links.isNotEmpty()
        val cover = obj.assetId
        if (cover.isBlank()) {
            db.execSQL(
                "INSERT INTO journal_days(date,cover_asset_id,has_content) VALUES(?, '', ?) ON CONFLICT(date) DO UPDATE SET has_content=excluded.has_content",
                arrayOf<Any>(date, if (hasContent) 1 else 0),
            )
        } else {
            db.execSQL(
                "INSERT INTO journal_days(date,cover_asset_id,has_content) VALUES(?,?,?) ON CONFLICT(date) DO UPDATE SET cover_asset_id=excluded.cover_asset_id,has_content=excluded.has_content",
                arrayOf<Any>(date, cover, if (hasContent) 1 else 0),
            )
        }
    }

    fun journalDay(date: String): JournalDay? =
        readableDatabase.query("journal_days", null, "date=?", arrayOf(date), null, null, null).use { cursor ->
            if (!cursor.moveToFirst()) return null
            JournalDay(
                date = cursor.getString(cursor.getColumnIndexOrThrow("date")),
                coverAssetId = cursor.getString(cursor.getColumnIndexOrThrow("cover_asset_id")) ?: "",
                hasContent = cursor.getInt(cursor.getColumnIndexOrThrow("has_content")) != 0,
            )
        }

    fun journalDays(prefix: String): List<JournalDay> {
        val out = mutableListOf<JournalDay>()
        readableDatabase.query("journal_days", null, "date LIKE ?", arrayOf("$prefix%"), null, null, "date").use { cursor ->
            while (cursor.moveToNext()) {
                out += JournalDay(
                    date = cursor.getString(cursor.getColumnIndexOrThrow("date")),
                    coverAssetId = cursor.getString(cursor.getColumnIndexOrThrow("cover_asset_id")) ?: "",
                    hasContent = cursor.getInt(cursor.getColumnIndexOrThrow("has_content")) != 0,
                )
            }
        }
        return out
    }

    fun applyPull(result: SyncPullResult) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            result.objects.forEach { raw ->
                // Never overwrite a local object that still has an unconfirmed
                // outbox operation. The next push will reconcile it first.
                if (hasPendingOutbox(db, raw.id)) return@forEach
                // The server no longer has a Logseq exporter; its state is the
                // durable local store and therefore already synced.
                val obj = raw.copy(syncStatus = if (raw.syncStatus == "error") "error" else "synced")
                val values = ContentValues().apply {
                    put("id", obj.id)
                    put("type_id", obj.typeId)
                    put("payload", seal(obj.toJson().toString(), "object/${obj.id}"))
                    put("seq", result.headSeq)
                    put("updated_at", obj.updatedAt.ifBlank { nowIso() })
                }
                db.insertWithOnConflict("objects", null, values, SQLiteDatabase.CONFLICT_REPLACE)
                updateFts(db, obj)
                updateWidgetProjection(db, obj)
                updateJournalDay(db, obj)
            }
            result.types.forEach { type ->
                db.insertWithOnConflict(
                    "types",
                    null,
                    ContentValues().apply {
                        put("id", type.id)
                        put("payload", seal(type.toJson().toString(), "type/${type.id}"))
                        put("seq", result.headSeq)
                    },
                    SQLiteDatabase.CONFLICT_REPLACE,
                )
            }
            result.tombstones.forEach { tombstone ->
                val json = JSONObject().apply {
                    put("id", tombstone.id)
                    put("revision", tombstone.revision)
                    put("deleted_at", tombstone.deletedAt)
                }.toString()
                db.insertWithOnConflict(
                    "tombstones",
                    null,
                    ContentValues().apply {
                        put("id", tombstone.id)
                        put("payload", seal(json, "tombstone/${tombstone.id}"))
                        put("seq", result.headSeq)
                    },
                    SQLiteDatabase.CONFLICT_REPLACE,
                )
                db.delete("objects", "id=?", arrayOf(tombstone.id))
                if (ftsAvailable) db.delete("objects_fts", "id=?", arrayOf(tombstone.id))
                db.delete("widget_tasks", "id=?", arrayOf(tombstone.id))
                db.delete("outbox", "object_id=?", arrayOf(tombstone.id))
            }
            result.assets.forEach { asset ->
                val existing = asset(asset.id)
                val merged = asset.copy(
                    localPath = existing?.localPath ?: asset.localPath,
                    thumbPath = existing?.thumbPath ?: asset.thumbPath,
                    pinned = existing?.pinned ?: asset.pinned,
                )
                db.insertWithOnConflict(
                    "assets",
                    null,
                    ContentValues().apply {
                        put("id", merged.id)
                        put("object_id", merged.objectId)
                        put("mime", merged.mime)
                        put("size", merged.size)
                        put("created_at", merged.createdAt)
                        put("seq", result.headSeq)
                        put("local_path", merged.localPath)
                        put("thumb_path", merged.thumbPath)
                        put("pinned", if (merged.pinned) 1 else 0)
                        put("payload", seal(merged.toJson().toString(), "asset/${merged.id}"))
                    },
                    SQLiteDatabase.CONFLICT_REPLACE,
                )
            }
            result.deleted.forEach { deleted ->
                when (deleted.kind) {
                    "object" -> {
                        db.delete("objects", "id=?", arrayOf(deleted.id))
                        if (ftsAvailable) db.delete("objects_fts", "id=?", arrayOf(deleted.id))
                        db.delete("widget_tasks", "id=?", arrayOf(deleted.id))
                        db.delete("outbox", "object_id=?", arrayOf(deleted.id))
                    }
                    "type" -> db.delete("types", "id=?", arrayOf(deleted.id))
                    "asset" -> db.delete("assets", "id=?", arrayOf(deleted.id))
                }
            }
            db.setTransactionSuccessful()
        } finally {
            db.endTransaction()
            reads.clear()
        }
    }

    fun counts(): Triple<Int, Int, Int> {
        fun count(table: String): Int = readableDatabase.rawQuery("SELECT count(*) FROM $table", null).use {
            if (it.moveToFirst()) it.getInt(0) else 0
        }
        return Triple(count("objects"), count("outbox"), count("assets"))
    }

    fun wipe() {
        val db = writableDatabase
        listOf("objects", "types", "tombstones", "assets", "outbox", "sync_meta", "widget_tasks", "journal_days").forEach {
            db.delete(it, null, null)
        }
        if (ftsAvailable) db.delete("objects_fts", null, null)
        reads.clear()
    }
}
