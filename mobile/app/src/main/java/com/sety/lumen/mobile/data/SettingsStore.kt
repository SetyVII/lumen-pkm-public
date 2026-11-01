package com.sety.lumen.mobile.data

import android.content.Context
import android.util.Base64
import java.time.LocalDate
import java.util.UUID

class SettingsStore(context: Context) {
    private val prefs = context.getSharedPreferences("lumen.settings", Context.MODE_PRIVATE)

    var salt: String
        get() = prefs.getString("salt", "") ?: ""
        set(value) = prefs.edit().putString("salt", value).apply()
    var wrappedMaster: String
        get() = prefs.getString("wrapped_master", "") ?: ""
        set(value) = prefs.edit().putString("wrapped_master", value).apply()
    var deviceWrappedMaster: String
        get() = prefs.getString("device_wrapped_master", "") ?: ""
        set(value) = prefs.edit().putString("device_wrapped_master", value).apply()
    var biometricWrappedMaster: String
        get() = prefs.getString("biometric_wrapped_master", "") ?: ""
        set(value) = prefs.edit().putString("biometric_wrapped_master", value).apply()
    var biometricIv: String
        get() = prefs.getString("biometric_iv", "") ?: ""
        set(value) = prefs.edit().putString("biometric_iv", value).apply()
    var biometricEnabled: Boolean
        get() = prefs.getBoolean("biometric_enabled", false)
        set(value) = prefs.edit().putBoolean("biometric_enabled", value).apply()

    var serverUrl: String
        get() = prefs.getString("server_url", "http://127.0.0.1:8765") ?: "http://127.0.0.1:8765"
        set(value) = prefs.edit().putString("server_url", value.trimEnd('/')).apply()
    var tokenEncrypted: String
        get() = prefs.getString("token_encrypted", "") ?: ""
        set(value) = prefs.edit().putString("token_encrypted", value).apply()
    var certFingerprint: String
        get() = prefs.getString("cert_fingerprint", "") ?: ""
        set(value) = prefs.edit().putString("cert_fingerprint", value.lowercase().replace(":", "")).apply()
    var serverId: String
        get() = prefs.getString("server_id", "") ?: ""
        set(value) = prefs.edit().putString("server_id", value).apply()
    var lastLanUrl: String
        get() = prefs.getString("last_lan_url", "") ?: ""
        set(value) = prefs.edit().putString("last_lan_url", value).apply()

    val deviceId: String
        get() {
            val existing = prefs.getString("device_id", "") ?: ""
            if (existing.isNotBlank()) return existing
            val generated = "android-" + UUID.randomUUID().toString().take(8)
            prefs.edit().putString("device_id", generated).apply()
            return generated
        }

    var lastSeq: Long
        get() = prefs.getLong("last_seq", 0L)
        set(value) = prefs.edit().putLong("last_seq", value).apply()
    var lastSyncAt: String
        get() = prefs.getString("last_sync_at", "") ?: ""
        set(value) = prefs.edit().putString("last_sync_at", value).apply()
    var lastSyncMessage: String
        get() = prefs.getString("last_sync_message", "") ?: ""
        set(value) = prefs.edit().putString("last_sync_message", value).apply()

    var autoSync: Boolean
        get() = prefs.getBoolean("auto_sync", true)
        set(value) = prefs.edit().putBoolean("auto_sync", value).apply()
    var stripGps: Boolean
        get() = prefs.getBoolean("strip_gps", true)
        set(value) = prefs.edit().putBoolean("strip_gps", value).apply()
    var journalReminder: Boolean
        get() = prefs.getBoolean("journal_reminder", false)
        set(value) = prefs.edit().putBoolean("journal_reminder", value).apply()
    var reminderHour: Int
        get() = prefs.getInt("reminder_hour", 21)
        set(value) = prefs.edit().putInt("reminder_hour", value.coerceIn(0, 23)).apply()

    var mediaSinceDate: String
        get() = prefs.getString("media_since_date", "") ?: ""
        set(value) = prefs.edit().putString("media_since_date", value).apply()

    fun ensureMediaWindow() {
        if (mediaSinceDate.isBlank()) {
            mediaSinceDate = LocalDate.now().minusDays(90).toString()
        }
    }

    fun clear() {
        prefs.edit().clear().apply()
    }

    fun hasConnection(): Boolean = serverUrl.isNotBlank() && tokenEncrypted.isNotBlank()

    fun base64(bytes: ByteArray): String = Base64.encodeToString(bytes, Base64.NO_WRAP)
    fun unbase64(value: String): ByteArray = Base64.decode(value, Base64.NO_WRAP)
}
