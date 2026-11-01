package com.sety.lumen.mobile

import android.graphics.Bitmap
import android.os.Build
import androidx.activity.compose.setContent
import androidx.compose.ui.test.*
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.lifecycle.ViewModelProvider
import androidx.test.platform.app.InstrumentationRegistry
import com.sety.lumen.mobile.data.*
import com.sety.lumen.mobile.ui.AppRoot
import com.sety.lumen.mobile.ui.LumenViewModel
import com.sety.lumen.mobile.ui.theme.LumenTheme
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File

/** Writes fixtures only in an unpaired emulator, never a personal device. */
class MobileNavigationTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    @Test fun systemBackAndToolbarRestoreJournalTimelineAndNestedObjects() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        val app = compose.activity.application as LumenApplication
        check(!app.settings.hasConnection()) { "Navigation fixtures require an unpaired emulator" }
        lateinit var vm: LumenViewModel
        compose.runOnUiThread {
            if (!app.vault.isSetup) app.vault.setup("isolated-test-password".toCharArray())
            check(app.vault.unlockWithDevice())
            app.settings.autoSync = false
            val repo = app.repository
            val image = LumenObject("qa-navigation-image", "image", "Imagen de navegación", properties = JSONObject().put("asset_id", "qa-navigation-asset"), createdAt = nowIso(), updatedAt = nowIso())
            val bytes = ByteArrayOutputStream().also { Bitmap.createBitmap(400, 100, Bitmap.Config.ARGB_8888).compress(Bitmap.CompressFormat.PNG, 100, it) }.toByteArray()
            val file = File(app.filesDir, "assets/qa-navigation.png")
            SecureMedia.save(app.vault, file, bytes)
            repo.db.saveAsset(LumenAsset("qa-navigation-asset", objectId = image.id, mime = "image/png", localPath = file.absolutePath))
            fun imageBlock() = JSONObject().put("id", "embed").put("type", "embed").put("text", "\uFFFC").put("runs", JSONArray().put(JSONObject().put("text", "\uFFFC").put("type", "image").put("object_id", image.id).put("image_name", "lumen://image/${image.id}").put("width", 400).put("height", 100)))
            val blocks = JSONArray().put(JSONObject().put("id", "text").put("type", "paragraph").put("text", "Origen del diario").put("runs", JSONArray().put(JSONObject().put("text", "Origen del diario")))).put(imageBlock())
            val page = LumenObject("qa-navigation-page", "page", "Página de navegación", properties = JSONObject().put("blocks", blocks), content = nativeDocumentText(blocks), createdAt = nowIso(), updatedAt = nowIso())
            val journal = LumenObject("journal-${todayIso()}", "journal", "Diario de prueba", properties = JSONObject().put("blocks", blocks), content = nativeDocumentText(blocks), createdAt = nowIso(), updatedAt = nowIso())
            listOf(image, page, journal).forEach { repo.db.saveObject(it) }
            repo.bump()
            vm = ViewModelProvider(compose.activity)[LumenViewModel::class.java]
            compose.activity.setContent { LumenTheme { AppRoot(onLock = {}) } }
        }
        compose.onNodeWithContentDescription("Diario").performClick()
        val imageNode = compose.onNodeWithContentDescription("Imagen de navegación").performScrollTo()
        val originalPosition = imageNode.fetchSemanticsNode().boundsInRoot
        imageNode.performClick()
        compose.runOnIdle { assertEquals("qa-navigation-image", vm.activeObject?.id) }
        systemBack()
        compose.waitUntil { vm.activeObject == null }
        val restoredImage = compose.onNodeWithContentDescription("Imagen de navegación").assertIsDisplayed()
        assertEquals(originalPosition.top, restoredImage.fetchSemanticsNode().boundsInRoot.top, 2f)

        compose.onNode(hasText("Cronología") and isSelectable()).performClick()
        compose.onNodeWithText("Página de navegación").performScrollTo().performClick()
        compose.onNode(hasText("Gráfico") and isSelectable()).performClick()
        compose.runOnUiThread { vm.openObject(vm.getObject("qa-navigation-image")) }
        val display = compose.activity.resources.displayMetrics
        shellInput("input swipe 1 ${display.heightPixels / 2} ${display.widthPixels / 2} ${display.heightPixels / 2} 350")
        compose.waitUntil(5_000) { vm.activeObject?.id == "qa-navigation-page" }
        compose.runOnIdle { assertEquals("qa-navigation-page", vm.activeObject?.id) }
        // The parent object's tab survives an excursion to another object.
        compose.onNodeWithText("Gráfico").assertIsSelected()
        compose.onNodeWithContentDescription("Atrás").performClick()
        compose.runOnIdle { assertNull(vm.activeObject) }
        compose.onNode(hasText("Cronología") and isSelectable()).assertIsSelected()
        compose.onNodeWithText("Origen del diario").assertDoesNotExist()
        compose.onNode(hasText("Nota diaria") and isSelectable()).performClick()
        compose.onNodeWithText("Origen del diario").performScrollTo().assertIsDisplayed()
        val output = File(app.getExternalFilesDir(null), "journal-navigation-restored.png")
        compose.onRoot().captureToImage().asAndroidBitmap().let { bitmap -> output.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) } }
    }

    private fun systemBack() = shellInput("input keyevent 4")

    private fun shellInput(command: String) {
        val descriptor = InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand(command)
        java.io.FileInputStream(descriptor.fileDescriptor).use { it.readBytes() }
        descriptor.close()
        compose.waitForIdle()
    }
}
