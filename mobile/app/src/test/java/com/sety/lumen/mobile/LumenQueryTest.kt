package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.File

class LumenQueryTest {
    @Test fun savedStatusIsEditableWithoutAddingAContradictoryCondition() {
        val spec = JSONObject("""{"type_id":"web","filter":{"mode":"and","rules":[
          {"field":"properties.status","op":"in","value":["Por revisar","Para revisar"]},
          {"mode":"or","rules":[{"field":"title","op":"contains","value":"a"},{"field":"title","op":"contains","value":"b"}]}
        ]},"presentation":"kanban","custom":"keep"}""")
        val draft = com.sety.lumen.mobile.ui.editableViewDraft(spec, emptySet(), true)
        assertEquals("Para revisar", draft.review)
        assertEquals(1, draft.base.getJSONObject("filter").getJSONArray("rules").length())
        assertEquals("kanban", draft.base.getString("presentation"))
        assertEquals("keep", draft.base.getString("custom"))
        assertEquals(2, spec.getJSONObject("filter").getJSONArray("rules").length())
    }
    @Test fun sharedSavedViewCasesMatchDesktop() {
        val file = generateSequence(File(requireNotNull(System.getProperty("user.dir")))) { it.parentFile }
            .map { File(it, "contracts/fixtures/saved-views.json") }.first { it.exists() }
        val fixture = JSONObject(file.readText())
        val raw = fixture.getJSONArray("objects")
        val objects = (0 until raw.length()).map { LumenObject.fromJson(raw.getJSONObject(it)) }
        val cases = fixture.getJSONArray("cases")
        for (i in 0 until cases.length()) {
            val case = cases.getJSONObject(i)
            val ids = case.getJSONArray("ids")
            assertEquals(case.getString("name"), (0 until ids.length()).map { ids.getString(it) }, LumenQuery.evaluate(objects, case.getJSONObject("spec")).map { it.id })
        }
    }
}
