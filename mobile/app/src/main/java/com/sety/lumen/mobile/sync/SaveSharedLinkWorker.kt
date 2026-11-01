package com.sety.lumen.mobile.sync

import android.content.Context
import android.widget.Toast
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import androidx.work.workDataOf
import com.sety.lumen.mobile.LumenApplication
import com.sety.lumen.mobile.data.CryptoBox
import com.sety.lumen.mobile.data.OgPreview
import org.json.JSONObject
import com.sety.lumen.mobile.util.OgFetcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.CancellationException

/** Save locally even offline; the next worker downloads a sealed thumbnail. */
class SaveSharedLinkWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val app = applicationContext as LumenApplication
        if (!app.vault.unlockWithDevice()) {
            toast("Desbloquea Lumen para guardar el enlace")
            return@withContext Result.failure()
        }
        try {
            val jobId = inputData.getString("share_id") ?: return@withContext Result.failure()
            val payload = inputData.getString("payload") ?: return@withContext Result.failure()
            val share = JSONObject(CryptoBox.decryptText(app.vault.requireMaster(), payload, "share/$jobId"))
            val obj = app.repository.saveWebLink(share.getString("url"), share.optString("title"), OgPreview(), null)
            toast("Guardado en Weblinks correctamente")
            Result.success(workDataOf("object_id" to obj.id))
        } catch (error: CancellationException) {
            throw error
        } catch (_: Exception) {
            toast("No se pudo guardar el enlace en Weblinks")
            Result.failure()
        }
    }

    private suspend fun toast(message: String) = withContext(Dispatchers.Main) {
        Toast.makeText(applicationContext, message, Toast.LENGTH_SHORT).show()
    }
}

class WebLinkPreviewWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        val app = applicationContext as LumenApplication
        if (!app.vault.unlockWithDevice()) return@withContext Result.retry()
        val id = inputData.getString("object_id") ?: return@withContext Result.failure()
        val obj = app.repository.getObject(id) ?: return@withContext Result.success()
        if (!obj.isWeb || obj.assetId.isNotBlank()) return@withContext Result.success()
        try {
            val preview = OgFetcher.fetch(obj.stringProperty("url"))
            app.repository.applyWebLinkPreview(id, preview)
            if (preview.imageBytes == null && runAttemptCount < 2) Result.retry() else Result.success()
        } catch (error: CancellationException) {
            throw error
        } catch (_: Exception) {
            if (runAttemptCount < 2) Result.retry() else Result.failure()
        }
    }
}
