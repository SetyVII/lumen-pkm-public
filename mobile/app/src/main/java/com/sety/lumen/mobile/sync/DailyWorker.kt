package com.sety.lumen.mobile.sync

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import com.sety.lumen.mobile.LumenApplication
import com.sety.lumen.mobile.data.todayIso

class DailyWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        val app = applicationContext as LumenApplication
        if (!app.vault.isUnlocked) app.vault.unlockWithDevice()
        if (!app.vault.isUnlocked) return Result.success()
        val today = todayIso()
        app.repository.widgetTasks()
            .filter { !it.completed && (it.dueDate.isBlank() || it.dueDate == today) }
            .forEach { Notifications.showTaskDue(applicationContext, it) }
        if (app.settings.journalReminder) {
            val journal = app.repository.journal(today)
            if (journal == null || (journal.content.isBlank() && journal.links.isEmpty())) {
                Notifications.showJournalReminder(applicationContext)
            }
        }
        return Result.success()
    }
}
