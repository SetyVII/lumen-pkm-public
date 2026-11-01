package com.sety.lumen.mobile.ui

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.datasource.ByteArrayDataSource
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.ProgressiveMediaSource
import com.sety.lumen.mobile.data.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

@androidx.annotation.OptIn(androidx.media3.common.util.UnstableApi::class)
@Composable
fun LocalAttachment(obj: LumenObject, vm: LumenViewModel) {
    val context = LocalContext.current
    val asset = vm.asset(obj.assetId)
    if (obj.typeId !in setOf("audio", "file", "pdf", "video") && asset?.mime?.startsWith("audio/") != true && asset?.isPdf != true) return
    if (asset == null || asset.localPath.isBlank()) {
        Text("Archivo pendiente de descarga")
        if (asset != null) TextButton(onClick = { vm.pinAsset(asset.id, true); vm.syncNow() }) { Text("Descargar archivo") }
        return
    }
    var bytes by remember(asset.localPath) { mutableStateOf<ByteArray?>(null) }
    LaunchedEffect(asset.localPath) { bytes = withContext(Dispatchers.IO) { SecureMedia.read(vm.vault, asset.localPath) } }
    if (asset.mime.startsWith("audio/")) {
        val data = bytes ?: return
        var playing by remember { mutableStateOf(false) }
        var duration by remember { mutableStateOf(0L) }
        var ready by remember { mutableStateOf(false) }
        var error by remember { mutableStateOf("") }
        val player = remember(asset.id, data) { ExoPlayer.Builder(context).build() }
        DisposableEffect(player) {
            val listener = object : Player.Listener {
                override fun onIsPlayingChanged(isPlaying: Boolean) { playing = isPlaying }
                override fun onPlaybackStateChanged(state: Int) { ready = state == Player.STATE_READY || state == Player.STATE_ENDED; if (ready) duration = player.duration.coerceAtLeast(0) }
                override fun onPlayerError(exception: PlaybackException) { error = "No se puede reproducir este formato de audio" }
            }
            player.addListener(listener)
            player.setMediaSource(ProgressiveMediaSource.Factory { ByteArrayDataSource(data) }
                .createMediaSource(MediaItem.fromUri(Uri.parse("lumen-memory://audio/${asset.id}"))))
            player.prepare()
            onDispose { player.removeListener(listener); player.release() }
        }
        Column(Modifier.fillMaxWidth().padding(vertical = 12.dp)) {
            Text("Audio guardado en el dispositivo · ${duration / 60_000}:${((duration / 1000) % 60).toString().padStart(2, '0')}", style = MaterialTheme.typography.labelMedium)
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Button(enabled = ready && error.isBlank(), onClick = {
                    if (playing) player.pause() else { if (player.playbackState == Player.STATE_ENDED) player.seekTo(0); player.play() }
                }) { Text(if (playing) "Pausar audio" else "Reproducir audio") }
                TextButton(enabled = ready, onClick = { player.seekTo(0) }) { Text("Desde el inicio") }
            }
            if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error)
        }
    } else {
        TextButton(onClick = {
            val plain = SecureMedia.tempPlain(context, vm.vault, asset.localPath) ?: return@TextButton
            val uri = FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", plain)
            runCatching { context.startActivity(Intent(Intent.ACTION_VIEW).setDataAndType(uri, asset.mime).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)) }
        }) { Text("Abrir archivo") }
    }
}
