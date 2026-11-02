package com.sety.lumen.mobile

import android.os.Build
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import com.sety.lumen.mobile.data.*
import java.util.UUID
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test

/** Uses a uniquely named disposable DB; never opens the personal library DB. */
class DocumentOutboxPersistenceTest {
    @Test
    fun remoteDeletionKeepsEncryptedDraftBlocksDispatchAndAllowsExplicitNoteCopy() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        val app = ApplicationProvider.getApplicationContext<LumenApplication>()
        check(!app.settings.hasConnection()) { "Requires isolated unpaired emulator" }
        if (!app.vault.isSetup) app.vault.setup("isolated-test-password".toCharArray())
        check(app.vault.unlockWithDevice())
        val name = "qa-remote-delete-${UUID.randomUUID()}.db"
        var db = LumenDatabase(app, app.vault, name)
        val source =
            LumenObject("offline-draft", "note", "Offline draft", revision = 1)
                .withDocument(CanonicalDocument.fromText("Saved offline draft"))
        val operation =
            OutboxOp(
                "offline-edit",
                "put",
                source.id,
                JSONObject()
                    .put("op_id", "offline-edit")
                    .put("action", "put")
                    .put("base_revision", 1)
                    .put("object", source.toJson()),
                nowIso(),
            )
        try {
            db.saveObject(source, enqueue = operation)
            val sealed =
                db.readableDatabase
                    .rawQuery("SELECT payload FROM outbox WHERE op_id='offline-edit'", null)
                    .use {
                        it.moveToFirst()
                        it.getBlob(0)
                    }
            db.confirmPush(listOf(operation.opId),listOf(JSONObject(operation.payload.toString()).put("object_id",source.id)),conflicts=listOf(SyncConflict(operation.opId,source.id,"ignored_tombstoned")))
            assertEquals(1,db.blockedConflictCount())
            db.applyPull(
                SyncPullResult(
                    1,
                    emptyList(),
                    emptyList(),
                    listOf(Tombstone(source.id, 2, nowIso())),
                    emptyList(),
                    emptyList(),
                )
            )
            assertEquals(1, db.blockedConflictCount())
            assertTrue(db.outbox().isEmpty())
            assertEquals("Saved offline draft", db.getObject(source.id)!!.document.plainText())
            assertEquals("error", db.getObject(source.id)!!.syncStatus)
            val retained =
                db.readableDatabase
                    .rawQuery("SELECT payload FROM outbox WHERE op_id='offline-edit'", null)
                    .use {
                        it.moveToFirst()
                        it.getBlob(0)
                    }
            assertArrayEquals(sealed, retained)
            db.close()
            db = LumenDatabase(app, app.vault, name)
            assertTrue(db.isRemoteDeletionBlocked(source.id))
            assertTrue(db.claimOutbox(listOf("offline-edit")).isEmpty())
            db.applyPull(
                SyncPullResult(
                    2,
                    emptyList(),
                    emptyList(),
                    emptyList(),
                    emptyList(),
                    listOf(DeletedRecord("object", source.id)),
                )
            )
            assertNotNull(db.getObject(source.id))
            assertEquals(1, db.blockedConflictCount())
            val repository = LumenRepository(app, db, app.vault, app.settings)
            val recovered = repository.recoverRemoteDeletedDraftAsNoteCopy(source.id)
            assertNotEquals(source.id, recovered.id)
            assertEquals("note", recovered.typeId)
            assertEquals(source.document.plainText(), recovered.document.plainText())
            assertEquals(0, db.blockedConflictCount())
            assertTrue(db.isRemoteDeletionBlocked(source.id))
            assertEquals(recovered.id, db.outbox().single().objectId)
            assertEquals(recovered.id, repository.recoverRemoteDeletedDraftAsNoteCopy(source.id).id)
        } finally {
            db.close()
            app.deleteDatabase(name)
        }
    }

    @Test
    fun paginatedPullKeepsDurablePageCursorThroughDisconnectAndRollback() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        val app = ApplicationProvider.getApplicationContext<LumenApplication>()
        check(!app.settings.hasConnection()) { "Requires isolated unpaired emulator" }
        if (!app.vault.isSetup) app.vault.setup("isolated-test-password".toCharArray())
        check(app.vault.unlockWithDevice())
        val name = "qa-document-pages-${UUID.randomUUID()}.db"
        var db = LumenDatabase(app, app.vault, name)
        val wire =
            JSONObject(
                    InstrumentationRegistry.getInstrumentation()
                        .context
                        .assets
                        .open("sync-v2/pull-pages.json")
                        .bufferedReader()
                        .use { it.readText() }
                )
                .getJSONArray("pages")
        val first = decodeSyncPullPage(wire.getJSONObject(0))
        val second = decodeSyncPullPage(wire.getJSONObject(1))
        try {
            assertTrue(
                runCatching {
                        consumeSyncPullPages(
                            0,
                            fetch = { cursor ->
                                if (cursor == 0L) first
                                else throw java.io.IOException("Disconnected after committed page")
                            },
                            apply = db::applyPull,
                        )
                    }
                    .isFailure
            )
            assertEquals("128", db.meta("document_v2_pull_seq"))
            assertEquals("First page", db.getObject("page-note-a")!!.document.plainText())
            db.close()
            db = LumenDatabase(app, app.vault, name)
            assertEquals("128", db.meta("document_v2_pull_seq"))
            assertNull(db.getObject("page-note-b"))
            db.writableDatabase.execSQL(
                "CREATE TEMP TRIGGER fail_page_cursor BEFORE INSERT ON sync_meta WHEN NEW.key='document_v2_pull_seq' BEGIN SELECT RAISE(ABORT,'injected'); END"
            )
            assertTrue(runCatching { db.applyPull(second) }.isFailure)
            assertEquals("128", db.meta("document_v2_pull_seq"))
            assertNull(db.getObject("page-note-b"))
            db.writableDatabase.execSQL("DROP TRIGGER fail_page_cursor")
            val progress =
                consumeSyncPullPages(
                    db.meta("document_v2_pull_seq")!!.toLong(),
                    fetch = { cursor ->
                        assertEquals(128, cursor)
                        second
                    },
                    apply = db::applyPull,
                )
            assertFalse(progress.hasMore)
            assertEquals("302", db.meta("document_v2_pull_seq"))
            assertEquals("Concurrent update", db.getObject("page-note-a")!!.document.plainText())
            assertNotNull(db.getObject("page-note-b"))
        } finally {
            db.close()
            app.deleteDatabase(name)
        }
    }

    @Test
    fun attemptedEditSurvivesNextSaveRestartAndCompactAcknowledgement() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        val app = ApplicationProvider.getApplicationContext<LumenApplication>()
        check(!app.settings.hasConnection()) { "Requires isolated unpaired emulator" }
        if (!app.vault.isSetup) app.vault.setup("isolated-test-password".toCharArray())
        check(app.vault.unlockWithDevice())
        val name = "qa-document-outbox-${UUID.randomUUID()}.db"
        var db = LumenDatabase(app, app.vault, name)
        fun op(id: String, obj: LumenObject) =
            OutboxOp(
                id,
                "put",
                obj.id,
                JSONObject()
                    .put("op_id", id)
                    .put("action", "put")
                    .put("base_revision", obj.revision)
                    .put("object", obj.toJson()),
                nowIso(),
            )
        try {
            val first =
                LumenObject("object", "note", "Note", revision = 1)
                    .withDocument(CanonicalDocument.fromText("First"))
            val sent = op("first-sent", first)
            db.saveObject(first, enqueue = sent)
            assertEquals(1, db.claimOutbox(listOf(sent.opId)).single().attempts)
            val second = first.withDocument(CanonicalDocument.fromText("Second"))
            val next = op("second-unsent", second)
            db.saveObject(second, enqueue = next)
            assertEquals(2, db.outbox().size)
            db.confirmPush(
                listOf(sent.opId),
                listOf(JSONObject(sent.payload.toString()).put("object_id", "object")),
                listOf(SyncReceipt(sent.opId, "object", 2, 1)),
            )
            var pending = db.outbox().single()
            assertEquals(2, pending.payload.getLong("base_revision"))
            assertEquals(0, pending.attempts)
            assertEquals(1, db.claimOutbox(listOf(next.opId)).single().attempts)
            val immutable = db.outbox().single().payload.toString()
            db.close()
            db = LumenDatabase(app, app.vault, name)
            pending = db.outbox().single()
            assertEquals(immutable, pending.payload.toString())
            assertEquals(1, pending.attempts)
            assertEquals("Second", db.getObject("object")!!.document.plainText())
        } finally {
            db.close()
            app.deleteDatabase(name)
        }
    }
}
