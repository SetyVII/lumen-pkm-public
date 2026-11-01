package com.sety.lumen.mobile.data

import android.util.Base64
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.SecretKey
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

/**
 * AES-256-GCM with the same 12-byte nonce prefix used by the Go server:
 * [nonce || ciphertext || tag]. The associated data binds a payload to its
 * logical kind/id, so a record cannot be swapped for another one.
 */
object CryptoBox {
    private const val NONCE_BYTES = 12
    private const val TAG_BITS = 128
    private const val ITERATIONS = 210_000

    fun randomBytes(size: Int): ByteArray = ByteArray(size).also { SecureRandom().nextBytes(it) }

    fun deriveKey(password: CharArray, salt: ByteArray): ByteArray {
        val spec = PBEKeySpec(password, salt, ITERATIONS, 256)
        return try {
            SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded
        } finally {
            spec.clearPassword()
        }
    }

    fun encrypt(key: ByteArray, plaintext: ByteArray, aad: String): ByteArray =
        encrypt(SecretKeySpec(key, "AES"), plaintext, aad)

    fun encrypt(key: SecretKey, plaintext: ByteArray, aad: String): ByteArray {
        // Android Keystore keys reject caller-provided IVs for encryption. Let
        // the provider generate the 12-byte GCM nonce and prefix it, exactly like
        // the raw-key path and the Go server format.
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key)
        cipher.updateAAD(aad.toByteArray(Charsets.UTF_8))
        return cipher.iv + cipher.doFinal(plaintext)
    }

    fun decrypt(key: ByteArray, payload: ByteArray, aad: String): ByteArray =
        decrypt(SecretKeySpec(key, "AES"), payload, aad)

    fun decrypt(key: SecretKey, payload: ByteArray, aad: String): ByteArray {
        require(payload.size > NONCE_BYTES) { "invalid encrypted payload" }
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(TAG_BITS, payload.copyOfRange(0, NONCE_BYTES)))
        cipher.updateAAD(aad.toByteArray(Charsets.UTF_8))
        return cipher.doFinal(payload.copyOfRange(NONCE_BYTES, payload.size))
    }

    fun encryptText(key: ByteArray, text: String, aad: String): String =
        Base64.encodeToString(encrypt(key, text.toByteArray(Charsets.UTF_8), aad), Base64.NO_WRAP)

    fun decryptText(key: ByteArray, encoded: String, aad: String): String =
        String(decrypt(key, Base64.decode(encoded, Base64.NO_WRAP), aad), Charsets.UTF_8)

    fun encryptText(key: SecretKey, text: String, aad: String): String =
        Base64.encodeToString(encrypt(key, text.toByteArray(Charsets.UTF_8), aad), Base64.NO_WRAP)

    fun decryptText(key: SecretKey, encoded: String, aad: String): String =
        String(decrypt(key, Base64.decode(encoded, Base64.NO_WRAP), aad), Charsets.UTF_8)
}
