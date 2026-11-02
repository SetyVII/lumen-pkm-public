package com.sety.lumen.mobile.net

import com.sety.lumen.mobile.data.KeyVault
import com.sety.lumen.mobile.data.LumenAsset
import com.sety.lumen.mobile.data.LumenDatabase
import com.sety.lumen.mobile.data.OutboxOp
import com.sety.lumen.mobile.data.SecureMedia
import com.sety.lumen.mobile.data.SettingsStore
import com.sety.lumen.mobile.data.SyncConflict
import com.sety.lumen.mobile.data.nowIso
import com.sety.lumen.mobile.data.documentOutboxWindow
import com.sety.lumen.mobile.data.readyDocumentBatch
import com.sety.lumen.mobile.data.consumeSyncPullPages
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import android.util.Base64
import org.json.JSONObject
import java.io.File
import java.time.Instant
import java.time.LocalDate

private val mobileSyncMutex = Mutex()

data class SyncOutcome(
    val success: Boolean,
    val message: String,
    val endpoint: String = "",
    val pushed: Int = 0,
    val pulled: Int = 0,
    val conflicts: List<SyncConflict> = emptyList(),
)

class SyncEngine(
    private val db: LumenDatabase,
    private val vault: KeyVault,
    private val settings: SettingsStore,
    private val filesDir: File,
) {
    suspend fun syncNow(): SyncOutcome = mobileSyncMutex.withLock { withContext(Dispatchers.IO) {
        if (!vault.isUnlocked && !vault.unlockWithDevice()) {
            return@withContext SyncOutcome(false, "Lumen está bloqueado")
        }
        val token = vault.decryptToken()
        if (token.isBlank()) {
            return@withContext SyncOutcome(false, "Falta el token de emparejamiento")
        }
        val endpoint = resolveEndpoint(token)
            ?: return@withContext SyncOutcome(false, "No se encontró el servidor Lumen en la red local ni por USB")
        val api = LumenApi(settings, token, endpoint)
        ensureCustomTypes(api)
        var pushed = 0
        var pushWarning: String? = null
        val conflicts = mutableListOf<SyncConflict>()
        runCatching {
            var dispatched = 0
            while (dispatched < 1000) {
                // Re-read after each ACK: never send two snapshots at the same
                // old base and never modify a payload which may have reached the server.
                val seeds = db.outbox(100)
                if (seeds.isEmpty()) break
                val window = documentOutboxWindow(seeds, db::pendingAssetOperation, db::firstPendingPut)
                val ready = readyDocumentBatch(window, db::pendingAssetOperation, db::firstPendingPut)
                check(ready.isNotEmpty()) { "Dependencias pendientes sin avance; los cambios permanecen guardados" }
                val batch = mutableListOf<OutboxOp>()
                var bytes = 0L
                for (op in ready) {
                    val size = if(op.action == "asset") op.payload.optString("local_path").takeIf(String::isNotBlank)?.let{File(it).length()} ?: 0L
                        else op.payload.toString().toByteArray(Charsets.UTF_8).size.toLong()
                    if(batch.isNotEmpty() && bytes+size > 18L*1024*1024) break
                    batch += op; bytes += size
                }
                // Claim under the same DB transaction that reads payloads. An
                // autosave may have coalesced a draft since window construction.
                val claimed = db.claimOutbox(batch.map{it.opId})
                if(claimed.isEmpty()) continue
                val operations = claimed.map { expandOperation(it.payload).apply { put("object_id",it.objectId) } }
                val result = api.push(settings.deviceId, db.meta("document_v2_pull_seq")?.toLongOrNull() ?: 0L, operations)
                db.confirmPush(result.applied,operations,result.receipts,result.conflicts)
                pushed += result.applied.size; conflicts += result.conflicts; dispatched += claimed.size
                if(result.errors.isNotEmpty()) {
                    pushWarning = "El servidor rechazó ${result.errors.size} operación(es): ${result.errors.first().error}"
                    break
                }
                if(result.applied.isEmpty()) { pushWarning="El servidor no confirmó los cambios pendientes";break }
            }
            if(dispatched>=1000 && db.outbox(1).isNotEmpty() && pushWarning==null) pushWarning="Quedan cambios guardados pendientes; continúa la sincronización"
            pushWarning

        }.onFailure { error ->
            return@withContext SyncOutcome(false, "Error al subir cambios: ${error.message}", endpoint)
        }.getOrNull()?.let { message ->
            settings.lastSyncMessage = message
        }

        var pulledCount=0
        val progress=runCatching {
            consumeSyncPullPages(db.meta("document_v2_pull_seq")?.toLongOrNull()?:0L,
                fetch={cursor->api.pull(cursor,settings.mediaSinceDate)},apply={page->
                    db.applyPull(page)
                    settings.lastSeq=page.nextSeq
                    pulledCount+=page.objects.size+page.types.size
                })
        }.getOrElse {error->
            return@withContext SyncOutcome(false,"Descarga interrumpida; las páginas confirmadas se conservan: ${error.message}",endpoint,pushed,pulledCount,conflicts)
        }
        if(progress.hasMore)pushWarning=listOfNotNull(pushWarning,"Quedan páginas por descargar; continúa la sincronización").joinToString(" · ")
        val blocked=db.blockedConflictCount()
        if(blocked>0)pushWarning=listOfNotNull(pushWarning,"$blocked borrador(es) conservados y bloqueados por borrado remoto; requieren recuperación explícita").joinToString(" · ")
        runCatching { api.status().serverId }.getOrNull()?.takeIf { it.isNotBlank() }?.let { settings.serverId = it }
        val downloaded = downloadAssets(api, db.assets())
        settings.lastSyncAt = nowIso()
        settings.lastSyncMessage = buildString {
            append(if(pushWarning==null) "Sincronizado " else "Sincronización parcial ")
            append(java.time.LocalTime.now().withNano(0))
            if (pushed > 0) append(" · $pushed subida(s)")
            if (pulledCount > 0) append(" · $pulledCount cambio(s)")
            if (downloaded > 0) append(" · $downloaded medio(s)")
            if (conflicts.isNotEmpty()) append(" · ${conflicts.size} conflicto(s)")
            pushWarning?.let { append(" · ").append(it) }
        }
        SyncOutcome(
            success = pushWarning == null,
            message = settings.lastSyncMessage,
            endpoint = endpoint,
            pushed = pushed,
            pulled = pulledCount,
            conflicts = conflicts,
        )
    }

    }

    private fun resolveEndpoint(token: String): String? {
        val candidates = linkedSetOf<String>()
        val configured = settings.serverUrl.trimEnd('/')
        val privateConfigured = configured.startsWith("http://") && (
            configured.contains("127.0.0.1") || configured.contains("localhost") ||
                configured.contains("192.168.") || configured.contains("10.") || configured.contains("172.")
            )
        if (privateConfigured) candidates += "http://127.0.0.1:8765"
        if (settings.lastLanUrl.isNotBlank()) candidates += settings.lastLanUrl.trimEnd('/')
        candidates += configured
        for (candidate in candidates) {
            val ok = runCatching {
                val api = LumenApi(settings, token, candidate)
                val status = api.status()
                if (candidate != configured && candidate.startsWith("http://")) {
                    settings.lastLanUrl = candidate
                }
                status.headSeq >= 0 && status.protocol == "lumen-sync/2"
            }.getOrDefault(false)
            if (ok) return candidate
        }
        return null
    }

    /**
     * Registers the phone's custom object types that the server does not know
     * yet. Without this, objects of a locally created type would be rejected
     * forever after re-pairing against a fresh server ("Unknown object
     * type"). The server keeps the pinned id, so object type references stay
     * valid. Failures here only warn: the next sync retries.
     */
    private fun ensureCustomTypes(api: LumenApi) {
        runCatching {
            val remote = api.types().map { it.id }.toSet()
            val missing = db.types().filter { it.id.isNotBlank() && it.id !in remote }
            if (missing.isEmpty()) return
            android.util.Log.i("LumenSync", "Registrando ${missing.size} tipo(s) personalizado(s)")
            missing.forEach { type -> runCatching { api.createType(type) } }
        }.onFailure {
            android.util.Log.w("LumenSync", "No se pudieron comprobar tipos personalizados: ${it.message}")
        }
    }

    private fun expandOperation(op: JSONObject): JSONObject {
        if (op.optString("action") != "asset") return op
        val path = op.optString("local_path")
        if (path.isBlank()) return op
        val file = File(path)
        if (!file.exists()) return op
        val bytes = SecureMedia.read(vault, path) ?: return op
        if (bytes.size > 20 * 1024 * 1024) return op
        val asset = op.optJSONObject("asset") ?: return op
        asset.put("data_base64", Base64.encodeToString(bytes, Base64.NO_WRAP))
        op.remove("local_path")
        return op
    }

    private fun downloadAssets(api: LumenApi, assets: List<LumenAsset>): Int {
        val cutoff = runCatching { LocalDate.parse(settings.mediaSinceDate) }.getOrDefault(LocalDate.now().minusDays(90))
        var downloaded = 0
        assets.forEach { asset ->
            val existing = db.asset(asset.id)
            if (existing?.localPath?.isNotBlank() == true && File(existing.localPath).exists()) return@forEach
            val created = runCatching { Instant.parse(asset.createdAt).atZone(java.time.ZoneId.systemDefault()).toLocalDate() }.getOrNull()
            val isPreview = asset.name.contains("preview", ignoreCase = true) || asset.name.startsWith("og-") || db.getObject(asset.objectId)?.isWeb == true
            val shouldDownload = when {
                asset.isPdf -> asset.pinned
                asset.mime.startsWith("audio/") -> asset.pinned || created == null || !created.isBefore(cutoff)
                asset.isImage -> asset.pinned || isPreview || created == null || !created.isBefore(cutoff)
                else -> false
            }
            if (!shouldDownload) return@forEach
            val extension = when {
                asset.mime.contains("webp") -> "webp"
                asset.mime.contains("png") -> "png"
                asset.mime.contains("jpeg") || asset.mime.contains("jpg") -> "jpg"
                asset.mime.contains("gif") -> "gif"
                asset.mime == "application/pdf" -> "pdf"
                else -> "bin"
            }
            val destination = File(File(filesDir, "assets"), "${asset.id}.$extension")
            runCatching {
                api.downloadAsset(asset.id, destination)
                // Seal the downloaded media immediately so nothing rests in the
                // clear on disk; the plaintext never leaves this block.
                SecureMedia.save(vault, destination, destination.readBytes())
                db.setAssetPath(asset.id, destination.absolutePath)
                downloaded++
            }
        }
        return downloaded
    }
}
