package com.sety.lumen.mobile.sync

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import com.sety.lumen.mobile.LumenApplication

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED && intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        val app = context.applicationContext as LumenApplication
        SyncScheduler.schedulePeriodic(context)
        if (app.settings.journalReminder) SyncScheduler.scheduleDaily(context)
    }
}
