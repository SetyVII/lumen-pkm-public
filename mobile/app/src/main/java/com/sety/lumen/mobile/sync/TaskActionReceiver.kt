package com.sety.lumen.mobile.sync

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.sety.lumen.mobile.LumenApplication
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

class TaskActionReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val app = context.applicationContext as LumenApplication
        if (!app.vault.isUnlocked) app.vault.unlockWithDevice()
        val taskId = intent.getStringExtra(EXTRA_TASK_ID) ?: return
        val pending = goAsync()
        CoroutineScope(Dispatchers.IO).launch {
            try {
                when (intent.action) {
                    ACTION_COMPLETE -> app.repository.toggleTask(taskId, true)
                    ACTION_POSTPONE -> app.repository.postponeTask(taskId)
                }
                Notifications.cancelTask(context, taskId)
                SyncScheduler.scheduleImmediate(context)
            } finally {
                pending.finish()
            }
        }
    }

    companion object {
        const val ACTION_COMPLETE = "com.sety.lumen.mobile.COMPLETE_TASK"
        const val ACTION_POSTPONE = "com.sety.lumen.mobile.POSTPONE_TASK"
        const val EXTRA_TASK_ID = "task_id"
    }
}
