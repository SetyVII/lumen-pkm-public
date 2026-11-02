package com.sety.lumen.mobile.data

import org.json.JSONObject

/** Only an unsent descendant of the exact acknowledged base can advance automatically. */
fun advanceUnsentDocumentOperation(
    payload: JSONObject,
    attempts: Int,
    receipt: SyncReceipt,
): JSONObject? {
    val previous = receipt.previousRevision ?: return null
    if (
        attempts != 0 ||
            payload.optString("action") != "put" ||
            receipt.revision <= previous ||
            payload.optLong("base_revision") != previous
    )
        return null
    val objectId = payload.optJSONObject("object")?.optString("id") ?: return null
    if (objectId != receipt.objectId) return null
    return JSONObject(payload.toString()).put("base_revision", receipt.revision).apply {
        optJSONObject("object")?.put("revision", receipt.revision)
    }
}
