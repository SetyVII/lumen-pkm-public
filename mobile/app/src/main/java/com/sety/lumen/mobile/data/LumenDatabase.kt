package com.sety.lumen.mobile.data

import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.database.sqlite.SQLiteDatabase
import android.database.sqlite.SQLiteOpenHelper
import org.json.JSONObject

class LumenDatabase(context: Context, private val vault: KeyVault, databaseName: String = "lumen-document-v2.db") :
    SQLiteOpenHelper(context.applicationContext, databaseName, null, 3) {

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
                attempts INTEGER NOT NULL DEFAULT 0,
                asset_id TEXT NOT NULL DEFAULT ''
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
        db.execSQL("CREATE INDEX outbox_asset ON outbox(asset_id, action)")
        db.execSQL("CREATE INDEX outbox_object ON outbox(object_id, action, created_at)")
        db.execSQL("CREATE TABLE blocked_outbox(object_id TEXT PRIMARY KEY, reason TEXT NOT NULL, remote_revision INTEGER NOT NULL, recovery_copy_id TEXT)")
        ensureFts(db)
    }

    override fun onUpgrade(db: SQLiteDatabase, oldVersion: Int, newVersion: Int) {
        if (oldVersion < 2) {
            db.execSQL("ALTER TABLE outbox ADD COLUMN asset_id TEXT NOT NULL DEFAULT ''")
            db.execSQL("CREATE INDEX outbox_asset ON outbox(asset_id, action)")
            db.execSQL("CREATE INDEX outbox_object ON outbox(object_id, action, created_at)")
        }
        if (oldVersion < 3) db.execSQL("CREATE TABLE blocked_outbox(object_id TEXT PRIMARY KEY, reason TEXT NOT NULL, remote_revision INTEGER NOT NULL, recovery_copy_id TEXT)")

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

    fun saveObject(value: LumenObject, seq: Long = 0, enqueue: OutboxOp? = null) {
        val db = writableDatabase
        db.beginTransaction()
        try {
            val blocked=isRemoteDeletionBlocked(value.id)
            val obj=if(blocked)value.copy(syncStatus="error",syncError="remote_deleted: Borrado remoto; borrador local conservado y bloqueado")else value
            if(blocked&&enqueue!=null)db.update("blocked_outbox",ContentValues().apply{putNull("recovery_copy_id")},"object_id=?",arrayOf(obj.id))
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
            if (enqueue != null) {
                // Replace unsent snapshots in the same transaction as the local edit.
                if (enqueue.action == "put" && !enqueue.payload.optBoolean("append"))
                    db.delete("outbox", "object_id=? AND action=? AND attempts=0", arrayOf(obj.id, "put"))
                insertOutbox(db, enqueue)
            }
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

    fun isRemoteDeletionBlocked(id:String):Boolean = readableDatabase.query("blocked_outbox",arrayOf("object_id"),"object_id=?",arrayOf(id),null,null,null,"1").use{it.moveToFirst()}

    fun blockedConflictCount():Int = readableDatabase.rawQuery("SELECT count(*) FROM blocked_outbox WHERE recovery_copy_id IS NULL",null).use{it.moveToFirst();it.getInt(0)}

    fun recoveryCopyId(id:String):String? = readableDatabase.query("blocked_outbox",arrayOf("recovery_copy_id"),"object_id=?",arrayOf(id),null,null,null,"1").use{if(it.moveToFirst()&&!it.isNull(0))it.getString(0)else null}

    private fun blockRemoteDeletion(db:SQLiteDatabase,id:String,revision:Long) {
        db.insertWithOnConflict("blocked_outbox",null,ContentValues().apply{put("object_id",id);put("reason","remote_deleted");put("remote_revision",revision)},SQLiteDatabase.CONFLICT_IGNORE)
        db.execSQL("UPDATE blocked_outbox SET remote_revision=MAX(remote_revision,?) WHERE object_id=?",arrayOf<Any>(revision,id))
        db.query("objects",arrayOf("payload"),"id=?",arrayOf(id),null,null,null,"1").use{cursor->if(cursor.moveToFirst()) {
            val obj=LumenObject.fromJson(open(cursor.getBlob(0),"object/$id")).copy(syncStatus="error",syncError="remote_deleted: Borrado remoto; borrador local conservado y bloqueado")
            db.update("objects",ContentValues().apply{put("payload",seal(obj.toJson().toString(),"object/$id"))},"id=?",arrayOf(id))
        }}
    }

    fun saveRecoveryCopy(sourceId:String,copy:LumenObject,operation:OutboxOp) {
        val db=writableDatabase;db.beginTransaction()
        try {
            check(isRemoteDeletionBlocked(sourceId)){"El objeto no tiene borrado remoto pendiente"}
            saveObject(copy,enqueue=operation)
            db.update("blocked_outbox",ContentValues().apply{put("recovery_copy_id",copy.id)},"object_id=?",arrayOf(sourceId))
            db.setTransactionSuccessful()
        } finally {db.endTransaction();reads.clear()}
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
            put("asset_id", op.payload.optJSONObject("asset")?.optString("id").orEmpty())
        }
        db.insertWithOnConflict("outbox", null, values, SQLiteDatabase.CONFLICT_REPLACE)
    }

    fun outbox(limit: Int = 100): List<OutboxOp> = readOutbox(limit = limit)

    fun pendingAssetOperation(id: String): OutboxOp? {
        // Upgrade can happen while the vault is locked. Backfill small asset
        // operations lazily after unlock, one encrypted payload at a time.
        writableDatabase.query("outbox", arrayOf("op_id", "payload"), "asset_id='' AND action=?", arrayOf("asset"), null, null, null).use { cursor ->
            while(cursor.moveToNext()) {
                val opId=cursor.getString(0)
                val assetId=JSONObject(open(cursor.getBlob(1),"outbox/$opId")).getJSONObject("asset").getString("id")
                writableDatabase.update("outbox",ContentValues().apply{put("asset_id",assetId)},"op_id=?",arrayOf(opId))
            }
        }
        return readOutbox("asset_id=? AND action=?", arrayOf(id,"asset"), 1).firstOrNull()
    }
    fun firstPendingPut(id: String): OutboxOp? = readOutbox("object_id=? AND action=?", arrayOf(id,"put"), 1).firstOrNull()

    private fun readOutbox(selection: String? = null, args: Array<String>? = null, limit: Int = 100): List<OutboxOp> {
        val out = mutableListOf<OutboxOp>()
        var bytes = 0L
        readableDatabase.query("outbox", null, "NOT EXISTS (SELECT 1 FROM blocked_outbox b WHERE b.object_id=outbox.object_id)" + (selection?.let { " AND ($it)" } ?: ""), args, null, null, "created_at,rowid", limit.toString()).use { cursor ->
            while (cursor.moveToNext()) {
                val opId = cursor.getString(cursor.getColumnIndexOrThrow("op_id"))
                val sealed = cursor.getBlob(cursor.getColumnIndexOrThrow("payload"))
                if (out.isNotEmpty() && bytes + sealed.size > 16L * 1024 * 1024) break
                bytes += sealed.size
                val payload = try { JSONObject(open(sealed, "outbox/$opId")) }
                    catch (error: Exception) { throw IllegalStateException("Operación pendiente ilegible; se conserva para recuperar: $opId", error) }
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
    fun confirmPush(applied: Collection<String>, operations: List<JSONObject>, receipts: List<SyncReceipt> = emptyList(), conflicts: List<SyncConflict> = emptyList()) {
        if (applied.isEmpty()) return
        val appliedSet = applied.toSet()
        val confirmed = receipts.associateBy { it.opId }
        val remotelyDeleted = conflicts.filter { it.resolution == "ignored_tombstoned" }.associateBy { it.opId }
        val db = writableDatabase
        db.beginTransaction()
        try {
            operations.forEach { op ->
                val opId = op.optString("op_id")
                if (opId !in appliedSet) return@forEach
                val objectId = op.optString("object_id")
                if(remotelyDeleted[opId]?.objectId==objectId && op.optString("action")=="put") {
                    blockRemoteDeletion(db,objectId,0);return@forEach
                }
                db.delete("outbox", "op_id=?", arrayOf(opId))
                if (objectId.isBlank() || op.optString("action") == "delete") return@forEach
                val receipt = confirmed[opId]?.takeIf { it.objectId == objectId && it.revision > 0 }
                // Only our own acknowledged commit may advance the base of a newer local edit.
                if (receipt != null) {
                    val pending = mutableListOf<Pair<String, JSONObject>>()
                    db.query("outbox", arrayOf("op_id", "payload"), "object_id=? AND action=? AND attempts=0", arrayOf(objectId, "put"), null, null, null).use { cursor ->
                        while (cursor.moveToNext()) {
                            val id = cursor.getString(0)
                            val payload = JSONObject(open(cursor.getBlob(1), "outbox/$id"))
                            advanceUnsentDocumentOperation(payload, 0, receipt)?.let { pending += id to it }
                        }
                    }
                    pending.forEach { (id, payload) -> db.update("outbox", ContentValues().apply {
                        put("payload", seal(payload.toString(), "outbox/$id"))
                    }, "op_id=?", arrayOf(id)) }
                }
                db.query("objects", arrayOf("payload"), "id=?", arrayOf(objectId), null, null, null, "1").use { cursor ->
                    if (!cursor.moveToFirst()) return@use
                    val obj = LumenObject.fromJson(open(cursor.getBlob(0), "object/$objectId"))
                    val pending = hasPendingOutbox(db, objectId)
                    val updated = obj.copy(revision = maxOf(obj.revision, receipt?.revision ?: obj.revision), syncStatus = if (pending) "pending" else "synced", syncError = "")
                    db.update("objects", ContentValues().apply { put("payload", seal(updated.toJson().toString(), "object/$objectId")) }, "id=?", arrayOf(objectId))
                    if (!pending) db.execSQL("UPDATE widget_tasks SET sync_pending=0 WHERE id=?", arrayOf(objectId))
                }
            }
            db.setTransactionSuccessful()
        } finally { db.endTransaction(); reads.clear() }
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

    /** Claim and read atomically, so an autosave cannot coalesce an operation between read and send. */
    fun claimOutbox(opIds: List<String>): List<OutboxOp> {
        if (opIds.isEmpty()) return emptyList()
        val db = writableDatabase
        db.beginTransaction()
        try {
            val claimed = readOutbox("op_id IN (${opIds.joinToString(",") { "?" }})", opIds.toTypedArray(), opIds.size)
            claimed.forEach { db.execSQL("UPDATE outbox SET attempts=attempts+1 WHERE op_id=?", arrayOf(it.opId)) }
            db.setTransactionSuccessful()
            return claimed.map { it.copy(attempts = it.attempts + 1) }
        } finally { db.endTransaction(); reads.clear() }
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
            val previous=db.query("sync_meta",arrayOf("value"),"key=?",arrayOf("document_v2_pull_seq"),null,null,null).use {if(it.moveToFirst())it.getString(0).toLong()else 0L}
            appliedPullCursor(previous,result)
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
                    put("seq", result.nextSeq)
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
                        put("seq", result.nextSeq)
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
                        put("seq", result.nextSeq)
                    },
                    SQLiteDatabase.CONFLICT_REPLACE,
                )
                if(hasPendingOutbox(db,tombstone.id)) { blockRemoteDeletion(db,tombstone.id,tombstone.revision);return@forEach }
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
                        put("seq", result.nextSeq)
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
                        if(hasPendingOutbox(db,deleted.id)) {blockRemoteDeletion(db,deleted.id,0);return@forEach}
                        db.delete("objects", "id=?", arrayOf(deleted.id))
                        if (ftsAvailable) db.delete("objects_fts", "id=?", arrayOf(deleted.id))
                        db.delete("widget_tasks", "id=?", arrayOf(deleted.id))
                        db.delete("outbox", "object_id=?", arrayOf(deleted.id))
                    }
                    "type" -> db.delete("types", "id=?", arrayOf(deleted.id))
                    "asset" -> if(asset(deleted.id)?.objectId?.let(::isRemoteDeletionBlocked)!=true) db.delete("assets", "id=?", arrayOf(deleted.id))
                }
            }
            db.insertWithOnConflict("sync_meta", null, ContentValues().apply {
                put("key", "document_v2_pull_seq"); put("value", result.nextSeq.toString())
            }, SQLiteDatabase.CONFLICT_REPLACE)
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
        listOf("objects", "types", "tombstones", "assets", "outbox", "blocked_outbox", "sync_meta", "widget_tasks", "journal_days").forEach {
            db.delete(it, null, null)
        }
        if (ftsAvailable) db.delete("objects_fts", null, null)
        reads.clear()
    }
}
