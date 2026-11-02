package com.sety.lumen.mobile.net

import com.sety.lumen.mobile.data.DeletedRecord
import com.sety.lumen.mobile.data.LumenAsset
import com.sety.lumen.mobile.data.LumenObject
import com.sety.lumen.mobile.data.LumenType
import com.sety.lumen.mobile.data.SettingsStore
import com.sety.lumen.mobile.data.SyncConflict
import com.sety.lumen.mobile.data.SyncOpError
import com.sety.lumen.mobile.data.decodeSyncPullPage
import com.sety.lumen.mobile.data.SyncPullResult
import com.sety.lumen.mobile.data.SyncPushResult
import com.sety.lumen.mobile.data.SyncReceipt
import com.sety.lumen.mobile.data.SyncStatus
import com.sety.lumen.mobile.data.Tombstone
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream

class ApiException(val statusCode: Int, message: String) : Exception(message)

class LumenApi(
    private val settings: SettingsStore,
    private val token: String,
    private val baseUrl: String = settings.serverUrl,
) {
    private val jsonType = "application/json; charset=utf-8".toMediaType()
    private val client: OkHttpClient = TlsPin.client(settings, baseUrl)
    private val root = baseUrl.trimEnd('/')

    fun status(): SyncStatus {
        val raw = execute(Request.Builder().url("$root/v1/sync/status").get().build())
        val json = JSONObject(raw)
        return SyncStatus(
            serverId = json.optString("server_id"),
            headSeq = json.optLong("head_seq"),
            pendingLogseq = json.optInt("pending_logseq"),
            protocol = json.optString("protocol"),
            tlsFingerprint = json.optString("tls_fingerprint"),
        )
    }

    fun pull(sinceSeq: Long, mediaSinceDate: String): SyncPullResult {
        val body = JSONObject().apply {
            put("since_seq", sinceSeq)
            if (mediaSinceDate.isNotBlank()) put("media_since_date", mediaSinceDate)
        }
        val raw = execute(
            Request.Builder().url("$root/v1/sync/pull")
                .post(body.toString().toRequestBody(jsonType))
                .build()
        )
        return decodeSyncPullPage(JSONObject(raw))
    }

    fun push(deviceId: String, baseSeq: Long, operations: List<JSONObject>): SyncPushResult {
        val body = JSONObject().apply {
            put("device_id", deviceId)
            put("base_seq", baseSeq)
            put("operations", JSONArray(operations))
        }
        val raw = execute(
            Request.Builder().url("$root/v1/sync/push")
                .post(body.toString().toRequestBody(jsonType))
                .build()
        )
        val json = JSONObject(raw)
        val applied = mutableListOf<String>()
        json.optJSONArray("applied")?.let { array ->
            for (i in 0 until array.length()) applied += array.optString(i)
        }
        val conflicts = mutableListOf<SyncConflict>()
        json.optJSONArray("conflicts")?.forEachObject {
            conflicts += SyncConflict(it.optString("op_id"), it.optString("object_id"), it.optString("resolution"))
        }
        val errors = mutableListOf<SyncOpError>()
        json.optJSONArray("errors")?.forEachObject {
            errors += SyncOpError(it.optString("op_id"), it.optString("error"))
        }
        val receipts = mutableListOf<SyncReceipt>()
        json.optJSONArray("receipts")?.forEachObject {
            receipts += SyncReceipt(it.optString("op_id"), it.optString("object_id"), it.optLong("revision"), if(it.has("previous_revision")&&!it.isNull("previous_revision")) it.getLong("previous_revision") else null)
        }
        return SyncPushResult(json.optLong("head_seq"), applied, conflicts, errors, receipts)
    }

    /** Tipos registrados en el servidor. Los personalizados del teléfono que
     *  falten aquí se crean con [createType] antes de subir objetos. */
    fun types(): List<LumenType> {
        val raw = execute(Request.Builder().url("$root/v1/types").get().build())
        val out = mutableListOf<LumenType>()
        JSONArray(raw).forEachObject { out += LumenType.fromJson(it) }
        return out
    }

    /** Registra un tipo en el servidor. Con [type.id] ya presente el servidor
     *  responde 200 con su definición y no hay duplicados. */
    fun createType(type: LumenType): LumenType {
        val raw = execute(
            Request.Builder().url("$root/v1/types")
                .post(type.toJson().toString().toRequestBody(jsonType))
                .build()
        )
        return LumenType.fromJson(JSONObject(raw))
    }

    fun downloadAsset(assetId: String, destination: File): File {
        val request = Request.Builder().url("$root/v1/assets/$assetId/content")
            .header("Authorization", "Bearer $token")
            .get()
            .build()
        client.newCall(request.newBuilder().header("X-Lumen-Document-Version", "2").build()).execute().use { response ->
            if (!response.isSuccessful) throw ApiException(response.code, "No se pudo descargar el adjunto (${response.code})")
            val body = response.body ?: throw ApiException(0, "Respuesta vacía")
            destination.parentFile?.mkdirs()
            body.byteStream().use { input ->
                FileOutputStream(destination).use { output -> input.copyTo(output) }
            }
        }
        return destination
    }

    private fun execute(request: Request): String {
        val authorized = request.newBuilder()
            .header("Authorization", "Bearer $token")
            .header("Accept", "application/json")
            .header("X-Lumen-Document-Version", "2")
            .header("Cache-Control", "no-store")
            .build()
        client.newCall(authorized).execute().use { response ->
            val body = response.body?.string().orEmpty()
            if (!response.isSuccessful) {
                val message = runCatching { JSONObject(body).optString("error") }.getOrDefault("")
                throw ApiException(response.code, message.ifBlank { "El servidor respondió HTTP ${response.code}" })
            }
            return body
        }
    }
}

private inline fun JSONArray.forEachObject(block: (JSONObject) -> Unit) {
    for (i in 0 until length()) optJSONObject(i)?.let(block)
}
