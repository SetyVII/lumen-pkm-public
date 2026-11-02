package com.sety.lumen.mobile.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import ru.noties.jlatexmath.JLatexMathDrawable

private val mathParserMutex = Mutex()

/** Native Canvas drawing at the current density; no WebView or bitmap enlargement. */
@Composable
fun NativeMathPreview(latex: String, modifier: Modifier = Modifier, inline: Boolean = false) {
    val density = LocalDensity.current
    val color = MaterialTheme.colorScheme.onSurface.toArgb()
    val fontSize = with(density) { (if (inline) 16.sp else 22.sp).toPx() }
    var failure by remember(latex) { mutableStateOf("") }
    val drawable by produceState<JLatexMathDrawable?>(null, latex, fontSize, color) {
        value = null
        failure = ""
        if (latex.isBlank()) return@produceState
        if (latex.length > 8192) { failure = "Fórmula demasiado extensa para previsualizar"; return@produceState }
        val parsed = withContext(Dispatchers.Default) {
            // The upstream parser has static registries; serialize construction.
            mathParserMutex.withLock { runCatching {
                JLatexMathDrawable.builder(latex).textSize(fontSize).color(color).padding(4).build()
            } }
        }
        parsed.onSuccess { value = it }.onFailure { failure = "No se pudo representar la fórmula; se conserva el código LaTeX" }
    }
    val image = drawable
    when {
        failure.isNotBlank() -> Text(failure, modifier, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.labelSmall)
        image != null -> Box(modifier.horizontalScroll(rememberScrollState())) {
            Canvas(Modifier.size(with(density) { image.intrinsicWidth.toDp() }, with(density) { image.intrinsicHeight.toDp() })) {
                image.draw(drawContext.canvas.nativeCanvas)
            }
        }
        latex.isNotBlank() -> Text("Representando fórmula…", modifier.padding(4.dp), style = MaterialTheme.typography.labelSmall)
    }
}
