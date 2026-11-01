package com.sety.lumen.mobile.util

import java.net.URI
import java.net.URLDecoder
import java.net.URLEncoder

/**
 * Kotlin port of the server's cleanURL. Keeping both implementations aligned is
 * what makes duplicate detection and offline captures deterministic.
 */
object UrlCleaner {
    private val tracking = setOf(
        "fbclid", "gclid", "si", "igshid", "ref", "feature", "mc_cid", "mc_eid",
        "access_token", "token", "api_key", "apikey", "authorization", "password", "secret",
    )

    fun clean(raw: String): String {
        val trimmed = raw.trim()
        require(trimmed.isNotEmpty() && trimmed.length <= 8192) { "El enlace está vacío o es demasiado largo" }
        val uri = runCatching { URI(trimmed) }.getOrElse { throw IllegalArgumentException("El enlace no es válido") }
        val scheme = (uri.scheme ?: "").lowercase()
        require(scheme == "http" || scheme == "https") { "Solo se admiten enlaces HTTP o HTTPS" }
        require(uri.userInfo == null) { "No guardes enlaces con usuario o contraseña" }
        var host = (uri.host ?: "").lowercase()
        require(host.isNotBlank()) { "El enlace no tiene un dominio válido" }
        var path = uri.path ?: ""
        val query = parseQuery(uri.rawQuery)
        query.keys.filter { isTracking(it) }.forEach(query::remove)

        when (host) {
            "youtu.be" -> {
                val id = path.trim('/')
                if (id.isNotBlank()) {
                    host = "www.youtube.com"
                    path = "/watch"
                    query["v"] = mutableListOf(id)
                }
            }
            "youtube.com", "www.youtube.com", "m.youtube.com" -> {
                if (path.startsWith("/shorts/")) {
                    val id = path.removePrefix("/shorts/").trim('/')
                    if (id.isNotBlank()) {
                        path = "/watch"
                        query["v"] = mutableListOf(id)
                    }
                }
            }
            "redd.it" -> {
                val id = path.trim('/')
                if (id.isNotBlank()) {
                    host = "www.reddit.com"
                    path = "/comments/$id"
                }
            }
        }

        val port = when {
            scheme == "http" && uri.port == 80 -> -1
            scheme == "https" && uri.port == 443 -> -1
            else -> uri.port
        }
        val base = URI(scheme, null, host, port, path, null, null).toASCIIString()
        val encoded = encodeQuery(query)
        return if (encoded.isBlank()) base else "$base?$encoded"
    }

    fun cleanOrNull(raw: String): String? = runCatching { clean(raw) }.getOrNull()

    fun isReddit(raw: String): Boolean {
        val host = runCatching { URI(raw).host?.lowercase() }.getOrNull() ?: return false
        return host == "reddit.com" || host == "www.reddit.com" || host.endsWith(".reddit.com") || host == "redd.it"
    }

    fun isRedditShort(raw: String): Boolean =
        isReddit(raw) && runCatching { URI(raw).path?.contains("/s/") == true }.getOrDefault(false)

    fun hostOf(raw: String): String = runCatching { URI(raw).host ?: "" }.getOrDefault("")

    private fun isTracking(key: String): Boolean {
        val lower = key.lowercase()
        return tracking.contains(lower) || lower.startsWith("utm_")
    }

    private fun parseQuery(raw: String?): MutableMap<String, MutableList<String>> {
        val out = linkedMapOf<String, MutableList<String>>()
        if (raw.isNullOrBlank()) return out
        raw.split('&').forEach { part ->
            if (part.isBlank()) return@forEach
            val index = part.indexOf('=')
            val key = decode(if (index >= 0) part.substring(0, index) else part)
            val value = decode(if (index >= 0) part.substring(index + 1) else "")
            if (key.isBlank()) return@forEach
            out.getOrPut(key) { mutableListOf() }.add(value)
        }
        return out
    }

    private fun encodeQuery(query: Map<String, List<String>>): String {
        val parts = mutableListOf<String>()
        query.keys.sorted().forEach { key ->
            query[key].orEmpty().forEach { value ->
                parts += encode(key) + "=" + encode(value)
            }
        }
        return parts.joinToString("&")
    }

    private fun decode(value: String): String =
        runCatching { URLDecoder.decode(value, Charsets.UTF_8.name()) }.getOrDefault(value)

    private fun encode(value: String): String =
        URLEncoder.encode(value, Charsets.UTF_8.name()).replace("+", "%20")
}
