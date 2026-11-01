package com.sety.lumen.mobile.util

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.net.Uri
import androidx.exifinterface.media.ExifInterface
import java.io.ByteArrayOutputStream
import java.io.File

data class ProcessedImage(
    val bytes: ByteArray,
    val thumbBytes: ByteArray,
    val mime: String = "image/jpeg",
    val width: Int,
    val height: Int,
)

object ImageTools {
    fun processUri(context: Context, uri: Uri): ProcessedImage {
        val rawBytes = context.contentResolver.openInputStream(uri)?.use { it.readBytes() }
            ?: throw IllegalArgumentException("No se pudo leer la imagen")
        val mimeType = context.contentResolver.getType(uri)?.takeIf { it.isNotBlank() } ?: "image/jpeg"
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(rawBytes, 0, rawBytes.size, bounds)
        require(bounds.outWidth > 0 && bounds.outHeight > 0) { "Dimensiones de imagen no válidas" }

        val rotation = context.contentResolver.openInputStream(uri)?.use { input ->
            runCatching { ExifInterface(input).rotationDegrees }.getOrDefault(0)
        } ?: 0

        return buildProcessedImage(rawBytes, mimeType, bounds.outWidth, bounds.outHeight, rotation)
    }

    fun processFile(file: File): ProcessedImage {
        val rawBytes = file.readBytes()
        val mimeType = when {
            file.name.endsWith(".png", true) -> "image/png"
            file.name.endsWith(".webp", true) -> "image/webp"
            file.name.endsWith(".gif", true) -> "image/gif"
            else -> "image/jpeg"
        }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(rawBytes, 0, rawBytes.size, bounds)
        require(bounds.outWidth > 0 && bounds.outHeight > 0) { "Dimensiones de imagen no válidas" }

        val rotation = runCatching { ExifInterface(file).rotationDegrees }.getOrDefault(0)
        return buildProcessedImage(rawBytes, mimeType, bounds.outWidth, bounds.outHeight, rotation)
    }

    private fun buildProcessedImage(
        rawBytes: ByteArray,
        mimeType: String,
        origWidth: Int,
        origHeight: Int,
        rotation: Int,
    ): ProcessedImage {
        val (fullBytes, finalWidth, finalHeight) = if (rotation != 0) {
            val decodeOpts = BitmapFactory.Options().apply {
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            val bmp = BitmapFactory.decodeByteArray(rawBytes, 0, rawBytes.size, decodeOpts)
            if (bmp != null) {
                val matrix = Matrix().apply { postRotate(rotation.toFloat()) }
                val rotated = Bitmap.createBitmap(bmp, 0, 0, bmp.width, bmp.height, matrix, true)
                val out = ByteArrayOutputStream()
                if (mimeType.contains("png", ignoreCase = true)) {
                    rotated.compress(Bitmap.CompressFormat.PNG, 100, out)
                } else {
                    rotated.compress(Bitmap.CompressFormat.JPEG, 100, out)
                }
                val bytes = out.toByteArray()
                val w = rotated.width
                val h = rotated.height
                if (rotated !== bmp) rotated.recycle()
                bmp.recycle()
                Triple(bytes, w, h)
            } else {
                Triple(rawBytes, origWidth, origHeight)
            }
        } else {
            // Keep full 100% original untouched bytes when no rotation is needed
            Triple(rawBytes, origWidth, origHeight)
        }

        // Generate high-resolution thumbnail (1080px) for smooth card browsing
        val thumbBytes = generateThumbnail(fullBytes, finalWidth, finalHeight, 1080)

        return ProcessedImage(
            bytes = fullBytes,
            thumbBytes = thumbBytes,
            mime = mimeType,
            width = finalWidth,
            height = finalHeight,
        )
    }

    private fun generateThumbnail(bytes: ByteArray, width: Int, height: Int, maxDimension: Int = 1080): ByteArray {
        val sample = sampleSize(width, height, maxDimension)
        val opts = BitmapFactory.Options().apply {
            inSampleSize = sample
            inPreferredConfig = Bitmap.Config.ARGB_8888
        }
        val bmp = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, opts) ?: return bytes
        val scaled = scaleDown(bmp, maxDimension)
        val out = ByteArrayOutputStream()
        scaled.compress(Bitmap.CompressFormat.JPEG, 85, out)
        if (scaled !== bmp) scaled.recycle()
        bmp.recycle()
        return out.toByteArray()
    }

    fun compressForOg(bytes: ByteArray, maxDimension: Int = 800): ByteArray? {
        val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size) ?: return null
        val scaled = scaleDown(decoded, maxDimension)
        val result = compress(scaled, 85)
        if (scaled !== decoded) scaled.recycle()
        decoded.recycle()
        return result
    }

    fun save(context: Context, name: String, bytes: ByteArray): File {
        val dir = File(context.filesDir, "assets").apply { mkdirs() }
        val file = File(dir, name)
        file.writeBytes(bytes)
        return file
    }

    fun delete(file: File?) {
        runCatching { file?.delete() }
    }

    private fun scaleDown(bitmap: Bitmap, maxDimension: Int): Bitmap {
        val largest = maxOf(bitmap.width, bitmap.height)
        if (largest <= maxDimension) return bitmap
        val ratio = maxDimension.toFloat() / largest
        return Bitmap.createScaledBitmap(bitmap, (bitmap.width * ratio).toInt().coerceAtLeast(1), (bitmap.height * ratio).toInt().coerceAtLeast(1), true)
    }

    private fun compress(bitmap: Bitmap, quality: Int): ByteArray {
        val out = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.JPEG, quality, out)
        return out.toByteArray()
    }

    private fun sampleSize(width: Int, height: Int, maxDimension: Int): Int {
        var sample = 1
        var largest = maxOf(width, height)
        while (largest / 2 >= maxDimension) {
            largest /= 2
            sample *= 2
        }
        return sample
    }
}
