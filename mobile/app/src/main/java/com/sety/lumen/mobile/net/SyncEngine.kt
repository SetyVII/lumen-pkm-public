package com.sety.lumen.mobile.net

import com.sety.lumen.mobile.data.KeyVault
import com.sety.lumen.mobile.data.LumenAsset
import com.sety.lumen.mobile.data.LumenDatabase
import com.sety.lumen.mobile.data.OutboxOp
import com.sety.lumen.mobile.data.SecureMedia
import com.sety.lumen.mobile.data.SettingsStore
import com.sety.lumen.mobile.data.SyncConflict
import com.sety.lumen.mobile.data.nowIso
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import android.util.Base64
import org.json.JSONObject
import java.io.File
import java.time.Instant
import java.time.LocalDate

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
    suspend fun syncNow(): SyncOutcome = withContext(Dispatchers.IO) {
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
            val pending = db.outbox(100)
            val batch = mutableListOf<JSONObject>()
            var batchBytes = 0L
            val maxBatchBytes = 18L * 1024L * 1024L // ~18 MB raw (~24 MB Base64), well below server 64 MB limit

            suspend fun flushBatch(): String? {
                if (batch.isEmpty()) return null
                val operations = batch.toList()
                batch.clear()
                batchBytes = 0L
                val result = api.push(settings.deviceId, settings.lastSeq, operations)
                db.confirmPush(result.applied, operations)
                val failed = operations.mapNotNull { it.optString("op_id") }.filter { id -> result.errors.any { it.opId == id } }
                db.markOutboxAttempt(failed)
                pushed += result.applied.size
                conflicts += result.conflicts
                if (result.errors.isNotEmpty()) {
                    return "El servidor rechazó ${result.errors.size} operación(es): ${result.errors.first().error}"
                }
                return null
            }

            for (op in orderOps(pending)) {
                val isAsset = op.payload.optString("action") == "asset"
                val fileLen = if (isAsset) {
                    op.payload.optString("local_path").takeIf { it.isNotBlank() }?.let { File(it).length() } ?: 0L
                } else 0L

                if (batch.isNotEmpty() && batchBytes + fileLen > maxBatchBytes) {
                    val err = flushBatch()
                    if (err != null) {
                        pushWarning = err
                        return@runCatching err
                    }
                }
                batch += expandOperation(op.payload).apply { put("object_id", op.objectId) }
                batchBytes += fileLen
            }
            flushBatch()?.also { pushWarning = it }
        }.onFailure { error ->
            return@withContext SyncOutcome(false, "Error al subir cambios: ${error.message}", endpoint)
        }.getOrNull()?.let { message ->
            settings.lastSyncMessage = message
        }

        val pull = runCatching { api.pull(settings.lastSeq, settings.mediaSinceDate) }
            .getOrElse { error ->
                return@withContext SyncOutcome(false, "Error al descargar cambios: ${error.message}", endpoint, pushed)
            }
        db.applyPull(pull)
        settings.lastSeq = pull.headSeq
        runCatching { api.status().serverId }.getOrNull()?.takeIf { it.isNotBlank() }?.let { settings.serverId = it }
        val downloaded = downloadAssets(api, db.assets())
        settings.lastSyncAt = nowIso()
        settings.lastSyncMessage = buildString {
            append("Sincronizado ")
            append(java.time.LocalTime.now().withNano(0))
            if (pushed > 0) append(" · $pushed subida(s)")
            if (pull.objects.size + pull.types.size > 0) append(" · ${pull.objects.size + pull.types.size} cambio(s)")
            if (downloaded > 0) append(" · $downloaded medio(s)")
            if (conflicts.isNotEmpty()) append(" · ${conflicts.size} conflicto(s) resuelto(s)")
            pushWarning?.let { append(" · ").append(it) }
        }
        SyncOutcome(
            success = true,
            message = settings.lastSyncMessage,
            endpoint = endpoint,
            pushed = pushed,
            pulled = pull.objects.size + pull.types.size,
            conflicts = conflicts,
        )
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
                status.headSeq >= 0
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

    /**
     * Orders pending operations so that objects appear before the operations
     * that link to them. The server drops relations it cannot resolve yet, so
     * respecting the dependency order here preserves them instead of losing
     * them on the first pass. Cycles are appended as-is: the server breaks the
     * tie by dropping one direction.
     */
    private fun orderOps(pending: List<OutboxOp>): List<OutboxOp> {
        val linksOf: (OutboxOp) -> Set<String> = { op ->
            op.payload.optJSONObject("object")?.optJSONArray("links")
                ?.let { arr -> (0 until arr.length()).mapNotNull { arr.optString(it) }.toSet() }
                ?: emptySet()
        }
        val ids = pending.map { it.objectId }.toSet()
        val ordered = mutableListOf<OutboxOp>()
        val done = mutableSetOf<String>()
        val rest = pending.toMutableList()
        while (rest.isNotEmpty()) {
            val ready = rest.filter { op -> linksOf(op).all { it in done || it !in ids } }
            if (ready.isEmpty()) {
                ordered += rest
                break
            }
            ordered += ready
            done += ready.map { it.objectId }
            rest -= ready
        }
        return ordered
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
