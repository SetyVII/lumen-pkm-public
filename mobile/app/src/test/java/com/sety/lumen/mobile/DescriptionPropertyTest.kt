package com.sety.lumen.mobile

import com.sety.lumen.mobile.data.LumenObject
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Test

class DescriptionPropertyTest {
    private fun webObject(properties: JSONObject) = LumenObject(
        id = "web-1",
        typeId = "web",
        title = "Enlace",
        properties = properties,
    )

    @Test
    fun summaryIsTheCanonicalDescription() {
        val objectWithBoth = webObject(
            JSONObject()
                .put("summary", "nuevo")
                .put("description", "viejo")
                .put("og_description", "og"),
        )
        assertEquals("nuevo", objectWithBoth.ogDescription)
    }

    @Test
    fun legacyDescriptionStillReads() {
        assertEquals("viejo", webObject(JSONObject().put("description", "viejo")).ogDescription)
        assertEquals("og", webObject(JSONObject().put("og_description", "og")).ogDescription)
    }
}
