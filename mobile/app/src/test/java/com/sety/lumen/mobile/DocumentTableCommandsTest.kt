package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

class DocumentTableCommandsTest {
    @Test fun localTableOperationsMatchTheGoSharedCorpus() {
        val fixtureRoot = generateSequence(File(System.getProperty("user.dir"))) { it.parentFile }
            .map { File(it, "contracts/fixtures/document-commands") }.first { it.isDirectory }
        for (name in listOf("sort-numeric", "move-table-column", "resize-only-one", "delete-table-row")) {
            val fixture = JSONObject(File(fixtureRoot, "$name.json").readText())
            val original = fixture.getJSONObject("input")
            val before = original.toString()
            var table = original.getJSONArray("blocks").getJSONObject(0)
            val commands = fixture.getJSONArray("commands")
            for (i in 0 until commands.length()) table = applyDocumentTableCommand(table, commands.getJSONObject(i))
            assertTrue(name, equivalent(fixture.getJSONObject("expected").getJSONArray("blocks").getJSONObject(0), table))
            assertEquals("Input was mutated: $name", before, original.toString())
        }
    }
    @Test fun failedNumericSortLeavesTheInputIntact() {
        val table = documentTable(2, 1)
        table.getJSONArray("children").getJSONObject(0).getJSONArray("children").getJSONObject(0).getJSONArray("children").put(0, documentParagraph("invalid number"))
        val before = table.toString()
        val command = JSONObject().put("type", "table_sort").put("node_id", table.getString("id"))
            .put("column_id", table.getJSONObject("attrs").getJSONArray("columns").getJSONObject(0).getString("id")).put("direction", "asc").put("numeric", true)
        assertTrue(runCatching { applyDocumentTableCommand(table, command) }.isFailure)
        assertEquals(before, table.toString())
    }
    private fun equivalent(a: Any?, b: Any?): Boolean = when {
        a is JSONObject && b is JSONObject -> a.keys().asSequence().toSet().let { keys -> keys == b.keys().asSequence().toSet() && keys.all { equivalent(a.get(it), b.get(it)) } }
        a is JSONArray && b is JSONArray -> a.length() == b.length() && (0 until a.length()).all { equivalent(a.get(it), b.get(it)) }
        a is Number && b is Number -> a.toDouble() == b.toDouble()
        else -> a == b
    }
}
