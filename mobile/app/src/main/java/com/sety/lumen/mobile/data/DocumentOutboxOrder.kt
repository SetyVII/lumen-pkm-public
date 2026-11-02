package com.sety.lumen.mobile.data

import org.json.JSONArray
import org.json.JSONObject

/** Prepare a NEW bootstrap operation only; never alter an operation's replay payload. */
fun mediaUploadSnapshot(obj: LumenObject, uploadableAssetIds: Set<String>): LumenObject =
    if (
        obj.typeId in setOf("image", "pdf") &&
            obj.assetId.isNotBlank() &&
            obj.assetId in uploadableAssetIds
    )
        obj.copy(
            properties = JSONObject(obj.properties.toString()).put("media_state", "upload_pending")
        )
    else obj

/**
 * Stable dependency order. Asset edges are strict; object-reference cycles may be deferred
 * server-side.
 */
fun orderDocumentOutbox(pending: List<OutboxOp>): List<OutboxOp> {
    val firstPut =
        pending
            .filter { it.action == "put" }
            .groupBy { it.objectId }
            .mapValues { it.value.first().opId }
    val assetOps =
        pending
            .filter { it.action == "asset" }
            .associateBy { it.payload.optJSONObject("asset")?.optString("id").orEmpty() }
    val strict = mutableMapOf<String, MutableSet<String>>()
    val soft = mutableMapOf<String, MutableSet<String>>()
    val previous = mutableMapOf<String, String>()
    for (op in pending) {
        val required = strict.getOrPut(op.opId) { mutableSetOf() }
        if (op.action != "asset") previous.put(op.objectId, op.opId)?.let(required::add)
        val refs = soft.getOrPut(op.opId) { mutableSetOf() }
        if (op.action == "asset") {
            firstPut[op.payload.optJSONObject("asset")?.optString("object_id")]
                ?.takeIf { it != op.opId }
                ?.let(required::add)
        }
        op.payload.optJSONObject("object")?.let { obj ->
            obj.optJSONArray("links")?.let { links ->
                for (i in 0 until links.length()) firstPut[links.optString(i)]
                    ?.takeIf { it != op.opId }
                    ?.let(refs::add)
            }
            fun visit(nodes: JSONArray) {
                for (i in 0 until nodes.length()) {
                    val node = nodes.getJSONObject(i)
                    val attrs = node.optJSONObject("attrs") ?: JSONObject()
                    if (node.optString("type") == "image")
                        assetOps[attrs.optString("asset_id")]?.let { required.add(it.opId) }
                    if (node.optString("type") in setOf("image", "object_embed"))
                        firstPut[attrs.optString("object_id")]
                            ?.takeIf { it != op.opId }
                            ?.let(refs::add)
                    node.optJSONArray("content")?.let { content ->
                        for (j in 0 until content.length()) {
                            val inline = content.getJSONObject(j)
                            if (inline.optString("type") in setOf("object_ref", "block_ref"))
                                firstPut[inline.optString("object_id")]
                                    ?.takeIf { it != op.opId }
                                    ?.let(refs::add)
                        }
                    }
                    node.optJSONArray("children")?.let(::visit)
                }
            }
            obj.optJSONObject("document")?.optJSONArray("blocks")?.let(::visit)
        }
    }
    val done = mutableSetOf<String>()
    val rest = pending.toMutableList()
    return buildList {
        while (rest.isNotEmpty()) {
            val ready =
                rest.firstOrNull {
                    strict[it.opId].orEmpty().all(done::contains) &&
                        soft[it.opId].orEmpty().all(done::contains)
                }
                    ?: rest.firstOrNull { strict[it.opId].orEmpty().all(done::contains) }
                    ?: error(
                        "Dependencia circular de archivos/documentos: las operaciones pendientes se conservan"
                    )
            add(ready)
            done += ready.opId
            rest.remove(ready)
        }
    }
}
