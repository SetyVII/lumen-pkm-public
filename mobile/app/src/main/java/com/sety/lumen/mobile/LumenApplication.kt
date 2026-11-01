package com.sety.lumen.mobile

import android.app.Application
import com.sety.lumen.mobile.data.KeyVault
import com.sety.lumen.mobile.data.LumenDatabase
import com.sety.lumen.mobile.data.LumenRepository
import com.sety.lumen.mobile.data.SecureMedia
import com.sety.lumen.mobile.data.SettingsStore
import com.sety.lumen.mobile.net.WifiWatcher
import com.sety.lumen.mobile.sync.Notifications
import com.sety.lumen.mobile.sync.SyncScheduler

class LumenApplication : Application() {
    lateinit var settings: SettingsStore
        private set
    lateinit var vault: KeyVault
        private set
    lateinit var database: LumenDatabase
        private set
    lateinit var repository: LumenRepository
        private set

    private var wifiWatcher: WifiWatcher? = null

    override fun onCreate() {
        super.onCreate()
        settings = SettingsStore(this)
        vault = KeyVault(this, settings)
        database = LumenDatabase(this, vault)
        repository = LumenRepository(this, database, vault, settings)
        SecureMedia.clearShared(this)
        Notifications.createChannels(this)
        SyncScheduler.schedulePeriodic(this)
        if (settings.journalReminder) SyncScheduler.scheduleDaily(this)
        wifiWatcher = WifiWatcher(this) {
            if (settings.autoSync && settings.hasConnection()) {
                SyncScheduler.scheduleImmediate(this)
            }
        }.also { it.start() }
    }
}
