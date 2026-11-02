package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.*
import org.junit.Assert.*
import org.junit.Test

class DocumentOutboxWindowTest {
    private fun put(id: String, owner: String, blocks: JSONArray = JSONArray()) =
        OutboxOp(
            id,
            "put",
            owner,
            JSONObject()
                .put("action", "put")
                .put(
                    "object",
                    JSONObject()
                        .put("id", owner)
                        .put("document", CanonicalDocument.empty().withBlocks(blocks).toJson()),
                ),
            "",
        )

    @Test
    fun dependencyBeyondFirstHundredIsIncludedAndAckStagesPreventMissingAsset() {
        val doc =
            put(
                "journal",
                "journal",
                JSONArray().put(documentNode("image", JSONObject().put("asset_id", "asset"))),
            )
        val owner = put("media", "media")
        val asset =
            OutboxOp(
                "asset",
                "asset",
                "media",
                JSONObject()
                    .put("action", "asset")
                    .put("asset", JSONObject().put("id", "asset").put("object_id", "media")),
                "",
            )
        val queue =
            (listOf(doc) + (1..120).map { put("filler$it", "filler$it") } + owner + asset)
                .toMutableList()
        fun assetFor(id: String) =
            queue.firstOrNull {
                it.action == "asset" && it.payload.getJSONObject("asset").getString("id") == id
            }
        fun firstPut(id: String) = queue.firstOrNull { it.action == "put" && it.objectId == id }
        val window = documentOutboxWindow(queue.take(100), ::assetFor, ::firstPut)
        assertTrue(window.any { it.opId == "asset" })
        assertTrue(window.any { it.opId == "media" })
        var batch = readyDocumentBatch(window, ::assetFor, ::firstPut)
        assertTrue(batch.any { it.opId == "media" })
        assertFalse(batch.any { it.opId == "journal" || it.opId == "asset" })
        queue.removeAll { it.opId in batch.map { op -> op.opId } }
        batch =
            readyDocumentBatch(
                documentOutboxWindow(queue.take(100), ::assetFor, ::firstPut),
                ::assetFor,
                ::firstPut,
            )
        assertTrue(batch.any { it.opId == "asset" })
        assertFalse(batch.any { it.opId == "journal" })
        queue.removeAll { it.opId in batch.map { op -> op.opId } }
        batch =
            readyDocumentBatch(
                documentOutboxWindow(queue.take(100), ::assetFor, ::firstPut),
                ::assetFor,
                ::firstPut,
            )
        assertTrue(batch.any { it.opId == "journal" })
    }

    @Test
    fun atMostOneSnapshotPerObjectIsSentAndDependencyClosureIsBounded() {
        val operations = (1..110).map { put("edit$it", "same") }
        val first: (String) -> OutboxOp? = { id -> operations.firstOrNull { it.objectId == id } }
        val window = documentOutboxWindow(operations, { null }, first, maxOperations = 8)
        assertEquals(8, window.size)
        assertEquals(listOf("edit1"), readyDocumentBatch(window, { null }, first).map { it.opId })
    }
}
