package com.sety.lumen.mobile

import com.sety.lumen.mobile.util.UrlCleaner
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UrlCleanerTest {
    @Test
    fun removesTrackingAndSortsQuery() {
        assertEquals(
            "https://example.com/a?keep=1",
            UrlCleaner.clean("https://Example.com/a?utm_source=x&keep=1&fbclid=2#frag"),
        )
    }

    @Test
    fun normalizesYouTubeShortLinks() {
        assertEquals("https://www.youtube.com/watch?v=abc123", UrlCleaner.clean("https://youtu.be/abc123?si=tracking"))
        assertEquals("https://www.youtube.com/watch?v=abc123", UrlCleaner.clean("https://www.youtube.com/shorts/abc123?feature=share"))
    }

    @Test
    fun normalizesRedditShortLinks() {
        assertEquals("https://www.reddit.com/comments/abc123", UrlCleaner.clean("https://redd.it/abc123"))
        assertTrue(UrlCleaner.isReddit("https://www.reddit.com/r/android/comments/abc123"))
        assertTrue(UrlCleaner.isRedditShort("https://www.reddit.com/r/android/s/abc123"))
        assertFalse(UrlCleaner.isReddit("https://example.com"))
    }
}
