package com.sety.lumen.mobile.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import com.sety.lumen.mobile.LumenApplication
import com.sety.lumen.mobile.data.SecureMedia
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Resolves a local media path into a Coil model. Files sealed at rest
 * ([SecureMedia]) are decrypted once per path on the IO dispatcher; legacy
 * plaintext files are handed to Coil as a File right away. Returns null while
 * the vault is locked or the file is unavailable, so callers keep their
 * existing placeholder states.
 */
@Composable
fun rememberMediaModel(path: String?): Any? {
    if (path.isNullOrBlank()) return null
    val context = LocalContext.current
    var model by remember(path) { mutableStateOf<Any?>(if (SecureMedia.peekLegacy(path)) File(path) else null) }
    LaunchedEffect(path, context) {
        val app = context.applicationContext as? LumenApplication ?: return@LaunchedEffect
        val bytes = withContext(Dispatchers.IO) { SecureMedia.read(app.vault, path) }
        if (bytes != null) model = bytes
    }
    return model
}
