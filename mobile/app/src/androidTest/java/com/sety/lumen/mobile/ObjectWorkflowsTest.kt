package com.sety.lumen.mobile

import android.graphics.Bitmap
import android.os.Build
import androidx.activity.compose.setContent
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.core.content.FileProvider
import com.sety.lumen.mobile.data.*
import com.sety.lumen.mobile.ui.*
import com.sety.lumen.mobile.ui.theme.LumenTheme
import kotlinx.coroutines.runBlocking
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class ObjectWorkflowsTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()
    private lateinit var app: LumenApplication
    private lateinit var vm: LumenViewModel
    @Before fun isolatedEmulator() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        app = compose.activity.application as LumenApplication
        check(!app.settings.hasConnection()) { "Fixtures require an unpaired emulator" }
        compose.runOnUiThread {
            if (!app.vault.isSetup) app.vault.setup("isolated-test-password".toCharArray())
            check(app.vault.unlockWithDevice())
            app.settings.autoSync = false
            objectCatalog(emptyList()).forEach { app.repository.db.saveType(it) }
            vm = ViewModelProvider(compose.activity)[LumenViewModel::class.java]
        }
    }

    @Test fun reviewFiltersUseSavedSchemaValues() {
        compose.runOnUiThread {
            app.repository.db.saveType(LumenType("web", "Enlaces web", properties = JSONArray().put(JSONObject().put("key", "status").put("name", "Estado").put("type", "select").put("options", JSONArray().put("Por revisar").put("Revisado")))))
            listOf("qa-review-pending" to "Para revisar", "qa-review-read" to "Leído").forEach { (id,status) ->
                app.repository.db.saveObject(LumenObject(id,"web",id,properties=JSONObject().put("url","https://example.org/$id").put("status",status)))
            }
            vm.selectedTypeKey = "web"
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNodeWithText("Revisado").performClick()
        compose.onNodeWithText("qa-review-read").performScrollTo().performClick()
        compose.onNode(hasText("Propiedades") and isSelectable()).performClick()
        compose.onNode(hasText("Estado") and hasClickAction()).performScrollTo().performClick()
        compose.onNodeWithText("Por revisar").performClick()
        compose.onNodeWithText("Guardar").performClick()
        compose.runOnIdle { assertEquals("Por revisar", app.repository.getObject("qa-review-read")!!.status) }
        compose.onNodeWithContentDescription("Atrás").performClick()
        compose.onNodeWithText("qa-review-read").assertDoesNotExist()
        compose.onNodeWithText("Por revisar").performClick()
        compose.onNodeWithText("qa-review-read").performScrollTo().assertIsDisplayed()
        capture("review-filter.png")
    }

    @Test fun modernBookRelationsAndLegacyAuthorRemainIndependent() {
        compose.runOnUiThread {
            val schema = JSONArray().put(JSONObject().put("key","authors").put("name","Autores").put("type","object").put("target_type","person").put("multiple",true))
                .put(JSONObject().put("key","genre").put("name","Género").put("type","select").put("options",JSONArray().put("Ensayo").put("Ficción")))
                .put(JSONObject().put("key","reading_line").put("name","Línea de lectura").put("type","date"))
            app.repository.db.saveType(LumenType("qa-modern-book","Libro moderno",properties=schema))
            app.repository.db.saveObject(LumenObject("qa-person","person","Autora de prueba"))
            app.repository.db.saveObject(LumenObject("qa-legacy-book","book","Libro antiguo",properties=JSONObject().put("author","Autor como texto")))
            val book = LumenObject("qa-modern","qa-modern-book","Libro con relaciones",properties=JSONObject().put("reading_line",JSONObject().put("start","2026-11-01").put("end","2026-11-30").put("include_time",false)))
            app.repository.db.saveObject(book)
            vm.openObject(book)
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNode(hasText("Propiedades") and isSelectable()).performClick()
        compose.onNodeWithText("2026-11-01 → 2026-11-30").performScrollTo().assertIsDisplayed()
        compose.onNode(hasText("Autores") and hasClickAction()).performScrollTo().performClick()
        compose.onNodeWithText("Autora de prueba").performClick()
        compose.onNodeWithText("Guardar").performClick()
        compose.runOnIdle {
            assertEquals(listOf("qa-person"), relationIds(vm.getObject("qa-modern")!!.property("authors")))
            assertTrue("qa-person" in vm.getObject("qa-modern")!!.propertyLinks)
            assertEquals("Autor como texto", vm.getObject("qa-legacy-book")!!.author)
        }
        compose.onNodeWithText("Abrir Autora de prueba").performClick()
        compose.onNode(hasText("Gráfico") and isSelectable()).performClick()
        compose.onNodeWithText("Libro con relaciones").performScrollTo().assertIsDisplayed()
        capture("typed-relations.png")
    }

    @Test fun customSelectPropertyFiltersUseTheRegisteredSchema() {
        compose.runOnUiThread {
            val schema = JSONArray().put(JSONObject().put("key","genre").put("name","Género").put("type","select").put("options",JSONArray().put("Ensayo").put("Ficción")))
            app.repository.db.saveType(LumenType("qa-filter-type","Libros por género",properties=schema))
            app.repository.db.saveObject(LumenObject("qa-genre-book","qa-filter-type","Ensayo de prueba",properties=JSONObject().put("genre","Ensayo")))
            vm.selectedTypeKey = "qa-filter-type"
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNodeWithText("Filtrar propiedades").performClick()
        compose.onNodeWithText("Género").performClick()
        compose.onNodeWithText("Ficción").performClick()
        compose.onNodeWithText("Aplicar filtro").performClick()
        compose.onNodeWithText("Ensayo de prueba").assertDoesNotExist()
        compose.onNodeWithText("Sin coincidencias").assertIsDisplayed()
        compose.onNodeWithText("Género: Ficción").performClick()
        compose.onNodeWithText("Género").performClick()
        compose.onNodeWithText("Ensayo").performClick()
        compose.onNodeWithText("Aplicar filtro").performClick()
        compose.onNodeWithText("Ensayo de prueba").assertIsDisplayed()
        capture("schema-filter.png")
    }

    @Test fun importedAudioIsSealedAndPlaysFromMemory() {
        val samples = 16_000 * 10
        val wave = ByteBuffer.allocate(44 + samples * 2).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(36 + samples * 2); put("WAVEfmt ".toByteArray()); putInt(16)
            putShort(1); putShort(1); putInt(16000); putInt(32000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(samples * 2)
            repeat(samples) { putShort(0) }
        }.array()
        val file = File(app.cacheDir,"qa-audio.wav").apply { writeBytes(wave) }
        val uri = FileProvider.getUriForFile(app,"${app.packageName}.fileprovider",file)
        val before = app.repository.journal(todayIso())?.toJson()?.toString()
        val audio = runBlocking { app.repository.importFile(uri) }
        file.delete()
        assertEquals("audio",audio.typeId)
        val asset = app.repository.asset(audio.assetId)!!
        assertFalse(File(asset.localPath).readBytes().contentEquals(wave))
        assertTrue(SecureMedia.read(app.vault,asset.localPath)!!.contentEquals(wave))
        assertEquals(before, app.repository.journal(todayIso())?.toJson()?.toString())
        compose.runOnUiThread {
            vm.openObject(audio)
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.waitUntil(15_000) { compose.onAllNodes(hasText("Reproducir audio") and isEnabled()).fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithText("Reproducir audio").performScrollTo().performClick()
        compose.onNodeWithText("Pausar audio").assertIsDisplayed().performClick()
        compose.onNodeWithText("Reproducir audio").assertIsDisplayed()
        capture("audio-local.png")
    }

    @Test fun appendingJournalKeepsTwoSeparateNativeImages() {
        val repo = app.repository
        val date = "2098-12-01"
        val previousOps = repo.db.outbox(1000).map { it.opId }.toSet()
        repo.db.saveObject(LumenObject("journal-$date","journal","Prueba append").withPlainContent("**Original**"))
        repo.appendJournal(date,"![Primera](lumen://object/qa-image-a)",listOf("qa-image-a"),null)
        repo.appendJournal(date,"![Segunda](lumen://object/qa-image-b)",listOf("qa-image-b"),null)
        val journal = repo.journal(date)!!
        assertEquals(setOf("qa-image-a","qa-image-b"), embeddedImageIds(journal))
        assertEquals(journal.content,nativeDocumentText(journal.properties.getJSONArray("blocks")))
        assertTrue(journal.properties.getJSONArray("blocks").getJSONObject(0).getJSONArray("runs").getJSONObject(0).getBoolean("bold"))
        val outbox = repo.db.outbox(1000).filter { it.objectId == journal.id && it.opId !in previousOps }
        assertEquals(2, outbox.size)
        outbox.forEach { assertEquals(1,it.payload.getJSONObject("object").getJSONObject("properties").getJSONArray("blocks").length()) }
    }

    @Test fun savedViewsReopenAndReactToPropertyEdits() {
        compose.runOnUiThread {
            vm.objects().filter { it.typeId == "query" && it.title.startsWith("Pendientes QA") }.forEach { app.repository.deleteObject(it.id) }
            app.repository.db.saveObject(LumenObject("qa-view-pending", "web", "qa-view-pending", properties = JSONObject().put("status", "Para revisar")))
            app.repository.db.saveObject(LumenObject("qa-view-read", "web", "qa-view-read", properties = JSONObject().put("status", "Revisado")))
            vm.selectedTypeKey = "web"
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNode(hasSetTextAction() and hasText("Buscar objetos")).performTextInput("qa-view-")
        compose.onNode(hasText("Para revisar") and isSelectable()).performClick()
        compose.onNodeWithText("qa-view-read").assertDoesNotExist()
        compose.onNodeWithText("Guardar vista").performClick()
        compose.onNodeWithText("Guardar como nueva vista").performClick()
        compose.onNode(hasSetTextAction() and hasText("Nombre de la vista")).performTextInput("Pendientes QA")
        compose.onNodeWithText("Guardar").performClick()
        lateinit var view: LumenObject
        compose.runOnIdle {
            view = vm.objects().first { it.typeId == "query" && it.title == "Pendientes QA" }
            assertEquals("web", view.properties.getJSONObject("view").getString("type_id"))
            assertTrue(app.repository.db.outbox(1000).any { it.objectId == view.id })
        }
        compose.onNodeWithContentDescription("Atrás").performClick()
        compose.runOnUiThread { vm.openObject(app.repository.getObject(view.id)) }
        compose.onNodeWithText("qa-view-read").assertDoesNotExist()
        compose.onNode(hasText("Para revisar") and isSelectable()).assertIsSelected()
        compose.onNode(hasText("Revisado") and isSelectable()).performClick()
        compose.onNodeWithText("qa-view-read").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("qa-view-pending").assertDoesNotExist()
        compose.onNodeWithText("Guardar vista").performScrollTo().performClick()
        compose.onNodeWithText("Actualizar Pendientes QA").performClick()
        compose.runOnIdle {
            assertEquals(listOf("qa-view-read"), LumenQuery.evaluate(vm.objects(), app.repository.getObject(view.id)!!.properties.getJSONObject("view")).map { it.id })
        }
        compose.onNode(hasText("Para revisar") and isSelectable()).performClick()
        compose.onNodeWithText("Guardar vista").performClick()
        compose.onNodeWithText("Actualizar Pendientes QA").performClick()
        compose.onNodeWithText("qa-view-pending").performScrollTo().performClick()
        compose.onNode(hasText("Propiedades") and isSelectable()).performClick()
        compose.onNode(hasText("Estado") and hasClickAction()).performScrollTo().performClick()
        compose.onNodeWithText("Revisado").performClick()
        compose.onNodeWithText("Guardar").performClick()
        compose.onNodeWithContentDescription("Atrás").performClick()
        compose.onNodeWithText("qa-view-pending").assertDoesNotExist()
        compose.onNodeWithText("qa-view-read").assertDoesNotExist()
        compose.onNodeWithText("Sin coincidencias").assertExists()
        capture("saved-view.png")
        compose.onNodeWithText("Guardar vista").performScrollTo().performClick()
        compose.onNodeWithText("Renombrar vista").performClick()
        compose.onNode(hasSetTextAction() and hasText("Nombre de la vista")).performTextReplacement("Revisión QA")
        compose.onNodeWithText("Guardar").performClick()
        compose.runOnIdle { assertEquals("Revisión QA", app.repository.getObject(view.id)!!.title) }
        compose.onNodeWithText("Guardar vista").performClick()
        compose.onNodeWithText("Eliminar vista", substring = false).performClick()
        compose.onNodeWithText("Eliminar", substring = false).performClick()
        compose.runOnIdle {
            assertNull(app.repository.getObject(view.id))
            assertNotNull(app.repository.getObject("qa-view-pending"))
            assertNotNull(app.repository.getObject("qa-view-read"))
        }
    }

    @Test fun structuralEditsPersistWithoutChangingOtherBlocks() {
        val table = editTableCell(editTableCell(nativeTable(2, 2), 0, 0, "Cabecera QA"), 1, 0, "Celda QA")
        val columns = nativeColumns()
        columns.getJSONArray("columns").put(0, JSONArray().put(nativeParagraph("Columna QA")))
        columns.getJSONArray("columns").put(1, JSONArray().put(nativeParagraph("Otra columna QA")))
        val untouched = nativeParagraph("Texto vecino intacto").put("custom", "keep")
        val note = LumenObject("qa-structure", "note", "Edición estructural QA").withNativeBlocks(JSONArray().put(table).put(columns).put(untouched))
        compose.runOnUiThread {
            app.repository.db.saveObject(note); vm.openObject(note)
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNode(hasSetTextAction() and hasText("Celda QA")).performScrollTo().performTextReplacement("Celda editada")
        compose.onNodeWithText("Añadir fila").performScrollTo().performClick()
        compose.onNodeWithText("Añadir columna").performScrollTo().performClick()
        compose.onNode(hasSetTextAction() and hasText("Columna QA")).performScrollTo().performTextReplacement("Columna editada")
        compose.runOnIdle {
            val saved = app.repository.getObject(note.id)!!
            val blocks = saved.properties.getJSONArray("blocks")
            assertEquals(table.getString("id"), blocks.getJSONObject(0).getString("id"))
            assertEquals(3, blocks.getJSONObject(0).getJSONArray("rows").length())
            assertEquals(3, blocks.getJSONObject(0).getJSONArray("rows").getJSONArray(0).length())
            assertEquals("Columna editada", blocks.getJSONObject(1).getJSONArray("columns").getJSONArray(0).getJSONObject(0).getString("text"))
            assertEquals(untouched.toString(), blocks.getJSONObject(2).toString())
            assertTrue(saved.content.contains("Celda editada"))
            assertTrue(app.repository.db.outbox(1000).any { it.objectId == note.id && it.payload.toString().contains("Columna editada") })
        }
        compose.onNodeWithContentDescription("Atrás").performClick()
        compose.runOnUiThread { vm.openObject(app.repository.getObject(note.id)) }
        compose.onNodeWithText("Celda editada").performScrollTo()
        capture("table-reopen-before-assert.png")
        compose.onNodeWithText("Celda editada").assertIsDisplayed()
        capture("table-editing.png")
        compose.onNodeWithText("Columna editada").performScrollTo().assertIsDisplayed()
        capture("column-editing.png")
    }

    @Test fun decryptedReadsRefreshAfterWritesSyncAndVaultLock() {
        val db = app.repository.db
        val id = "qa-read-cache"
        val original = LumenObject(id, "note", "Original de prueba")
        db.saveObject(original)
        val first = db.getObject(id)
        assertSame(first, db.getObject(id))
        val list = db.objects()
        assertSame(list, db.objects())
        val pending = app.repository.saveObject(original.copy(title = "Editada de prueba"))
        assertEquals(pending.title, db.getObject(id)!!.title)
        assertNotSame(list, db.objects())
        val op = db.outbox(1000).first { it.objectId == id }
        db.confirmPush(listOf(op.opId), listOf(JSONObject(op.payload.toString()).put("object_id", id)))
        assertEquals("synced", db.getObject(id)!!.syncStatus)
        val remote = original.copy(title = "Remota de prueba")
        db.applyPull(SyncPullResult(1, listOf(remote), emptyList(), emptyList(), emptyList(), emptyList()))
        assertEquals(remote.title, db.getObject(id)!!.title)
        val asset = LumenAsset("qa-cache-asset", objectId = id, name = "Primera")
        db.saveAsset(asset); assertSame(db.asset(asset.id), db.asset(asset.id))
        db.saveAsset(asset.copy(name = "Segunda")); assertEquals("Segunda", db.asset(asset.id)!!.name)
        app.vault.lock()
        assertNull(db.getObject(id)); assertTrue(db.objects().isEmpty()); assertNull(db.asset(asset.id)); assertTrue(db.types().isEmpty())
        check(app.vault.unlockWithDevice())
        assertEquals(remote.title, db.getObject(id)!!.title)
        db.deleteObjectLocal(id); assertNull(db.getObject(id))
        db.deleteAssetLocal(asset.id); assertNull(db.asset(asset.id))
    }

    @Test fun editorBarTouchesTheKeyboardWithoutAnExtraInsetGap() {
        compose.runOnUiThread {
            val note = LumenObject("journal-2099-11-01", "journal", "2099-11-01", properties = JSONObject().put("date", "2099-11-01"))
                .withPlainContent("Texto para comprobar el teclado")
            app.repository.db.saveObject(note)
            vm.openObject(note)
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNode(hasSetTextAction() and hasText("Texto para comprobar el teclado")).performClick()
        compose.waitUntil(10_000) { compose.onAllNodesWithContentDescription("Barra de edición").fetchSemanticsNodes().isNotEmpty() }
        compose.waitForIdle()
        val bar = compose.onNodeWithContentDescription("Barra de edición").fetchSemanticsNode().boundsInWindow
        val insets = androidx.core.view.ViewCompat.getRootWindowInsets(compose.activity.window.decorView)!!
        val keyboardHeight = insets.getInsets(androidx.core.view.WindowInsetsCompat.Type.ime()).bottom
        val displayBottom = compose.activity.windowManager.currentWindowMetrics.bounds.bottom
        val density = compose.activity.resources.displayMetrics.density
        assertTrue("IME must be visible", keyboardHeight > 0)
        assertTrue("Editor toolbar must stay compact", bar.height < 72 * density)
        assertTrue("Toolbar bottom ${bar.bottom} must meet keyboard top ${displayBottom - keyboardHeight}",
            kotlin.math.abs(bar.bottom - (displayBottom - keyboardHeight)) < 16 * density)
        capture("keyboard-insets.png")
    }

    private fun capture(name: String) {
        val file = File(app.getExternalFilesDir(null),name)
        file.outputStream().use { compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG,100,it) }
    }
}
