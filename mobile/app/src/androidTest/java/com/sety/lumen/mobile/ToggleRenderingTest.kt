package com.sety.lumen.mobile

import android.graphics.Bitmap
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.sety.lumen.mobile.data.*
import com.sety.lumen.mobile.ui.NativeEditorContent
import com.sety.lumen.mobile.ui.theme.LumenTheme
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

class ToggleRenderingTest {
    @get:Rule val compose = createComposeRule()

    @Test fun toggleBodyIsEditableAndStateReloads() {
        val toggle = editToggleTitle(nativeToggle(), "Esto es un toggle").put("children", JSONArray()
            .put(nativeParagraph("Aquí van las cosas dentro del toggle"))
            .put(setToggleExpanded(editToggleTitle(nativeToggle(), "Toggle anidado"), false)))
        var source by mutableStateOf(LumenObject("fixture", "note", "Prueba").withNativeBlocks(JSONArray()
            .put(toggle).put(nativeParagraph("Fuera del toggle"))))
        compose.setContent {
            LumenTheme {
                Surface(Modifier.fillMaxSize()) {
                    Column(Modifier.padding(24.dp).verticalScroll(rememberScrollState())) {
                        NativeEditorContent(TextFieldValue(source.content), source, source.properties.getJSONArray("blocks"), emptyList(),
                            onValueChange = {}, onOpenObject = {}, assetFor = { null }, onBlocksChange = { source = source.withNativeBlocks(it) })
                    }
                }
            }
        }
        val body = compose.onNode(hasSetTextAction() and hasText("Aquí van las cosas dentro del toggle"))
        body.performTextReplacement("Contenido modificado desde Android")
        compose.waitForIdle()
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        fun capture(name: String) {
            File(context.getExternalFilesDir(null), name).outputStream().use {
                compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it)
            }
        }
        capture("toggle-expanded.png")
        compose.onNodeWithContentDescription("Contraer toggle").performClick()
        compose.onNodeWithText("Contenido modificado desde Android").assertDoesNotExist()
        compose.onNodeWithText("Fuera del toggle").assertExists()
        compose.runOnIdle {
            source = source.copy(properties = JSONObject(source.properties.toString()))
            assertFalse(toggleIsExpanded(source.properties.getJSONArray("blocks").getJSONObject(0)))
        }
        capture("toggle-collapsed.png")
        compose.onAllNodesWithContentDescription("Expandir toggle")[0].performClick()
        compose.onNodeWithText("Contenido modificado desde Android").assertExists()
        compose.runOnIdle {
            val children = source.properties.getJSONArray("blocks").getJSONObject(0).getJSONArray("children")
            assertEquals("Contenido modificado desde Android", children.getJSONObject(0).getString("text"))
            assertFalse(toggleIsExpanded(children.getJSONObject(1)))
        }
    }
}
