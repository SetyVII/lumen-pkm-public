package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class DocumentAcknowledgementTest {
    private fun operation(base: Long) =
        JSONObject()
            .put("op_id", "unsent")
            .put("action", "put")
            .put("base_revision", base)
            .put(
                "object",
                JSONObject()
                    .put("id", "object")
                    .put("revision", base)
                    .put("document", CanonicalDocument.fromText("Local edit").toJson()),
            )

    @Test
    fun compactOwnReceiptAdvancesOnlyUnsentExactDescendantWithoutMutatingItsSource() {
        val op = operation(2)
        val ack = SyncReceipt("uploaded-asset", "object", 3, 2)
        val advanced = advanceUnsentDocumentOperation(op, 0, ack)!!
        assertEquals(3, advanced.getLong("base_revision"))
        assertEquals(2, op.getLong("base_revision"))
        assertEquals(
            op.getJSONObject("object").getJSONObject("document").toString(),
            advanced.getJSONObject("object").getJSONObject("document").toString(),
        )
        assertNull(advanceUnsentDocumentOperation(op, 1, ack))
        assertNull(advanceUnsentDocumentOperation(op, 0, SyncReceipt("foreign", "object", 5, 4)))
        assertNull(
            advanceUnsentDocumentOperation(op, 0, SyncReceipt("unknown-previous", "object", 3))
        )
    }

    @Test
    fun droppedAcknowledgementKeepsAttemptedPayloadIdenticalUntilExactReceiptArrives() {
        val attempted = operation(1)
        val before = attempted.toString()
        assertNull(
            advanceUnsentDocumentOperation(attempted, 1, SyncReceipt("first", "object", 2, 1))
        )
        assertEquals(before, attempted.toString())
        val following = operation(1)
        assertEquals(
            2,
            advanceUnsentDocumentOperation(following, 0, SyncReceipt("first", "object", 2, 1))!!
                .getLong("base_revision"),
        )
    }
}
