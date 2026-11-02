package com.sety.lumen.mobile.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import org.json.JSONObject
import java.time.*
import java.time.format.DateTimeFormatter
import java.util.Locale

fun temporalDisplay(attrs: JSONObject): String = runCatching {
    val locale = Locale.forLanguageTag("es")
    val dateFormat = DateTimeFormatter.ofPattern("d 'de' MMMM 'de' yyyy", locale)
    val timeFormat = DateTimeFormatter.ofPattern("HH:mm", locale)
    val end = attrs.optString("end")
    if (attrs.optString("kind") == "date") {
        val startDate = LocalDate.parse(attrs.getString("start"))
        startDate.format(dateFormat) + if (end.isNotBlank()) " - " + LocalDate.parse(end).format(dateFormat) else ""
    } else {
        val zone = ZoneId.of(attrs.getString("timezone"))
        val startTime = Instant.parse(attrs.getString("start")).atZone(zone)
        val endTime = end.takeIf(String::isNotBlank)?.let { Instant.parse(it).atZone(zone) }
        startTime.format(dateFormat) + ", " + startTime.format(timeFormat) + if (endTime != null) {
            " - " + (if (endTime.toLocalDate() != startTime.toLocalDate()) endTime.format(dateFormat) + ", " else "") + endTime.format(timeFormat)
        } else ""
    }
}.getOrElse { attrs.optString("start") }

/** Resolve ambiguous times explicitly by zone rules; reject nonexistent DST times. */
fun temporalValue(includeTime: Boolean, date: LocalDate, time: LocalTime, endDate: LocalDate?, endTime: LocalTime?, zone: ZoneId): JSONObject {
    fun instant(day: LocalDate, clock: LocalTime): Instant {
        val local = day.atTime(clock)
        val offsets = zone.rules.getValidOffsets(local)
        require(offsets.isNotEmpty()) { "La hora no existe por el cambio de horario" }
        // Choose the earlier occurrence consistently when a local clock repeats.
        return local.toInstant(offsets.first())
    }
    val start = if (includeTime) instant(date, time).toString() else date.toString()
    val end = endDate?.let { if (includeTime) instant(it, endTime ?: time).toString() else it.toString() }
    require(end == null || if (includeTime) !Instant.parse(end).isBefore(Instant.parse(start)) else !LocalDate.parse(end).isBefore(date)) { "La finalización no puede ser anterior al inicio" }
    return JSONObject().put("kind", if (includeTime) "datetime" else "date").put("start", start).apply {
        if (includeTime) put("timezone", zone.id)
        if (end != null) put("end", end)
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DocumentTemporalPicker(value: JSONObject, onDismiss: () -> Unit, onConfirm: (JSONObject) -> Unit, onRemove: () -> Unit) {
    val zone = remember { runCatching { ZoneId.of(value.optString("timezone")) }.getOrDefault(ZoneId.systemDefault()) }
    val initial = remember { runCatching {
        if (value.optString("kind") == "datetime") Instant.parse(value.getString("start")).atZone(zone)
        else LocalDate.parse(value.getString("start")).atStartOfDay(zone)
    }.getOrDefault(ZonedDateTime.now(zone)) }
    val initialEnd = remember { runCatching {
        if (value.optString("kind") == "datetime") Instant.parse(value.getString("end")).atZone(zone)
        else LocalDate.parse(value.getString("end")).atStartOfDay(zone)
    }.getOrDefault(initial) }
    var includeTime by remember { mutableStateOf(value.optString("kind") == "datetime") }
    var hasEnd by remember { mutableStateOf(value.has("end")) }
    var startTime by remember { mutableStateOf(initial.toLocalTime().withSecond(0).withNano(0).toString()) }
    var endTime by remember { mutableStateOf(initialEnd.toLocalTime().withSecond(0).withNano(0).toString()) }
    val startPicker = rememberDatePickerState(initialSelectedDateMillis = initial.toLocalDate().atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli())
    val endPicker = rememberDatePickerState(initialSelectedDateMillis = initialEnd.toLocalDate().atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli())
    var error by remember { mutableStateOf("") }
    fun date(state: DatePickerState): LocalDate = Instant.ofEpochMilli(state.selectedDateMillis ?: kotlin.error("Escoge una fecha")).atZone(ZoneOffset.UTC).toLocalDate()
    Dialog(onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxWidth().widthIn(max = 960.dp).padding(12.dp), shape = MaterialTheme.shapes.large) {
            Column(Modifier.verticalScroll(rememberScrollState()).padding(8.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Text("Fecha y hora", style = MaterialTheme.typography.titleLarge)
                    TextButton(onClick = onDismiss) { Text("Cerrar") }
                }
                Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                    Text("Incluir hora", Modifier.weight(1f)); Switch(includeTime, { includeTime = it })
                }
                Row(verticalAlignment = androidx.compose.ui.Alignment.CenterVertically) {
                    Text("Fecha de finalización", Modifier.weight(1f)); Switch(hasEnd, { hasEnd = it })
                }
                Row(Modifier.horizontalScroll(rememberScrollState())) {
                    listOf("Ayer" to -1L, "Hoy" to 0L, "Mañana" to 1L, "En 1 semana" to 7L, "En 2 semanas" to 14L).forEach { (label, days) ->
                        TextButton(onClick = { startPicker.selectedDateMillis = LocalDate.now(zone).plusDays(days).atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli() }) { Text(label) }
                    }
                }
                BoxWithConstraints {
                    if (hasEnd && maxWidth > 700.dp) Row {
                        Column(Modifier.weight(1f)) { DatePicker(startPicker, title = { Text("Inicio") }); if (includeTime) OutlinedTextField(startTime, { startTime = it }, label = { Text("Hora de inicio HH:mm") }) }
                        Column(Modifier.weight(1f)) { DatePicker(endPicker, title = { Text("Finalización") }); if (includeTime) OutlinedTextField(endTime, { endTime = it }, label = { Text("Hora de finalización HH:mm") }) }
                    } else Column {
                        DatePicker(startPicker, title = { Text("Inicio") })
                        if (includeTime) OutlinedTextField(startTime, { startTime = it }, label = { Text("Hora de inicio HH:mm") })
                        if (hasEnd) { DatePicker(endPicker, title = { Text("Finalización") }); if (includeTime) OutlinedTextField(endTime, { endTime = it }, label = { Text("Hora de finalización HH:mm") }) }
                    }
                }
                if (hasEnd) Row(Modifier.horizontalScroll(rememberScrollState())) {
                    listOf("Mismo día" to 0L, "1 día" to 1L, "2 días" to 2L, "1 semana" to 7L, "2 semanas" to 14L).forEach { (label, days) ->
                        TextButton(onClick = { runCatching { date(startPicker).plusDays(days) }.onSuccess {
                            endPicker.selectedDateMillis = it.atStartOfDay(ZoneOffset.UTC).toInstant().toEpochMilli()
                        } }) { Text(label) }
                    }
                }
                if (includeTime) Text("Zona: ${zone.id}. Si una hora se repite, se usa su primera aparición.", style = MaterialTheme.typography.labelSmall)
                if (error.isNotBlank()) Text(error, color = MaterialTheme.colorScheme.error)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    TextButton(onClick = onRemove) { Text("Eliminar") }
                    TextButton(onClick = onDismiss) { Text("Cancelar") }
                    Button(onClick = { runCatching {
                        temporalValue(includeTime, date(startPicker), if (includeTime) LocalTime.parse(startTime) else LocalTime.MIDNIGHT,
                            if (hasEnd) date(endPicker) else null, if (includeTime && hasEnd) LocalTime.parse(endTime) else null, zone)
                    }.onSuccess(onConfirm).onFailure { error = it.message ?: "Fecha inválida" } }) { Text("Confirmar") }
                }
            }
        }
    }
}
