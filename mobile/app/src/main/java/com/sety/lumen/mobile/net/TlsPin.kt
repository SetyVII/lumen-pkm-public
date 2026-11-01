package com.sety.lumen.mobile.net

import com.sety.lumen.mobile.data.SettingsStore
import okhttp3.OkHttpClient
import java.security.MessageDigest
import java.security.SecureRandom
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.X509TrustManager

/**
 * Trust-on-first-use certificate pinning for the self-signed LAN certificate.
 * Pairing can pre-fill the SHA-256 fingerprint from the QR, which removes the
 * first-use window. HTTP (USB/localhost) is handled by the normal client.
 */
object TlsPin {
    fun client(settings: SettingsStore, url: String = settings.serverUrl): OkHttpClient {
        val builder = OkHttpClient.Builder()
            .connectTimeout(4, TimeUnit.SECONDS)
            .readTimeout(30, TimeUnit.SECONDS)
            .writeTimeout(30, TimeUnit.SECONDS)
            .followRedirects(false)
        if (url.startsWith("https://")) {
            val trust = TofuTrustManager(settings)
            val context = SSLContext.getInstance("TLS").apply {
                init(null, arrayOf<X509TrustManager>(trust), SecureRandom())
            }
            builder.sslSocketFactory(context.socketFactory, trust)
            builder.hostnameVerifier { _, _ -> true }
        }
        return builder.build()
    }

    fun fingerprint(certificate: X509Certificate): String =
        MessageDigest.getInstance("SHA-256").digest(certificate.encoded)
            .joinToString("") { "%02x".format(it) }

    private class TofuTrustManager(private val settings: SettingsStore) : X509TrustManager {
        override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) = Unit

        override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {
            val leaf = chain?.firstOrNull() ?: throw CertificateException("El servidor no presentó un certificado")
            val actual = fingerprint(leaf)
            val expected = settings.certFingerprint
            if (expected.isBlank()) {
                settings.certFingerprint = actual
                return
            }
            if (!expected.equals(actual, ignoreCase = true)) {
                throw CertificateException("La huella TLS del servidor cambió. Vuelve a emparejar el dispositivo.")
            }
        }

        override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
    }
}
