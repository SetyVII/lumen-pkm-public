package com.sety.lumen.mobile.data

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * Two hardware-backed wrappings around the 32-byte workspace key:
 *  - password -> PBKDF2 -> AES-GCM, for explicit unlock;
 *  - a device-unlocked Keystore key, so WorkManager/widgets can sync without
 *    asking for the password every time the screen turns on.
 * Biometric unlock uses a third key that requires strong authentication.
 */
class KeyVault(private val context: Context, val settings: SettingsStore) {
    private var master: ByteArray? = null
    private val lockListeners = java.util.concurrent.CopyOnWriteArrayList<() -> Unit>()
    internal fun onLock(listener: () -> Unit) { lockListeners += listener }

    val isSetup: Boolean get() = settings.wrappedMaster.isNotBlank()
    val isUnlocked: Boolean get() = master != null

    fun setup(password: CharArray): ByteArray {
        require(password.size >= 8) { "La contraseña debe tener al menos 8 caracteres" }
        val salt = CryptoBox.randomBytes(16)
        val key = CryptoBox.randomBytes(32)
        val derived = CryptoBox.deriveKey(password, salt)
        settings.salt = Base64.encodeToString(salt, Base64.NO_WRAP)
        settings.wrappedMaster = Base64.encodeToString(CryptoBox.encrypt(derived, key, "lumen-master"), Base64.NO_WRAP)
        settings.deviceWrappedMaster = CryptoBox.encryptText(deviceKey(), Base64.encodeToString(key, Base64.NO_WRAP), "lumen-device-master")
        settings.ensureMediaWindow()
        master = key
        return key
    }

    fun unlock(password: CharArray): Boolean {
        val salt = runCatching { Base64.decode(settings.salt, Base64.NO_WRAP) }.getOrNull() ?: return false
        val wrapped = runCatching { Base64.decode(settings.wrappedMaster, Base64.NO_WRAP) }.getOrNull() ?: return false
        return runCatching {
            val derived = CryptoBox.deriveKey(password, salt)
            val key = CryptoBox.decrypt(derived, wrapped, "lumen-master")
            if (key.size != 32) return false
            master = key
            if (settings.deviceWrappedMaster.isBlank()) {
                settings.deviceWrappedMaster = CryptoBox.encryptText(deviceKey(), Base64.encodeToString(key, Base64.NO_WRAP), "lumen-device-master")
            }
            true
        }.getOrDefault(false)
    }

    /** Used by WorkManager and the widget when the device is unlocked. */
    fun unlockWithDevice(): Boolean {
        if (master != null) return true
        val wrapped = settings.deviceWrappedMaster
        if (wrapped.isBlank()) return false
        return runCatching {
            val encoded = CryptoBox.decryptText(deviceKey(), wrapped, "lumen-device-master")
            val key = Base64.decode(encoded, Base64.NO_WRAP)
            if (key.size != 32) return false
            master = key
            true
        }.getOrDefault(false)
    }

    fun lock() {
        master?.fill(0)
        master = null
        lockListeners.forEach { it() }
    }

    fun masterOrNull(): ByteArray? = master

    fun requireMaster(): ByteArray = master ?: throw IllegalStateException("Lumen está bloqueado")

    fun createBiometricCipher(): Cipher {
        val iv = Base64.decode(settings.biometricIv, Base64.NO_WRAP)
        val key = getKey(BIOMETRIC_ALIAS) ?: throw IllegalStateException("No hay clave biométrica")
        return Cipher.getInstance("AES/GCM/NoPadding").apply {
            init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, iv))
        }
    }

    /** Creates the auth-bound key and returns an ENCRYPT cipher for BiometricPrompt. */
    fun prepareBiometricEncryption(): Cipher {
        deleteKey(BIOMETRIC_ALIAS)
        val key = createKey(BIOMETRIC_ALIAS, authRequired = true, unlockedDeviceRequired = false)
        return Cipher.getInstance("AES/GCM/NoPadding").apply {
            init(Cipher.ENCRYPT_MODE, key)
        }
    }

    /** Called only after BiometricPrompt authorized the encryption cipher. */
    fun completeBiometricEnrollment(cipher: Cipher): Boolean {
        val keyBytes = master ?: return false
        return runCatching {
            val encrypted = cipher.doFinal(keyBytes)
            settings.biometricIv = Base64.encodeToString(cipher.iv, Base64.NO_WRAP)
            settings.biometricWrappedMaster = Base64.encodeToString(encrypted, Base64.NO_WRAP)
            settings.biometricEnabled = true
            true
        }.onFailure { android.util.Log.e("LumenKeyVault", "No se pudo completar la inscripción biométrica", it) }.getOrDefault(false)
    }

    fun unlockWithBiometricCipher(cipher: Cipher): Boolean {
        if (master != null) return true
        val wrapped = runCatching { Base64.decode(settings.biometricWrappedMaster, Base64.NO_WRAP) }.getOrNull() ?: return false
        return runCatching {
            val key = cipher.doFinal(wrapped)
            if (key.size != 32) return false
            master = key
            true
        }.getOrDefault(false)
    }

    fun disableBiometric() {
        settings.biometricEnabled = false
        settings.biometricWrappedMaster = ""
        settings.biometricIv = ""
        deleteKey(BIOMETRIC_ALIAS)
    }

    fun encryptToken(token: String): String = CryptoBox.encryptText(deviceKey(), token, "lumen-token")

    fun decryptToken(): String {
        val encoded = settings.tokenEncrypted
        if (encoded.isBlank()) return ""
        return runCatching { CryptoBox.decryptText(deviceKey(), encoded, "lumen-token") }.getOrDefault("")
    }

    fun deviceKey(): SecretKey = getKey(DEVICE_ALIAS) ?: createKey(DEVICE_ALIAS, authRequired = false, unlockedDeviceRequired = true)

    private fun getKey(alias: String): SecretKey? {
        val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        return keyStore.getKey(alias, null) as? SecretKey
    }

    private fun createKey(alias: String, authRequired: Boolean, unlockedDeviceRequired: Boolean): SecretKey {
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        val builder = KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setKeySize(256)
            .setUserAuthenticationRequired(authRequired)
        if (authRequired) {
            builder.setUserAuthenticationParameters(0, KeyProperties.AUTH_BIOMETRIC_STRONG)
            builder.setInvalidatedByBiometricEnrollment(true)
        }
        if (unlockedDeviceRequired) {
            runCatching { builder.setUnlockedDeviceRequired(true) }
        }
        return runCatching {
            generator.init(builder.build())
            generator.generateKey()
        }.getOrElse {
            if (!unlockedDeviceRequired) throw it
            // Some OEM Keystores reject unlockedDeviceRequired. Retry without it
            // rather than leaving background sync permanently broken.
            val fallback = KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .setUserAuthenticationRequired(authRequired)
                .build()
            generator.init(fallback)
            generator.generateKey()
        }
    }

    private fun deleteKey(alias: String) {
        runCatching {
            val keyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            if (keyStore.containsAlias(alias)) keyStore.deleteEntry(alias)
        }
    }

    companion object {
        private const val DEVICE_ALIAS = "lumen-device"
        private const val BIOMETRIC_ALIAS = "lumen-biometric"
    }
}
