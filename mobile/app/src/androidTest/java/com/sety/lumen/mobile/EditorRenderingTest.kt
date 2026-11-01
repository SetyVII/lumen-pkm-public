package com.sety.lumen.mobile

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.sety.lumen.mobile.data.*
import com.sety.lumen.mobile.ui.LumenEditorField
import com.sety.lumen.mobile.ui.theme.LumenTheme
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

class EditorRenderingTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun inlineImagesListsAndRichRunsSurviveMobileEditing() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val assets = listOf("a", "b").associate { id ->
            val file = File(context.cacheDir, "native-editor-fixture-$id.png")
            val bitmap = Bitmap.createBitmap(400, 100, Bitmap.Config.ARGB_8888)
            val canvas = Canvas(bitmap)
            canvas.drawColor(if (id == "a") 0xff356d8a.toInt() else 0xff8a5c35.toInt())
            canvas.drawText("Imagen $id · datos ficticios", 18f, 60f, Paint().apply { color = -1; textSize = 25f; isAntiAlias = true })
            file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            id to LumenAsset(id, objectId = "image-$id", mime = "image/png", localPath = file.absolutePath)
        }
        val images = listOf("a", "b").map { id -> LumenObject("image-$id", "image", "Imagen $id", properties = JSONObject().put("asset_id", id)) }
        fun block(id: String, type: String, text: String, run: JSONObject) = JSONObject().put("id", id).put("type", type).put("text", text).put("runs", JSONArray().put(run))
        fun image(id: String) = block("image-$id", "embed", "\uFFFC", JSONObject().put("text", "\uFFFC").put("type", "image")
            .put("object_id", "image-$id").put("image_name", "lumen://image/image-$id").put("width", 400).put("height", 100))
        val blocks = JSONArray()
            .put(block("bullet", "bullet_list", "Viñeta de prueba", JSONObject().put("text", "Viñeta de prueba").put("bold", true)).put("indent", 1))
            .put(image("a"))
            .put(block("heading", "heading", "Entre las imágenes", JSONObject().put("text", "Entre las imágenes")).put("level", 2))
            .put(block("paragraph", "paragraph", "Texto en cursiva", JSONObject().put("text", "Texto en cursiva").put("italic", true)))
            .put(image("b"))
        var source by mutableStateOf(LumenObject("fixture", "note", "Prueba", content = nativeDocumentText(blocks), properties = JSONObject().put("blocks", blocks)))
        var value by mutableStateOf(TextFieldValue(source.content, TextRange(0)))
        fun update(next: TextFieldValue) {
            source = source.withPlainContent(next.text, images)
            value = next.copy(text = source.content, selection = TextRange(next.selection.end.coerceAtMost(source.content.length)))
        }
        compose.setContent {
            LumenTheme {
                Surface(Modifier.fillMaxSize()) {
                    Column(Modifier.padding(24.dp).verticalScroll(rememberScrollState())) {
                        Text("Bloques nativos · datos ficticios")
                        LumenEditorField(value, ::update, "Escribe aquí", source = source, objects = images, assetFor = { assets[it] })
                    }
                }
            }
        }
        compose.onNodeWithContentDescription("Imagen a").assertExists()
        compose.onNodeWithContentDescription("Imagen b").assertExists()
        val bullet = compose.onNode(hasSetTextAction() and hasText("Viñeta de prueba"))
        bullet.performClick()
        bullet.performTextInputSelection(TextRange("Viñeta de prueba".length))
        bullet.performTextInput(" editada")
        compose.waitForIdle()
        compose.runOnIdle {
            val edited = source.properties.getJSONArray("blocks")
            assertEquals("bullet_list", edited.getJSONObject(0).getString("type"))
            assertEquals("Viñeta de prueba editada", edited.getJSONObject(0).getString("text"))
            assertTrue(edited.getJSONObject(0).getJSONArray("runs").getJSONObject(0).getBoolean("bold"))
            for (i in 1 until blocks.length()) assertEquals(blocks.getJSONObject(i).toString(), edited.getJSONObject(i).toString())
        }
        compose.onNodeWithContentDescription("Imagen b").performScrollTo().assertIsDisplayed()
        val output = File(context.getExternalFilesDir(null), "native-editor-rendering.png")
        output.outputStream().use { compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it) }
        assets.values.forEach { File(it.localPath).delete() }
    }
    @Test
    fun textBeforeAndAfterAnImageStaysEditable() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val file = File(context.cacheDir, "qa-mixed-image.png")
        val bitmap = Bitmap.createBitmap(400, 100, Bitmap.Config.ARGB_8888).apply { eraseColor(0xff356d8a.toInt()) }
        file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        val image = LumenObject("mixed-image", "image", "Imagen entre texto", properties = JSONObject().put("asset_id", "mixed-asset"))
        val imageRun = JSONObject().put("text", "\uFFFC").put("type", "image").put("object_id", image.id)
            .put("image_name", "lumen://image/${image.id}").put("width", 400).put("height", 100)
        val runs = JSONArray().put(JSONObject().put("text", "Antes de la imagen"))
            .put(imageRun).put(JSONObject().put("text", "Después de la imagen").put("italic", true))
        val block = JSONObject().put("id", "mixed").put("type", "paragraph").put("text", "Antes de la imagen\uFFFCDespués de la imagen").put("runs", runs)
        var source by mutableStateOf(LumenObject("mixed-source", "note", "Texto e imagen", content = block.getString("text"), properties = JSONObject().put("blocks", JSONArray().put(block))))
        var value by mutableStateOf(TextFieldValue(source.content))
        compose.setContent {
            LumenTheme {
                Surface {
                    Column(Modifier.padding(24.dp)) {
                        Text("Texto e imagen en un mismo bloque")
                        LumenEditorField(value, onValueChange = { next ->
                            source = source.withPlainContent(next.text, listOf(image))
                            value = next.copy(text = source.content)
                        }, placeholder = "", source = source, objects = listOf(image),
                            assetFor = { LumenAsset("mixed-asset", objectId = image.id, mime = "image/png", localPath = file.absolutePath) })
                    }
                }
            }
        }
        compose.onNodeWithText("Después de la imagen").assertIsDisplayed()
        val before = compose.onNode(hasSetTextAction() and hasText("Antes de la imagen"))
        before.performTextReplacement("Antes editado")
        compose.onNodeWithContentDescription("Imagen entre texto").assertIsDisplayed()
        compose.onNodeWithText("Después de la imagen").assertIsDisplayed()
        compose.runOnIdle {
            val edited = source.properties.getJSONArray("blocks").getJSONObject(0)
            assertTrue(edited.getString("text").startsWith("Antes editado"))
            val editedRuns = edited.getJSONArray("runs")
            assertEquals(imageRun.toString(), (0 until editedRuns.length()).map { editedRuns.getJSONObject(it) }.first { it.optString("type") == "image" }.toString())
        }
        File(context.getExternalFilesDir(null), "mixed-image-text.png").outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it)
        }
        file.delete()
    }

    @Test fun createResizeAndDeleteStructuresThroughTheEditor() {
        var source by mutableStateOf(LumenObject("new-structures", "note", "Prueba").withNativeBlocks(JSONArray().put(nativeParagraph())))
        var value by mutableStateOf(TextFieldValue(source.content))
        compose.setContent { LumenTheme { Surface {
            Column(Modifier.padding(24.dp).verticalScroll(rememberScrollState())) {
                LumenEditorField(value, { next -> source = source.withPlainContent(next.text); value = next.copy(text = source.content) }, "",
                    source = source, onBlocksChange = { blocks -> source = source.withNativeBlocks(blocks); value = TextFieldValue(source.content) })
            }
        } } }
        compose.onNodeWithText("Añadir bloque").performClick()
        compose.onNodeWithText("Tabla").performClick()
        compose.onNodeWithText("Añadir fila").performScrollTo().performClick()
        compose.onNodeWithText("Opciones de tabla").performScrollTo().performClick()
        compose.onNodeWithText("Alinear columna: Centro").performClick()
        compose.runOnIdle { assertEquals("center", source.properties.getJSONArray("blocks").getJSONObject(1).getJSONArray("align").getString(0)) }
        compose.onNodeWithText("Opciones de tabla").performClick()
        compose.onNodeWithText("Eliminar fila 1").performClick()
        compose.onNodeWithText("Eliminar", substring = false).performClick()
        compose.runOnIdle { assertEquals(3, source.properties.getJSONArray("blocks").getJSONObject(1).getJSONArray("rows").length()) }
        compose.onNodeWithText("Eliminar tabla").performScrollTo().performClick()
        compose.onNodeWithText("Eliminar", substring = false).performClick()
        compose.onNodeWithText("Añadir bloque").performScrollTo().performClick()
        compose.onNodeWithText("Columnas", substring = false).performClick()
        compose.onNodeWithText("Añadir columna al grupo").performScrollTo().performClick()
        compose.runOnIdle { assertEquals(3, source.properties.getJSONArray("blocks").getJSONObject(1).getJSONArray("columns").length()) }
        compose.onNodeWithText("Eliminar columna 1").performScrollTo().performClick()
        compose.onNodeWithText("Eliminar", substring = false).performClick()
        compose.runOnIdle { assertEquals(2, source.properties.getJSONArray("blocks").getJSONObject(1).getJSONArray("columns").length()) }
        compose.onNodeWithText("Eliminar grupo de bloques").performScrollTo().performClick()
        compose.onNodeWithText("Eliminar", substring = false).performClick()
        compose.runOnIdle { assertEquals(1, source.properties.getJSONArray("blocks").length()) }
    }

    @Test
    fun nativeTablesAndChecklistRemainVisibleAndInteractive() {
        fun cell(text: String) = JSONObject().put("runs", JSONArray().put(JSONObject().put("text", text)))
        val todo = JSONObject().put("id", "check").put("type", "todo").put("text", "○ Revisar prueba")
            .put("runs", JSONArray().put(JSONObject().put("text", "○ Revisar prueba")))
        val table = JSONObject().put("id", "table").put("type", "table").put("text", "").put("runs", JSONArray())
            .put("rows", JSONArray().put(JSONArray().put(cell("Encabezado"))).put(JSONArray().put(cell("Valor de tabla"))))
        val blocks = JSONArray().put(todo).put(table)
        var source by mutableStateOf(LumenObject("table-check", "note", "Prueba", content = nativeDocumentText(blocks), properties = JSONObject().put("blocks", blocks)))
        var value by mutableStateOf(TextFieldValue(source.content))
        compose.setContent {
            LumenTheme {
                Surface {
                    Column(Modifier.padding(24.dp)) {
                        LumenEditorField(value, onValueChange = { next -> source = source.withPlainContent(next.text); value = next.copy(text = source.content) },
                            placeholder = "", source = source)
                    }
                }
            }
        }
        compose.onNodeWithText("Revisar prueba").assertIsDisplayed()
        val ctx = InstrumentationRegistry.getInstrumentation().targetContext
        File(ctx.getExternalFilesDir(null), "table-before-assert.png").outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it)
        }
        compose.onNodeWithText("Encabezado").assertIsDisplayed()
        compose.onNodeWithText("Valor de tabla").assertIsDisplayed()
        compose.onNode(isToggleable()).performClick().assertIsOn()
        compose.runOnIdle {
            assertTrue(source.content.startsWith("✔ "))
            assertEquals(table.toString(),source.properties.getJSONArray("blocks").getJSONObject(1).toString())
        }
    }

}
