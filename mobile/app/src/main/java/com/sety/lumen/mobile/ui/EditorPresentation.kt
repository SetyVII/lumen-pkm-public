package com.sety.lumen.mobile.ui

import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextDecoration
import org.json.JSONArray
import androidx.compose.ui.text.input.OffsetMapping
import androidx.compose.ui.text.input.TransformedText
import androidx.compose.ui.text.input.VisualTransformation
import com.sety.lumen.mobile.data.EditorToken
import com.sety.lumen.mobile.data.EditorTokenKind

data class PresentedEditorToken(val source: EditorToken, val start: Int, val end: Int)

class EditorPresentation(
    private val source: String,
    tokens: List<EditorToken>,
    foreground: Color,
    background: Color,
    nativeRuns: JSONArray? = null,
) : VisualTransformation {
    val tokens: List<PresentedEditorToken>
    val transformed: TransformedText

    init {
        val output = AnnotatedString.Builder()
        val originalToDisplay = IntArray(source.length + 1)
        val displayToOriginal = mutableListOf(0)
        val displayedTokens = mutableListOf<PresentedEditorToken>()
        var position = 0
        fun append(start: Int, end: Int, text: String) {
            val displayStart = output.length
            output.append(text)
            val originalLength = end - start
            for (i in 0..originalLength) {
                originalToDisplay[start + i] = displayStart + if (originalLength == 0) 0 else i * text.length / originalLength
            }
            for (i in 1..text.length) displayToOriginal += start + i * originalLength / text.length
        }
        tokens.forEach { token ->
            append(position, token.start, source.substring(position, token.start))
            val start = output.length
            val label = when (token.kind) { EditorTokenKind.DIVIDER -> " "; EditorTokenKind.LINK -> token.label; else -> " ${token.label} " }
            append(token.start, token.end, label)
            val end = output.length
            if (token.kind == EditorTokenKind.REFERENCE) {
                output.addStyle(SpanStyle(color = foreground, background = background, fontWeight = FontWeight.Medium), start, end)
            }
            if (token.kind == EditorTokenKind.LINK) output.addStyle(SpanStyle(color = foreground, textDecoration = TextDecoration.Underline), start, end)
            displayedTokens += PresentedEditorToken(token, start, end)
            position = token.end
        }
        append(position, source.length, source.substring(position))
        var runOffset = 0
        for (i in 0 until (nativeRuns?.length() ?: 0)) {
            val run = nativeRuns?.optJSONObject(i) ?: continue
            val end = (runOffset + run.optString("text").length).coerceAtMost(source.length)
            val decorations = mutableListOf<TextDecoration>()
            if (run.optBoolean("underline")) decorations += TextDecoration.Underline
            if (run.optBoolean("strike")) decorations += TextDecoration.LineThrough
            if (end > runOffset) output.addStyle(SpanStyle(
                fontWeight = if (run.optBoolean("bold")) FontWeight.Bold else null,
                fontStyle = if (run.optBoolean("italic")) FontStyle.Italic else null,
                fontFamily = if (run.optBoolean("code")) FontFamily.Monospace else null,
                textDecoration = if (decorations.isEmpty()) null else TextDecoration.combine(decorations),
            ), originalToDisplay[runOffset], originalToDisplay[end])
            runOffset = end
        }
        this.tokens = displayedTokens
        transformed = TransformedText(output.toAnnotatedString(), object : OffsetMapping {
            override fun originalToTransformed(offset: Int) = originalToDisplay[offset.coerceIn(0, source.length)]
            override fun transformedToOriginal(offset: Int) = displayToOriginal[offset.coerceIn(0, displayToOriginal.lastIndex)]
        })
    }

    override fun filter(text: AnnotatedString): TransformedText = transformed
}
