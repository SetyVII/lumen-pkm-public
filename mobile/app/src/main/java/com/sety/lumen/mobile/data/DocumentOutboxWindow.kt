package com.sety.lumen.mobile.data

import org.json.JSONArray

/**
 * Resolve strict media dependencies outside the queue's head without decrypting the whole library.
 */
fun documentOutboxWindow(
    seeds: List<OutboxOp>,
    assetFor: (String) -> OutboxOp?,
    firstPut: (String) -> OutboxOp?,
    maxOperations: Int = 128,
    maxBytes: Long = 24L * 1024 * 1024,
): List<OutboxOp> {
    val result = linkedMapOf<String, OutboxOp>()
    val visiting = mutableSetOf<String>()
    var bytes = 0L
    fun visit(op: OutboxOp) {
        if (op.opId in result) return
        require(visiting.add(op.opId)) { "Dependencia circular de medios; se conserva outbox" }
        if (result.size >= maxOperations) {
            visiting.remove(op.opId)
            return
        }
        strictDocumentDependencies(op, assetFor, firstPut).forEach(::visit)
        val size = op.payload.toString().toByteArray(Charsets.UTF_8).size
        if (result.size < maxOperations && (bytes + size <= maxBytes || result.isEmpty())) {
            result[op.opId] = op
            bytes += size
        }
        visiting.remove(op.opId)
    }
    seeds.forEach(::visit)
    return result.values.toList()
}

fun strictDocumentDependencies(
    op: OutboxOp,
    assetFor: (String) -> OutboxOp?,
    firstPut: (String) -> OutboxOp?,
): List<OutboxOp> = buildList {
    if (op.action == "asset")
        op.payload.optJSONObject("asset")?.optString("object_id")?.let(firstPut)?.let { add(it) }
    if (op.action == "put") {
        firstPut(op.objectId)?.takeIf { it.opId != op.opId }?.let { add(it) }
        fun visit(nodes: JSONArray) {
            for (i in 0 until nodes.length()) {
                val node = nodes.getJSONObject(i)
                if (node.optString("type") == "image")
                    node.optJSONObject("attrs")?.optString("asset_id")?.let(assetFor)?.let {
                        add(it)
                    }
                node.optJSONArray("children")?.let(::visit)
            }
        }
        op.payload
            .optJSONObject("object")
            ?.optJSONObject("document")
            ?.optJSONArray("blocks")
            ?.let(::visit)
    }
}

/** One operation per owner; dependencies must already be ACKed, not merely earlier in a batch. */
fun readyDocumentBatch(
    window: List<OutboxOp>,
    assetFor: (String) -> OutboxOp?,
    firstPut: (String) -> OutboxOp?,
    maxOperations: Int = 32,
): List<OutboxOp> {
    val owners = mutableSetOf<String>()
    return window
        .filter { op ->
            strictDocumentDependencies(op, assetFor, firstPut).isEmpty() && owners.add(op.objectId)
        }
        .take(maxOperations)
}
