package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.*
import org.junit.Assert.*
import org.junit.Test

class DocumentOutboxOrderTest {
    @Test
    fun bootstrapMarksOnlyActualQueuedMediaAndDoesNotMutateExistingSnapshot() {
        val image =
            LumenObject(
                "image",
                "image",
                "Image",
                properties = JSONObject().put("asset_id", "asset").put("media_state", "ready"),
            )
        val pending = mediaUploadSnapshot(image, setOf("asset"))
        assertEquals("upload_pending", pending.properties.getString("media_state"))
        assertEquals("ready", image.properties.getString("media_state"))
        assertEquals(image.revision, pending.revision)
        assertTrue(image.document.semanticallyEquals(pending.document))
        assertSame(image, mediaUploadSnapshot(image, emptySet()))
    }

    private fun put(id: String, owner: String, blocks: JSONArray = JSONArray()) =
        OutboxOp(
            id,
            "put",
            owner,
            JSONObject()
                .put(
                    "object",
                    JSONObject()
                        .put("id", owner)
                        .put("document", CanonicalDocument.empty().withBlocks(blocks).toJson()),
                ),
            "",
        )

    @Test
    fun imageAssetPrecedesReferencingDocumentAndFollowsMediaCreation() {
        val image =
            documentNode("image", JSONObject().put("asset_id", "asset").put("object_id", "media"))
        val journal = put("journal-append", "journal", JSONArray().put(image))
        val media = put("media-create", "media")
        val asset =
            OutboxOp(
                "asset-upload",
                "asset",
                "media",
                JSONObject()
                    .put("asset", JSONObject().put("id", "asset").put("object_id", "media")),
                "",
            )
        assertEquals(
            listOf("media-create", "asset-upload", "journal-append"),
            orderDocumentOutbox(listOf(journal, asset, media)).map { it.opId },
        )
    }

    @Test
    fun editsOnSameObjectRetainTheirOrderAndSoftReferenceCyclesDoNotDropOperations() {
        val a =
            put(
                "a-first",
                "a",
                JSONArray()
                    .put(
                        documentNode(
                            "object_embed",
                            JSONObject().put("object_id", "b").put("view", "embed"),
                        )
                    ),
            )
        val b =
            put(
                "b-first",
                "b",
                JSONArray()
                    .put(
                        documentNode(
                            "object_embed",
                            JSONObject().put("object_id", "a").put("view", "embed"),
                        )
                    ),
            )
        val a2 = put("a-second", "a")
        val ordered = orderDocumentOutbox(listOf(a, b, a2))
        assertEquals(setOf("a-first", "b-first", "a-second"), ordered.map { it.opId }.toSet())
        assertTrue(ordered.indexOf(a) < ordered.indexOf(a2))
    }
}
