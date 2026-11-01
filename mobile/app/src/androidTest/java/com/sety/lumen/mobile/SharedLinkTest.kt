package com.sety.lumen.mobile

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Color
import android.os.Build
import android.view.accessibility.AccessibilityEvent
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.CopyOnWriteArrayList
import androidx.activity.compose.setContent
import androidx.compose.material3.Text
import androidx.compose.ui.test.*
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.lifecycle.ViewModelProvider
import com.sety.lumen.mobile.data.SecureMedia
import com.sety.lumen.mobile.ui.LumenViewModel
import com.sety.lumen.mobile.ui.WebLinkDetailScreen
import com.sety.lumen.mobile.ui.theme.LumenTheme
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import okio.Buffer
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.io.File

/** Isolated HTTP + unpaired emulator: no external site or personal library. */
class SharedLinkTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    @Test fun linkShareReturnsImmediatelyAndKeepsDownloadedThumbnailOffline() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        val app = compose.activity.application as LumenApplication
        check(!app.settings.hasConnection()) { "Share fixtures require an unpaired emulator" }
        compose.runOnUiThread {
            if (!app.vault.isSetup) app.vault.setup("isolated-test-password".toCharArray())
            check(app.vault.unlockWithDevice())
            app.settings.autoSync = false
            compose.activity.setContent { LumenTheme { Text("Aplicación de origen de prueba") } }
        }
        val server = MockWebServer()
        server.start()
        val png = ByteArrayOutputStream().also { out ->
            Bitmap.createBitmap(1200, 600, Bitmap.Config.ARGB_8888).apply { eraseColor(Color.rgb(40, 120, 160)) }.compress(Bitmap.CompressFormat.PNG, 100, out)
        }.toByteArray()
        server.enqueue(MockResponse().setHeader("Content-Type", "text/html").setBody("""<html><head><meta property="og:title" content="Enlace ficticio con miniatura"><meta property="og:image" content="/thumb.png"><meta property="og:description" content="Vista previa guardada en el dispositivo"></head></html>"""))
        server.enqueue(MockResponse().setHeader("Content-Type", "image/png").setBody(Buffer().write(png)))
        val url = server.url("/shared-link").toString()
        val toasts = CopyOnWriteArrayList<String>()
        val automation = InstrumentationRegistry.getInstrumentation().uiAutomation
        automation.setOnAccessibilityEventListener { event ->
            if (event.eventType == AccessibilityEvent.TYPE_NOTIFICATION_STATE_CHANGED) toasts.addAll(event.text.map { it.toString() })
        }
        val diaryBefore = app.repository.journal(com.sety.lumen.mobile.data.todayIso())?.toJson()?.toString()
        compose.runOnUiThread {
            compose.activity.startActivity(Intent(compose.activity, ShareActivity::class.java).apply {
                action = Intent.ACTION_SEND
                type = "text/plain"
                putExtra(Intent.EXTRA_TEXT, url)
            })
        }
        // Source activity stays visible instead of a Lumen success/dialog screen.
        compose.onNodeWithText("Aplicación de origen de prueba").assertIsDisplayed()
        compose.waitUntil(30_000) { app.repository.webLinks().any { it.sourceUrl == url } }
        compose.waitUntil(10_000) { toasts.any { it.contains("Guardado en Weblinks correctamente") } }
        automation.setOnAccessibilityEventListener(null)
        compose.waitUntil(40_000) { app.repository.webLinks().first { it.sourceUrl == url }.assetId.isNotBlank() }
        val web = app.repository.webLinks().first { it.sourceUrl == url }
        assertEquals("Para revisar", web.status)
        assertEquals("ready", web.stringProperty("preview_status"))
        assertEquals(diaryBefore, app.repository.journal(com.sety.lumen.mobile.data.todayIso())?.toJson()?.toString())
        val asset = app.repository.asset(web.assetId)!!
        assertTrue(File(asset.localPath).exists())
        val sealed = File(asset.localPath).readBytes()
        val bytes = SecureMedia.read(app.vault, asset.localPath)!!
        assertFalse(sealed.contentEquals(bytes))
        val bitmap = android.graphics.BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        assertTrue(bitmap.width <= 800)
        assertEquals(2, server.requestCount)
        server.shutdown()
        // Read and render again with the source HTTP server stopped.
        assertNotNull(SecureMedia.read(app.vault, asset.localPath))
        compose.runOnUiThread {
            val vm = ViewModelProvider(compose.activity)[LumenViewModel::class.java]
            compose.activity.setContent { LumenTheme { WebLinkDetailScreen(web, vm, onBack = {}) } }
        }
        compose.waitForIdle()
        val output = File(app.getExternalFilesDir(null), "shared-link-offline.png")
        compose.onRoot().captureToImage().asAndroidBitmap().let { bitmap -> output.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) } }
    }
}
