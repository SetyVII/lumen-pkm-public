package com.sety.lumen.mobile

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.os.Build
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Surface
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.dp
import androidx.test.platform.app.InstrumentationRegistry
import com.sety.lumen.mobile.data.*
import com.sety.lumen.mobile.ui.CanonicalDocumentEditor
import com.sety.lumen.mobile.ui.theme.LumenTheme
import java.io.File
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Rule
import org.junit.Test

/** Pure editor surface, local fake corpus, no repository or external server. */
class CanonicalEditorSmokeTest {
    @get:Rule val compose = createComposeRule()
    private var dismissKeyboard: () -> Unit = {}
    private var current by mutableStateOf(CanonicalDocument.empty())

    private fun start() {
        assumeTrue(Build.HARDWARE == "ranchu" || Build.FINGERPRINT.contains("generic"))
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val fixture =
            JSONObject(
                instrumentation.context.assets
                    .open("document-v2/rich.document.json")
                    .bufferedReader()
                    .use { it.readText() }
            )
        val blocks =
            JSONArray().put(documentParagraph("Editable smoke paragraph").put("id", "smoke-edit"))
        val original = fixture.getJSONArray("blocks")
        for (i in 0 until original.length()) blocks.put(original.getJSONObject(i))
        current = CanonicalDocument.empty().withBlocks(blocks)
        val image = File(instrumentation.targetContext.cacheDir, "qa-editor-fake-image.png")
        val bitmap = Bitmap.createBitmap(400, 120, Bitmap.Config.ARGB_8888)
        Canvas(bitmap).apply {
            drawColor(0xff204d60.toInt())
            drawText(
                "Imagen ficticia QA",
                20f,
                70f,
                Paint().apply {
                    color = -1
                    textSize = 28f
                    isAntiAlias = true
                },
            )
        }
        image.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
        val asset =
            LumenAsset(
                "asset-image",
                objectId = "object-image",
                mime = "image/png",
                localPath = image.absolutePath,
            )
        compose.setContent {
            val keyboard = LocalSoftwareKeyboardController.current
            val focus = LocalFocusManager.current
            SideEffect {
                dismissKeyboard = {
                    keyboard?.hide()
                    focus.clearFocus(force = true)
                }
            }
            LumenTheme {
                Surface(Modifier.fillMaxSize().imePadding()) {
                    Column(
                        Modifier.testTag("qa-scroll")
                            .verticalScroll(rememberScrollState())
                            .padding(12.dp)
                    ) {
                        CanonicalDocumentEditor(
                            current,
                            emptyList(),
                            { id -> asset.takeIf { id == asset.id } },
                            {},
                            { current = it },
                            objectId = "qa-smoke-object",
                        )
                    }
                }
            }
        }
        compose.waitForIdle()
    }

    private fun reveal(node: SemanticsNodeInteraction): SemanticsNodeInteraction {
        node.performScrollTo()
        repeat(35) {
            if (node.isDisplayed()) return node
            val y = node.fetchSemanticsNode().positionInRoot.y
            val scroll = compose.onNodeWithTag("qa-scroll")
            val height = scroll.fetchSemanticsNode().size.height.toFloat()
            val delta = (y - height / 3).coerceIn(-height / 2, height / 2)
            scroll.performSemanticsAction(SemanticsActions.ScrollBy) { action -> action(0f, delta) }
        }
        screenshot("qa-reveal-failure.png")
        return node.assertIsDisplayed()
    }

    private fun screenshot(name: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bitmap = compose.onAllNodes(isRoot()).onLast().captureToImage().asAndroidBitmap()
        File(context.cacheDir, name).outputStream().use {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
        }
    }

    @Test
    fun editSelectParentDeleteUndoAndAtomicImage() {
        start()
        compose
            .onNode(hasSetTextAction() and hasText("Editable smoke paragraph"))
            .performTextReplacement("Edited native paragraph")
        compose.runOnIdle {
            assertEquals("Edited native paragraph", documentNodeText(current.block("smoke-edit")!!))
        }
        compose.runOnIdle { dismissKeyboard() }
        compose.waitForIdle()
        val originalGroup = current.block("group")!!.toString()
        reveal(compose.onNodeWithTag("document-select-group")).performClick()
        compose.onNodeWithTag("document-select-toggle").assertIsOn().assertIsNotEnabled()
        reveal(compose.onNodeWithText("Copiar selección")).performClick()
        compose.runOnIdle {
            val context = InstrumentationRegistry.getInstrumentation().targetContext
            val clipboard =
                context.getSystemService(android.content.Context.CLIPBOARD_SERVICE)
                    as android.content.ClipboardManager
            assertTrue(clipboard.primaryClipDescription!!.hasMimeType(DOCUMENT_CLIPBOARD_MIME))
            assertEquals(
                1,
                JSONObject(
                        clipboard.primaryClip!!
                            .getItemAt(0)
                            .intent!!
                            .getStringExtra("lumen_document_v2")!!
                    )
                    .getInt("clipboard_version"),
            )
        }
        reveal(compose.onNodeWithText("Eliminar selección")).performClick()
        compose.runOnIdle {
            assertNull(current.block("group"))
            assertNull(current.block("toggle"))
        }
        reveal(compose.onNodeWithText("Deshacer")).performClick()
        compose.runOnIdle { assertEquals(originalGroup, current.block("group")!!.toString()) }
        val imageFields =
            compose.onAllNodes(
                hasSetTextAction() and hasAnyAncestor(hasTestTag("document-node-image"))
            )
        imageFields.assertCountEquals(
            1
        ) // Caption only; the atomic image is not an editable paragraph.
        imageFields[0].performScrollTo()
        screenshot("qa-editor-image-atomic.png")
    }

    @Test fun invalidStructuredMimeDoesNotSilentlyPasteAsText() {
        start()
        val before=current.toJson().toString()
        compose.runOnIdle {
            val context=InstrumentationRegistry.getInstrumentation().targetContext
            val manager=context.getSystemService(android.content.Context.CLIPBOARD_SERVICE) as android.content.ClipboardManager
            manager.setPrimaryClip(android.content.ClipData(android.content.ClipDescription("Invalid fixture",arrayOf(DOCUMENT_CLIPBOARD_MIME)),android.content.ClipData.Item("not json")))
        }
        reveal(compose.onNodeWithText("Pegar bloques al final")).performClick()
        compose.runOnIdle {assertEquals(before,current.toJson().toString())}
    }

    @Test
    fun tableSelectionAppliesFormattingOnlyToChosenColumn() {
        start()
        reveal(compose.onNodeWithTag("table-scope-table-column")).performClick()
        reveal(compose.onNodeWithTag("table-format-table-bold")).performClick()
        screenshot("qa-editor-table-before-assert.png")
        compose.runOnIdle {
            val rows = current.block("table")!!.getJSONArray("children")
            for (r in 0 until rows.length()) {
                val cells = rows.getJSONObject(r).getJSONArray("children")
                fun bold(c: Int) =
                    cells
                        .getJSONObject(c)
                        .getJSONArray("children")
                        .getJSONObject(0)
                        .getJSONArray("content")
                        .getJSONObject(0)
                        .optJSONObject("marks")
                        ?.optBoolean("bold") ?: false
                assertTrue("Row $r: ${rows.getJSONObject(r)}", bold(0))
                assertFalse("Row $r changed other column", bold(1))
            }
        }
        screenshot("qa-editor-table-column.png")
    }

    @Test
    fun temporalPickerAndNativeEquationAreVisible() {
        start()
        compose.onNodeWithText("LaTeX").performScrollTo()
        compose.waitForIdle()
        compose.onAllNodes(hasText("No se pudo representar", substring = true)).assertCountEquals(0)
        screenshot("qa-editor-native-math.png")
        val date =
            current.block("date")!!.getJSONArray("content").getJSONObject(0).getJSONObject("attrs")
        val label = com.sety.lumen.mobile.ui.temporalDisplay(date)
        reveal(compose.onNode(hasText(label) and hasClickAction())).performClick()
        compose.onNodeWithText("Fecha y hora").assertIsDisplayed()
        compose.onNodeWithText("Fecha de finalización").assertExists()
        screenshot("qa-editor-temporal-picker.png")
        compose.onNodeWithText("Cancelar").performScrollTo().performClick()
        compose.runOnIdle {
            assertEquals(
                date.toString(),
                current
                    .block("date")!!
                    .getJSONArray("content")
                    .getJSONObject(0)
                    .getJSONObject("attrs")
                    .toString(),
            )
        }
    }
}
