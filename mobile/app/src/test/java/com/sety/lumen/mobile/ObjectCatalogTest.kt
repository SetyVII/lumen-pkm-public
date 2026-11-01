package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class ObjectCatalogTest {
    @Test fun receivedTypesOverrideDefaultsWithoutInventingTypes() {
        val modern = LumenType("modern-book", "Libro moderno", properties = JSONArray().put(JSONObject().put("key", "authors").put("name", "Autores").put("type", "object").put("target_type", "person").put("multiple", true)))
        assertEquals(listOf("modern-book"), objectCatalog(listOf(modern)).map { it.id })
        val field = objectFields(modern).single()
        assertEquals("object", field.kind)
        assertEquals("person", field.targetType)
        assertTrue(field.multiple)
        assertTrue(objectFields(objectCatalog(emptyList()).first { it.id == "book" }).any { it.key == "author" && it.kind == "text" })
    }

    @Test fun reviewAliasesRemainFilterableWithoutChangingOtherTypeStatuses() {
        assertEquals("Para revisar", canonicalReviewStatus("Por revisar"))
        assertEquals("Revisado", canonicalReviewStatus("Leído"))
        assertEquals("Revisado", canonicalReviewStatus("Reviewed"))
        assertEquals("En curso", canonicalReviewStatus("En curso"))
        assertEquals("Pendiente de aprobación", canonicalReviewStatus("Pendiente de aprobación"))
    }

    @Test fun relationMetadataSurvivesWireAndReferenceDeletionKeepsPropertyRelation() {
        val original = LumenObject.fromJson(JSONObject().put("id", "note").put("type_id", "note").put("property_links", JSONArray().put("tag")))
        assertEquals(listOf("tag"), LumenObject.fromJson(original.toJson()).propertyLinks)
        val mentioned = original.withPlainContent("[Etiqueta](lumen://object/tag)")
        assertTrue("tag" in mentioned.links)
        val removed = mentioned.withPlainContent("Texto sin mención")
        assertFalse("tag" in removed.links)
        assertTrue("tag" in removed.propertyLinks)
    }

    @Test fun relationsRespectDeclaredSchemaAndDoNotGuessFromPlainText() {
        val type = LumenType("custom", "Ejemplo", properties = JSONArray().put(JSONObject().put("key", "owner").put("type", "object")))
        val obj = LumenObject("id", "custom", "Ejemplo", properties = JSONObject().put("owner", "person").put("description", "not-an-id"))
        assertEquals(setOf("person"), relatedObjectIds(obj, listOf(type)))
    }

    @Test fun characterByCharacterMarkdownProducesFormattedRuns() {
        var obj = LumenObject("markdown", "note", "Prueba")
        var visible = ""
        "Antes **fuerte** y _cursiva_".forEach { ch ->
            obj = obj.withPlainContent(visible + ch)
            visible = obj.content
        }
        assertEquals("Antes fuerte y cursiva", obj.content)
        val runs = obj.properties.getJSONArray("blocks").getJSONObject(0).getJSONArray("runs")
        assertTrue((0 until runs.length()).any { runs.getJSONObject(it).optBoolean("bold") })
        assertTrue((0 until runs.length()).any { runs.getJSONObject(it).optBoolean("italic") })
        val yOffset = obj.content.indexOf(" y ") + 1
        var offset = 0
        for (i in 0 until runs.length()) {
            val run = runs.getJSONObject(i)
            if (yOffset in offset until offset + run.getString("text").length) assertFalse(run.optBoolean("bold"))
            offset += run.getString("text").length
        }
    }

    @Test fun externalLinksRemainLinksAndCodeRemainsLiteral() {
        val obj = LumenObject("markdown", "note", "Prueba").withPlainContent("[Web](https://example.org) y `**literal**`")
        val tokens = editorTokens(obj.content, obj)
        assertEquals("https://example.org", tokens.first { it.kind == EditorTokenKind.LINK }.url)
        assertTrue(obj.content.contains("**literal**"))
    }
    @Test fun sharedSchemaFixtureLoadsWithoutFlatteningRelations() {
        val fixture = generateSequence(java.io.File(System.getProperty("user.dir"))) { it.parentFile }
            .map { java.io.File(it, "contracts/fixtures/mobile-object-schemas.json") }.first { it.exists() }
        val json = JSONObject(fixture.readText())
        val types = json.getJSONArray("types").let { array -> (0 until array.length()).map { LumenType.fromJson(array.getJSONObject(it)) } }
        val objects = json.getJSONArray("objects").let { array -> (0 until array.length()).map { LumenObject.fromJson(array.getJSONObject(it)) } }
        val modern = objects.first { it.id == "fixture-book-modern-object" }
        assertEquals(setOf("fixture-author"), relatedObjectIds(modern, types))
        assertTrue(relatedObjectIds(objects.first { it.id == "fixture-book-legacy" }, objectCatalog(emptyList())).isEmpty())
        assertEquals("2026-11-30", modern.properties.getJSONObject("reading_line").getString("end"))
    }

}
