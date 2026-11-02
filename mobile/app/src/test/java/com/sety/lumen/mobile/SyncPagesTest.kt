package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import java.io.File
import java.io.IOException
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class SyncPagesTest {
    private fun pages(): List<SyncPullResult> {
        val root =
            generateSequence(File(requireNotNull(System.getProperty("user.dir")))) { it.parentFile }
                .map { File(it, "contracts/fixtures/sync-v2/pull-pages.json") }
                .first { it.isFile }
        val json = JSONObject(root.readText()).getJSONArray("pages")
        return (0 until json.length()).map { decodeSyncPullPage(json.getJSONObject(it)) }
    }

    @Test
    fun sharedPagesCommitOnlyDeliveredCursorAndResumeAfterDisconnectedPage() {
        val pages = pages()
        var durable = 0L
        val saved = linkedMapOf<String,String>()
        val requests = mutableListOf<Long>()
        assertTrue(
            runCatching {
                    consumeSyncPullPages(
                        durable,
                        fetch = { cursor ->
                            requests += cursor
                            if (cursor == 0L) pages[0] else throw IOException("Disconnected")
                        },
                        apply = { page ->
                            durable = page.nextSeq
                            page.objects.forEach{saved[it.id]=it.document.plainText()}
                        },
                    )
                }
                .isFailure
        )
        assertEquals(128, durable)
        assertNotEquals(pages[0].headSeq, durable)
        assertEquals(listOf(0L, 128L), requests)
        val result =
            consumeSyncPullPages(
                durable,
                fetch = { cursor ->
                    assertEquals(128, cursor)
                    pages[1]
                },
                apply = { page ->
                    durable = page.nextSeq
                    page.objects.forEach{saved[it.id]=it.document.plainText()}
                },
            )
        assertEquals(302, durable)
        assertFalse(result.hasMore)
        assertEquals(listOf("page-note-a", "page-note-b"), saved.keys.toList())
        assertEquals("Concurrent update",saved["page-note-a"])
    }

    @Test
    fun stalledPageDoesNotApplyAndLimitDoesNotClaimGlobalHead() {
        val page = pages()[0]
        var applied = false
        assertTrue(
            runCatching { consumeSyncPullPages(128, fetch = { page }, apply = { applied = true }) }
                .isFailure
        )
        assertFalse(applied)
        val progress = consumeSyncPullPages(0, maxPages = 1, fetch = { page }, apply = {})
        assertTrue(progress.hasMore)
        assertEquals(128, progress.cursor)
    }

    @Test
    fun legacyFixtureDefaultsNextToHeadWithoutInventingMorePages() {
        val page = decodeSyncPullPage(JSONObject().put("head_seq", 5))
        assertEquals(5, page.nextSeq)
        assertFalse(page.hasMore)
    }
}
