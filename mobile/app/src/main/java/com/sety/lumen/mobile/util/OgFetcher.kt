package com.sety.lumen.mobile.util

import android.text.Html
import com.sety.lumen.mobile.data.OgPreview
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.net.URI
import java.net.URLDecoder
import java.util.concurrent.TimeUnit

/**
 * Client-side OpenGraph capture. It runs at share time, before the phone reaches
 * home, so a post that disappears later still keeps its local WebP card.
 * The Reddit pipeline is ported from Share-To-Thymer: direct OG, vxreddit embed,
 * then the Arctic Shift archive.
 */
object OgFetcher {
    private val client = OkHttpClient.Builder()
        .connectTimeout(8, TimeUnit.SECONDS)
        .readTimeout(8, TimeUnit.SECONDS)
        .followRedirects(true)
        .followSslRedirects(true)
        .build()

    private val browserHeaders = mapOf(
        "User-Agent" to "Mozilla/5.0 (compatible; open-graph-scraper/6.0.0; +https://www.npmjs.com/package/open-graph-scraper)",
        "Accept" to "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language" to "en-US",
        "Cache-Control" to "max-age=0",
    )
    private val redditHeaders = mapOf(
        "User-Agent" to "Discordbot/2.0",
        "Accept" to "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
        "Accept-Language" to "en-US",
    )

    fun fetch(rawUrl: String): OgPreview {
        var url = UrlCleaner.clean(rawUrl)
        if (UrlCleaner.isRedditShort(url)) {
            url = resolve(url) ?: url
            url = UrlCleaner.cleanOrNull(url) ?: url
        }
        return if (UrlCleaner.isReddit(url)) fetchReddit(url) else fetchGeneric(url)
    }

    private fun fetchGeneric(url: String): OgPreview {
        val page = fetchHtml(url, browserHeaders) ?: return fallback(url)
        var preview = parseOpenGraph(page.first, page.second)
        if (preview.title.isBlank()) preview = preview.copy(title = titleFromUrl(page.second))
        return downloadImage(preview, page.second)
    }

    private fun fetchReddit(url: String): OgPreview {
        var preview = OgPreview()
        fetchHtml(url, browserHeaders)?.let { preview = merge(preview, parseOpenGraph(it.first, it.second)) }
        fetchHtml(toVxRedditUrl(url), redditHeaders)?.let { preview = merge(preview, parseOpenGraph(it.first, url)) }
        if (preview.imageBytes == null && preview.title.isBlank()) {
            preview = merge(preview, arcticShift(url))
        }
        if (preview.title.isBlank() || isBlockedTitle(preview.title)) {
            preview = preview.copy(title = titleFromUrl(url).ifBlank { "Reddit" })
        }
        return downloadImage(preview, url)
    }

    private fun downloadImage(preview: OgPreview, baseUrl: String): OgPreview {
        val image = preview.imageBytes ?: return preview
        val compressed = ImageTools.compressForOg(image) ?: return preview.copy(imageBytes = null)
        return preview.copy(imageBytes = compressed, imageMime = "image/webp", finalUrl = preview.finalUrl.ifBlank { baseUrl })
    }

    private fun fetchHtml(url: String, headers: Map<String, String>): Pair<String, String>? {
        return runCatching {
            val request = Request.Builder().url(url).apply {
                headers.forEach { (key, value) -> header(key, value) }
            }.build()
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) return null
                val body = response.body?.string() ?: return null
                if (body.length > 3_000_000) return null
                body to (response.request.url.toString())
            }
        }.getOrNull()
    }

    private fun resolve(url: String): String? = runCatching {
        val request = Request.Builder().url(url).apply {
            browserHeaders.forEach { (key, value) -> header(key, value) }
        }.build()
        client.newCall(request).execute().use { it.request.url.toString() }
    }.getOrNull()

    private fun parseOpenGraph(html: String, pageUrl: String): OgPreview {
        val meta = mutableMapOf<String, String>()
        val tagRegex = Regex("""<meta\s+[^>]*>""", RegexOption.IGNORE_CASE)
        val attrRegex = Regex("""([A-Za-z:_-]+)\s*=\s*["']([^"']*)["']""")
        tagRegex.findAll(html).forEach { match ->
            val attrs = attrRegex.findAll(match.value).associate {
                it.groupValues[1].lowercase() to Html.fromHtml(it.groupValues[2], Html.FROM_HTML_MODE_LEGACY).toString()
            }
            val key = (attrs["property"] ?: attrs["name"])?.lowercase() ?: return@forEach
            if (key.isNotBlank() && !meta.containsKey(key)) meta[key] = attrs["content"].orEmpty()
        }
        val titleTag = Regex("""<title[^>]*>(.*?)</title>""", setOf(RegexOption.IGNORE_CASE, RegexOption.DOT_MATCHES_ALL))
            .find(html)?.groupValues?.get(1)?.let { Html.fromHtml(it, Html.FROM_HTML_MODE_LEGACY).toString().trim() }.orEmpty()
        val title = first(meta, "og:title", "twitter:title").ifBlank { titleTag }
        val description = first(meta, "og:description", "twitter:description", "description")
        val siteName = first(meta, "og:site_name")
        val imageUrl = first(meta, "og:image:secure_url", "og:image", "twitter:image", "twitter:image:src")
        val image = if (imageUrl.isBlank()) null else fetchImage(resolveUrl(pageUrl, imageUrl))
        return OgPreview(
            title = cleanTitle(title),
            description = description.take(2000),
            siteName = siteName.take(200),
            imageBytes = image,
            finalUrl = pageUrl,
        )
    }

    private fun fetchImage(url: String): ByteArray? = runCatching {
        val request = Request.Builder().url(url).header("Accept", "image/jpeg,image/png,image/webp,image/gif").build()
        client.newCall(request).execute().use { response ->
            if (!response.isSuccessful) return null
            val length = response.body?.contentLength() ?: -1
            if (length > 8_000_000) return null
            val bytes = response.body?.bytes() ?: return null
            if (bytes.size > 8_000_000) null else bytes
        }
    }.getOrNull()

    private fun arcticShift(url: String): OgPreview {
        val id = redditPostId(url) ?: return OgPreview()
        return runCatching {
            val archive = "https://arctic-shift.photon-reddit.com/api/posts/ids?ids=$id"
            val request = Request.Builder().url(archive)
                .header("Accept", "application/json")
                .header("User-Agent", "LumenMobile/0.1 (+public-metadata-only)")
                .build()
            client.newCall(request).execute().use { response ->
                if (!response.isSuccessful) return OgPreview()
                val data = JSONObject(response.body?.string() ?: return OgPreview())
                    .optJSONArray("data")?.optJSONObject(0) ?: return OgPreview()
                val title = data.optString("title").trim()
                if (isBlockedTitle(title)) return OgPreview()
                val image = redditImageUrl(data)
                OgPreview(
                    title = cleanTitle(title),
                    description = data.optString("selftext").trim().take(2000),
                    siteName = "reddit.com",
                    imageBytes = if (image.isBlank()) null else fetchImage(image),
                    finalUrl = url,
                )
            }
        }.getOrDefault(OgPreview())
    }

    private fun redditImageUrl(post: JSONObject): String {
        fun clean(value: String) = value.replace("&amp;", "&").trim()
        fun isImage(value: String): Boolean {
            val lower = value.lowercase()
            return lower.contains("i.redd.it") || lower.contains("preview.redd.it") ||
                lower.endsWith(".jpg") || lower.endsWith(".jpeg") || lower.endsWith(".png") ||
                lower.endsWith(".webp") || lower.endsWith(".gif")
        }
        listOf(
            post.optString("url_overridden_by_dest"),
            post.optString("url"),
        ).map(::clean).firstOrNull { it.isNotBlank() && isImage(it) }?.let { return it }
        post.optJSONObject("preview")?.optJSONArray("images")?.optJSONObject(0)
            ?.optJSONObject("source")?.optString("url")?.let(::clean)?.takeIf { it.isNotBlank() }?.let { return it }
        return clean(post.optString("thumbnail")).takeIf { it.startsWith("http") }.orEmpty()
    }

    private fun redditPostId(url: String): String? = runCatching {
        val segments = URI(url).path?.split("/")?.filter { it.isNotBlank() }.orEmpty()
        val index = segments.indexOf("comments")
        if (index >= 0 && segments.size > index + 1) segments[index + 1].takeIf { it.matches(Regex("[A-Za-z0-9_]+")) }
        else if (URI(url).host == "redd.it") segments.firstOrNull() else null
    }.getOrNull()

    private fun toVxRedditUrl(url: String): String = runCatching {
        val uri = URI(url)
        val host = uri.host ?: return url
        val vx = when {
            host == "reddit.com" -> "vxreddit.com"
            host == "www.reddit.com" -> "www.vxreddit.com"
            host.endsWith(".reddit.com") -> host.replace("reddit.com", "vxreddit.com")
            else -> host
        }
        URI(uri.scheme, vx, uri.path, uri.query, uri.fragment).toString()
    }.getOrDefault(url)

    private fun merge(primary: OgPreview, candidate: OgPreview): OgPreview = primary.copy(
        title = if (!isBlockedTitle(candidate.title) && candidate.title.isNotBlank()) candidate.title else primary.title,
        description = primary.description.ifBlank { candidate.description },
        siteName = primary.siteName.ifBlank { candidate.siteName },
        imageBytes = primary.imageBytes ?: candidate.imageBytes,
        finalUrl = primary.finalUrl.ifBlank { candidate.finalUrl },
    )

    private fun resolveUrl(base: String, value: String): String = runCatching {
        URI(base).resolve(value).toString()
    }.getOrDefault(value)

    private fun first(meta: Map<String, String>, vararg keys: String): String =
        keys.firstNotNullOfOrNull { meta[it]?.trim()?.takeIf(String::isNotBlank) }.orEmpty()

    private fun cleanTitle(value: String): String = value
        .replace(Regex("""^Check out this post on Reddit:\s*""", RegexOption.IGNORE_CASE), "")
        .replace(Regex("""\s*-\s*$"""), "")
        .trim()
        .take(500)

    private fun isBlockedTitle(title: String): Boolean {
        val value = title.trim().lowercase()
        if (value.isBlank() || value in setOf("shared link", "reddit", "instagram", "facebook")) return true
        return listOf(
            "please wait for verification", "cloudflare", "attention required",
            "internal server error", "500 internal", "404 not found", "login", "log in", "error",
        ).any(value::contains)
    }

    private fun titleFromUrl(url: String): String = runCatching {
        val uri = URI(url)
        val segments = uri.path?.split("/")?.filter { it.isNotBlank() }.orEmpty()
        val comments = segments.indexOf("comments")
        val slug = if (comments >= 0 && segments.size > comments + 2) segments[comments + 2]
        else segments.lastOrNull { it.contains("-") || it.contains("_") } ?: segments.lastOrNull().orEmpty()
        URLDecoder.decode(slug, Charsets.UTF_8.name()).replace(Regex("[-_]+"), " ").trim()
            .replaceFirstChar { if (it.isLowerCase()) it.titlecase() else it.toString() }
    }.getOrNull().orEmpty()

    private fun fallback(url: String): OgPreview = OgPreview(
        title = titleFromUrl(url).ifBlank { UrlCleaner.hostOf(url) },
        finalUrl = url,
    )
}
