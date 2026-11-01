package com.sety.lumen.mobile.widget

import android.content.Context
import androidx.glance.GlanceId
import androidx.glance.action.ActionParameters
import androidx.glance.appwidget.action.ActionCallback
import androidx.glance.appwidget.updateAll
import com.sety.lumen.mobile.LumenApplication

class SyncWidgetAction : ActionCallback {
    override suspend fun onAction(context: Context, glanceId: GlanceId, parameters: ActionParameters) {
        val app = context.applicationContext as LumenApplication
        app.repository.syncNow()
        LumenWidget().updateAll(context)
    }
}

class ToggleTaskAction : ActionCallback {
    override suspend fun onAction(context: Context, glanceId: GlanceId, parameters: ActionParameters) {
        val app = context.applicationContext as LumenApplication
        if (!app.vault.isUnlocked) app.vault.unlockWithDevice()
        val taskId = parameters[TASK_ID_KEY] ?: return
        app.repository.toggleTask(taskId, true)
        LumenWidget().updateAll(context)
    }

    companion object {
        val TASK_ID_KEY = ActionParameters.Key<String>("taskId")
    }
}
