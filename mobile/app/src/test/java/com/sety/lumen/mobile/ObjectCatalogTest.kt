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
        val mentioned = original.withDocument(CanonicalDocument.empty().withBlocks(JSONArray().put(documentParagraph().put("content", JSONArray().put(
            JSONObject().put("type", "object_ref").put("object_id", "tag").put("label", "Etiqueta"))))))
        assertTrue("tag" in mentioned.document.references())
        assertFalse("tag" in mentioned.links)
        val removed = mentioned.withDocument(CanonicalDocument.fromText("Texto sin mención"))
        assertFalse("tag" in removed.document.references())
        assertTrue("tag" in removed.propertyLinks)
    }

    @Test fun relationsRespectDeclaredSchemaAndDoNotGuessFromPlainText() {
        val type = LumenType("custom", "Ejemplo", properties = JSONArray().put(JSONObject().put("key", "owner").put("type", "object")))
        val obj = LumenObject("id", "custom", "Ejemplo", properties = JSONObject().put("owner", "person").put("description", "not-an-id"))
        assertEquals(setOf("person"), relatedObjectIds(obj, listOf(type)))
    }

    @Test fun documentMarksAndLinksSurviveMetadataChanges() {
        val paragraph = documentParagraph().put("content", JSONArray()
            .put(JSONObject().put("type", "text").put("text", "Fuerte").put("marks", JSONObject().put("bold", true)))
            .put(JSONObject().put("type", "link").put("text", "Web").put("href", "https://example.org")))
        val original = LumenObject("note", "note", "Prueba").withDocument(CanonicalDocument.empty().withBlocks(JSONArray().put(paragraph)))
        val changed = LumenObject.fromJson(original.copy(title = "Renombrado").toJson())
        assertEquals(original.document.toJson().toString(), changed.document.toJson().toString())
        assertTrue(changed.document.blocks().getJSONObject(0).getJSONArray("content").getJSONObject(0).getJSONObject("marks").getBoolean("bold"))
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
