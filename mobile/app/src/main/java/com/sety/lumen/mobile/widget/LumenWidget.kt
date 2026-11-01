package com.sety.lumen.mobile.widget

import android.content.Context
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.action.Action
import androidx.glance.action.actionParametersOf
import androidx.glance.action.actionStartActivity
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.action.actionRunCallback
import androidx.glance.appwidget.cornerRadius
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.layout.Alignment
import androidx.glance.layout.Column
import androidx.glance.layout.Row
import androidx.glance.layout.Spacer
import androidx.glance.layout.fillMaxWidth
import androidx.glance.layout.height
import androidx.glance.layout.padding
import androidx.glance.layout.width
import androidx.glance.GlanceTheme
import androidx.glance.text.FontWeight
import androidx.glance.text.Text
import androidx.glance.text.TextStyle
import androidx.glance.unit.ColorProvider
import com.sety.lumen.mobile.CaptureMomentActivity
import com.sety.lumen.mobile.LumenApplication
import com.sety.lumen.mobile.QuickNoteActivity
import com.sety.lumen.mobile.data.WidgetTask

class LumenWidget : GlanceAppWidget() {
    override suspend fun provideGlance(context: Context, id: GlanceId) {
        val app = context.applicationContext as LumenApplication
        if (!app.vault.isUnlocked) app.vault.unlockWithDevice()
        val tasks = runCatching { app.repository.widgetTasks().filter { !it.completed }.take(4) }.getOrDefault(emptyList())
        val pending = runCatching { app.repository.counts().second }.getOrDefault(0)
        val syncLabel = app.settings.lastSyncMessage.ifBlank { "Listo para sincronizar" }
        provideContent {
            GlanceTheme {
                WidgetContent(tasks, pending, syncLabel)
            }
        }
    }
}

@androidx.compose.runtime.Composable
private fun WidgetContent(tasks: List<WidgetTask>, pending: Int, syncLabel: String) {
    Column(
        GlanceModifier.fillMaxWidth()
            .background(ColorProvider(Color(0xFFF7F3EA)))
            .cornerRadius(18.dp)
            .padding(14.dp),
    ) {
        Row(GlanceModifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Column(GlanceModifier.defaultWeight()) {
                Text(
                    "Lumen",
                    style = TextStyle(color = ColorProvider(Color(0xFF1E211D)), fontSize = 18.sp, fontWeight = FontWeight.Bold),
                )
                Text(
                    if (pending > 0) "$pending cambio(s) local(es)" else syncLabel,
                    style = TextStyle(color = ColorProvider(Color(0xFF5E7A68)), fontSize = 11.sp),
                )
            }
            WidgetButton("📷") { actionStartActivity<CaptureMomentActivity>() }
            Spacer(GlanceModifier.width(4.dp))
            WidgetButton("✏️") { actionStartActivity<QuickNoteActivity>() }
            Spacer(GlanceModifier.width(4.dp))
            WidgetButton("↻") { actionRunCallback<SyncWidgetAction>() }
        }
        Spacer(GlanceModifier.height(8.dp))
        if (tasks.isEmpty()) {
            Text(
                "Sin tareas pendientes. ¡Buen día!",
                style = TextStyle(color = ColorProvider(Color(0xFF4D514A)), fontSize = 12.sp),
            )
        } else {
            tasks.forEach { task ->
                Row(
                    GlanceModifier.fillMaxWidth()
                        .clickable(actionRunCallback<ToggleTaskAction>(actionParametersOf(ToggleTaskAction.TASK_ID_KEY to task.id)))
                        .padding(vertical = 3.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        "☐",
                        style = TextStyle(color = ColorProvider(Color(0xFF5E7A68)), fontSize = 16.sp),
                    )
                    Spacer(GlanceModifier.width(7.dp))
                    Text(
                        task.title,
                        maxLines = 1,
                        style = TextStyle(color = ColorProvider(Color(0xFF1E211D)), fontSize = 12.sp),
                    )
                }
            }
        }
    }
}

@androidx.compose.runtime.Composable
private fun WidgetButton(label: String, onClick: () -> androidx.glance.action.Action) {
    Text(
        label,
        modifier = GlanceModifier
            .background(ColorProvider(Color(0xFFDCE9DE)))
            .cornerRadius(10.dp)
            .clickable(onClick())
            .padding(horizontal = 8.dp, vertical = 5.dp),
        style = TextStyle(fontSize = 14.sp),
    )
}
