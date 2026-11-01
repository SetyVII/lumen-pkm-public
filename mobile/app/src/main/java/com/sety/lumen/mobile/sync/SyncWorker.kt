package com.sety.lumen.mobile.sync

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import com.sety.lumen.mobile.LumenApplication

class SyncWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val app = applicationContext as LumenApplication
        if (!app.settings.hasConnection()) return Result.success()
        val outcome = app.repository.syncNow()
        return if (outcome.success) {
            Notifications.notifySyncResult(applicationContext, outcome.message)
            Result.success()
        } else if (runAttemptCount < 3) {
            Result.retry()
        } else {
            Notifications.notifySyncError(applicationContext, outcome.message)
            Result.failure()
        }
    }
}
