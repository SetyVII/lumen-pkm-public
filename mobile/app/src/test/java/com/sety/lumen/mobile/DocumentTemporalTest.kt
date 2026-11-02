package com.sety.lumen.mobile

import com.sety.lumen.mobile.ui.temporalDisplay
import com.sety.lumen.mobile.ui.temporalValue
import org.junit.Assert.*
import org.junit.Test
import java.time.*

class DocumentTemporalTest {
    @Test fun sameDayIntervalUsesOneLocalizedDateAndBothTimes() {
        val result = temporalValue(true, LocalDate.of(2026, 11, 2), LocalTime.of(2, 17), LocalDate.of(2026, 11, 2), LocalTime.of(3, 17), ZoneId.of("Europe/Madrid"))
        assertEquals("2026-11-02T01:17:00Z", result.getString("start"))
        assertEquals("2 de noviembre de 2026, 02:17 - 03:17", temporalDisplay(result))
    }
    @Test fun datesAndMonthYearChangesDoNotBecomeMidnightUtc() {
        val date = temporalValue(false, LocalDate.of(2026, 12, 31), LocalTime.MIDNIGHT, LocalDate.of(2027, 1, 1), null, ZoneId.of("Pacific/Auckland"))
        assertEquals("2026-12-31", date.getString("start")); assertEquals("2027-01-01", date.getString("end"))
        assertFalse(date.has("timezone"))
        assertEquals("31 de diciembre de 2026 - 1 de enero de 2027", temporalDisplay(date))
    }
    @Test fun invertedIntervalsAndNonexistentDstTimesAreRejected() {
        val zone = ZoneId.of("Europe/Madrid")
        assertTrue(runCatching { temporalValue(true, LocalDate.of(2026, 3, 29), LocalTime.of(2, 30), null, null, zone) }.isFailure)
        assertTrue(runCatching { temporalValue(false, LocalDate.of(2026, 11, 2), LocalTime.NOON, LocalDate.of(2026, 11, 1), null, zone) }.isFailure)
    }
    @Test fun repeatedDstClockUsesEarlierOccurrenceConsistently() {
        val date = temporalValue(true, LocalDate.of(2026, 10, 25), LocalTime.of(2, 30), null, null, ZoneId.of("Europe/Madrid"))
        assertEquals("2026-10-25T00:30:00Z", date.getString("start"))
    }
}
