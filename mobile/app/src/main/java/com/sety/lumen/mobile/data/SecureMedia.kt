package com.sety.lumen.mobile.data

import android.content.Context
import java.io.File

/**
 * Media files at rest are sealed with the workspace master key using the same
 * AES-256-GCM container as database payloads: [nonce || ciphertext || tag].
 * The associated data binds each file to its own file name, so a file cannot
 * be swapped for another one. Plaintext only exists in RAM while the media is
 * displayed or uploaded, and briefly in the private cache when the user shares
 * or copies an image.
 *
 * Files written by versions prior to 0.6.1 were plaintext. They stay readable
 * and are re-sealed in place the first time they are read while the vault is
 * unlocked.
 */
object SecureMedia {
    private val LEGACY_MAGIC = listOf(
        byteArrayOf(0xFF.toByte(), 0xD8.toByte()),          // JPEG
        byteArrayOf(0x89.toByte(), 0x50, 0x4E, 0x47),       // PNG
        byteArrayOf(0x52, 0x49, 0x46, 0x46),                // WEBP (RIFF)
        byteArrayOf(0x47, 0x49, 0x46, 0x38),                // GIF
        byteArrayOf(0x25, 0x50, 0x44, 0x46),                // PDF
    )

    /** Seals [bytes] with the master key and writes them to [file]. */
    fun save(vault: KeyVault, file: File, bytes: ByteArray) {
        val sealed = CryptoBox.encrypt(vault.requireMaster(), bytes, aad(file))
        file.parentFile?.mkdirs()
        file.writeBytes(sealed)
    }

    /**
     * Decrypts a sealed media file. Legacy plaintext files are detected by
     * their magic bytes, returned as-is and re-sealed while the vault is
     * unlocked. Returns null when the file is missing, the vault is locked or
     * the payload is corrupt.
     */
    fun read(vault: KeyVault, path: String): ByteArray? {
        if (path.isBlank()) return null
        val file = File(path)
        if (!file.exists()) return null
        if (!vault.isUnlocked) return null
        val key = runCatching { vault.requireMaster() }.getOrNull() ?: return null
        val raw = runCatching { file.readBytes() }.getOrNull() ?: return null
        if (raw.isEmpty()) return null
        val plain = runCatching { CryptoBox.decrypt(key, raw, aad(file)) }.getOrNull()
        if (plain != null) return plain
        if (isLegacyMedia(raw)) {
            runCatching { save(vault, file, raw) }
            return raw
        }
        return null
    }

    /**
     * Cheap synchronous check used by the UI to decide the initial Coil model:
     * plaintext legacy files can be handed to Coil as a File immediately,
     * sealed files wait for the asynchronous decryption.
     */
    fun peekLegacy(path: String): Boolean {
        if (path.isBlank()) return false
        val file = File(path)
        if (!file.exists()) return false
        val head = runCatching { file.inputStream().use { it.readNBytes(4) } }.getOrNull() ?: return false
        return isLegacyMedia(head)
    }

    /**
     * Writes a decrypted copy into the private cache so copy/share intents can
     * hand a real file to FileProvider. The cache directory is wiped on the
     * next app start.
     */
    fun tempPlain(context: Context, vault: KeyVault, path: String): File? {
        val bytes = read(vault, path) ?: return null
        val dir = File(context.cacheDir, "shared").apply { mkdirs() }
        val out = File(dir, File(path).name)
        out.writeBytes(bytes)
        return out
    }

    /** Removes decrypted copies left behind by share/copy intents. */
    fun clearShared(context: Context) {
        runCatching { File(context.cacheDir, "shared").deleteRecursively() }
    }

    private fun aad(file: File): String = "media/${file.name}"

    private fun isLegacyMedia(bytes: ByteArray): Boolean =
        LEGACY_MAGIC.any { magic ->
            bytes.size >= magic.size && magic.indices.all { bytes[it] == magic[it] }
        }
}
