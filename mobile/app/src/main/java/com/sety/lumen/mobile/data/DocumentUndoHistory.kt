package com.sety.lumen.mobile.data

/** Bounded immutable document snapshots, shared by all structural editor actions. */
class DocumentUndoHistory(
    private val limit: Int = 40,
    private val byteLimit: Long = 4L * 1024 * 1024,
) {
    private val previous = mutableListOf<CanonicalDocument>()
    private val following = mutableListOf<CanonicalDocument>()
    val canUndo
        get() = previous.isNotEmpty()

    val canRedo
        get() = following.isNotEmpty()

    private fun trim(stack: MutableList<CanonicalDocument>) {
        var bytes = stack.sumOf { it.toJson().toString().toByteArray(Charsets.UTF_8).size.toLong() }
        while (stack.isNotEmpty() && (stack.size > limit || bytes > byteLimit)) bytes -=
            stack.removeAt(0).toJson().toString().toByteArray(Charsets.UTF_8).size
    }

    fun record(current: CanonicalDocument, next: CanonicalDocument): Boolean {
        if (current.semanticallyEquals(next)) return false
        previous += current
        trim(previous)
        following.clear()
        return true
    }

    fun undo(current: CanonicalDocument): CanonicalDocument? {
        if (previous.isEmpty()) return null
        following += current
        trim(following)
        return previous.removeAt(previous.lastIndex)
    }

    fun redo(current: CanonicalDocument): CanonicalDocument? {
        if (following.isEmpty()) return null
        previous += current
        trim(previous)
        return following.removeAt(following.lastIndex)
    }

    fun reset() {
        previous.clear()
        following.clear()
    }
}
