package com.sety.lumen.mobile

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.widget.Toast
import com.sety.lumen.mobile.data.CryptoBox
import org.json.JSONObject
import java.util.UUID
import androidx.work.WorkManager
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.workDataOf
import androidx.work.Constraints
import androidx.work.NetworkType
import com.sety.lumen.mobile.sync.SaveSharedLinkWorker
import com.sety.lumen.mobile.sync.WebLinkPreviewWorker
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Error
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sety.lumen.mobile.data.todayIso
import com.sety.lumen.mobile.ui.LumenViewModel
import com.sety.lumen.mobile.ui.theme.LumenTheme
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext

class ShareActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        val shared = parseIntent()
        // A link share has no window: persist work and return to the sending app.
        if (shared !is SharedContent.Link) setTheme(R.style.Theme_Lumen_Dialog)
        super.onCreate(savedInstanceState)
        if (shared is SharedContent.Link) {
            val app = application as LumenApplication
            runCatching {
                check(app.vault.unlockWithDevice()) { "Desbloquea Lumen para guardar el enlace" }
                val id = UUID.randomUUID().toString()
                val payload = CryptoBox.encryptText(app.vault.requireMaster(),
                    JSONObject().put("url", shared.url).put("title", shared.title).toString(), "share/$id")
                val save = OneTimeWorkRequestBuilder<SaveSharedLinkWorker>()
                    .setInputData(workDataOf("share_id" to id, "payload" to payload))
                    .build()
                val preview = OneTimeWorkRequestBuilder<WebLinkPreviewWorker>()
                    .setConstraints(Constraints.Builder().setRequiredNetworkType(NetworkType.CONNECTED).build())
                    .build()
                WorkManager.getInstance(this).beginWith(save).then(preview).enqueue()
            }.onFailure {
                Toast.makeText(this, it.message ?: "No se pudo guardar el enlace", Toast.LENGTH_SHORT).show()
            }
            finish()
            return
        }
        setContent {
            LumenTheme {
                ShareScreen(shared, onClose = { finish() })
            }
        }
    }

    private fun parseIntent(): SharedContent {
        val action = intent?.action
        val text = intent?.getStringExtra(Intent.EXTRA_TEXT).orEmpty()
        val title = intent?.getStringExtra(Intent.EXTRA_TITLE)
            ?: intent?.getStringExtra(Intent.EXTRA_SUBJECT).orEmpty()
        val url = Regex("""https?://[^\s]+""").find(text)?.value?.trimEnd('.', ',', ')', ']', '"', '\'')
        @Suppress("DEPRECATION")
        val stream: Uri? = intent?.getParcelableExtra(Intent.EXTRA_STREAM)
        @Suppress("DEPRECATION")
        val streams: List<Uri> = if (action == Intent.ACTION_SEND_MULTIPLE) {
            intent?.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM).orEmpty()
        } else {
            listOfNotNull(stream)
        }
        val mime = intent?.type.orEmpty()
        return when {
            streams.isNotEmpty() && mime.startsWith("image/") -> SharedContent.Images(streams, text.take(280))
            !url.isNullOrBlank() -> SharedContent.Link(url, title.ifBlank { text.replace(url, "").trim() })
            text.isNotBlank() -> SharedContent.Text(text, title)
            else -> SharedContent.Empty
        }
    }
}

private sealed interface SharedContent {
    data class Link(val url: String, val title: String) : SharedContent
    data class Images(val uris: List<Uri>, val caption: String) : SharedContent
    data class Text(val text: String, val title: String) : SharedContent
    data object Empty : SharedContent
}

private sealed interface ShareState {
    data object Working : ShareState
    data class Success(val title: String, val detail: String) : ShareState
    data class Failure(val message: String) : ShareState
}

@Composable
private fun ShareScreen(shared: SharedContent, onClose: () -> Unit) {
    val context = LocalContext.current
    val vm: LumenViewModel = viewModel()
    var state by remember { mutableStateOf<ShareState>(ShareState.Working) }

    LaunchedEffect(shared) {
        when (shared) {
            is SharedContent.Link -> Unit
            is SharedContent.Images -> {
                val results = withContext(Dispatchers.IO) {
                    shared.uris.map { uri -> runCatching { vm.repository.captureMoment(uri, shared.caption, todayIso(), true) } }
                }
                val saved = results.count { it.isSuccess }
                val firstError = results.firstNotNullOfOrNull { it.exceptionOrNull()?.message }
                state = if (saved > 0) {
                    ShareState.Success(
                        if (saved == 1) "Momento guardado" else "$saved momentos guardados",
                        "Añadido(s) al diario de hoy",
                    )
                } else {
                    ShareState.Failure(firstError ?: "No se pudo guardar la imagen")
                }
            }
            is SharedContent.Text -> {
                vm.quickNote(shared.text, false, null, todayIso())
                state = ShareState.Success(shared.title.ifBlank { "Nota guardada" }, "Añadida al diario de hoy")
            }
            SharedContent.Empty -> state = ShareState.Failure("No hay nada que guardar")
        }
    }

    LaunchedEffect(state) {
        if (state is ShareState.Success) {
            delay(2600)
            onClose()
        }
    }

    Surface(Modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().statusBarsPadding().padding(26.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            when (val current = state) {
                ShareState.Working -> {
                    CircularProgressIndicator()
                    Spacer(Modifier.height(18.dp))
                    Text("Guardando en Lumen…", style = MaterialTheme.typography.titleMedium)
                    Text(
                        "La portada se descarga y comprime en el teléfono.",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(top = 6.dp),
                    )
                }
                is ShareState.Success -> {
                    Icon(Icons.Default.CheckCircle, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(Modifier.height(12.dp))
                    Text(current.title, style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                    Text(current.detail, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.height(18.dp))
                    Button(onClick = {
                        context.startActivity(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                        onClose()
                    }) { Text("Abrir Lumen") }
                    TextButton(onClick = onClose) { Text("Cerrar") }
                }
                is ShareState.Failure -> {
                    Icon(Icons.Default.Error, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                    Spacer(Modifier.height(12.dp))
                    Text("No se pudo guardar", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                    Text(current.message, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.height(18.dp))
                    Button(onClick = onClose) { Text("Cerrar") }
                }
            }
        }
    }
}
