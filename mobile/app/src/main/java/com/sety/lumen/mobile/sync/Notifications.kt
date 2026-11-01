package com.sety.lumen.mobile.sync

import android.Manifest
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import com.sety.lumen.mobile.MainActivity
import com.sety.lumen.mobile.R
import com.sety.lumen.mobile.data.WidgetTask

object Notifications {
    const val CHANNEL_TASKS = "lumen_tasks"
    const val CHANNEL_JOURNAL = "lumen_journal"
    const val CHANNEL_SYNC = "lumen_sync"
    const val CHANNEL_ERRORS = "lumen_sync_errors"

    fun createChannels(context: Context) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        val manager = context.getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_TASKS, "Tareas de Lumen", NotificationManager.IMPORTANCE_DEFAULT).apply {
                description = "Tareas que vencen hoy y acciones rápidas"
            }
        )
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_JOURNAL, "Diario visual", NotificationManager.IMPORTANCE_DEFAULT).apply {
                description = "Recordatorio para capturar un momento del día"
            }
        )
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_SYNC, "Sincronización", NotificationManager.IMPORTANCE_LOW).apply {
                description = "Resultado silencioso de la sincronización local"
            }
        )
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ERRORS, "Errores de sincronización", NotificationManager.IMPORTANCE_DEFAULT).apply {
                description = "Avisos cuando el servidor Lumen no está disponible"
            }
        )
    }

    fun showTaskDue(context: Context, task: WidgetTask) {
        if (!canNotify(context)) return
        val complete = action(context, TaskActionReceiver.ACTION_COMPLETE, task.id, 1)
        val postpone = action(context, TaskActionReceiver.ACTION_POSTPONE, task.id, 2)
        val open = PendingIntent.getActivity(
            context,
            task.id.hashCode(),
            Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notification = NotificationCompat.Builder(context, CHANNEL_TASKS)
            .setSmallIcon(R.drawable.ic_lumen)
            .setContentTitle(task.title)
            .setContentText(if (task.dueDate.isBlank()) "Tarea pendiente" else "Vence hoy · ${task.dueDate}")
            .setContentIntent(open)
            .setAutoCancel(true)
            .addAction(R.drawable.ic_lumen, "Completar ✓", complete)
            .addAction(R.drawable.ic_lumen, "Posponer a mañana", postpone)
            .build()
        NotificationManagerCompat.from(context).notify(task.id.hashCode(), notification)
    }

    fun showJournalReminder(context: Context) {
        if (!canNotify(context)) return
        val open = PendingIntent.getActivity(
            context,
            9001,
            Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notification = NotificationCompat.Builder(context, CHANNEL_JOURNAL)
            .setSmallIcon(R.drawable.ic_lumen)
            .setContentTitle("Tu día aún no tiene una foto")
            .setContentText("Abre Lumen y captura un momento antes de dormir.")
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
        NotificationManagerCompat.from(context).notify(9001, notification)
    }

    fun notifySyncResult(context: Context, message: String) {
        if (!canNotify(context)) return
        if (!message.contains("subida") && !message.contains("cambio") && !message.contains("conflicto")) return
        val notification = NotificationCompat.Builder(context, CHANNEL_SYNC)
            .setSmallIcon(R.drawable.ic_lumen)
            .setContentTitle("Lumen sincronizado")
            .setContentText(message)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setAutoCancel(true)
            .build()
        NotificationManagerCompat.from(context).notify(9100, notification)
    }

    fun notifySyncError(context: Context, message: String) {
        if (!canNotify(context)) return
        val notification = NotificationCompat.Builder(context, CHANNEL_ERRORS)
            .setSmallIcon(R.drawable.ic_lumen)
            .setContentTitle("No se pudo sincronizar Lumen")
            .setContentText(message)
            .setStyle(NotificationCompat.BigTextStyle().bigText(message))
            .setAutoCancel(true)
            .build()
        NotificationManagerCompat.from(context).notify(9101, notification)
    }

    fun cancelTask(context: Context, taskId: String) {
        NotificationManagerCompat.from(context).cancel(taskId.hashCode())
    }

    private fun action(context: Context, action: String, taskId: String, request: Int): PendingIntent =
        PendingIntent.getBroadcast(
            context,
            taskId.hashCode() + request,
            Intent(context, TaskActionReceiver::class.java)
                .setAction(action)
                .putExtra(TaskActionReceiver.EXTRA_TASK_ID, taskId),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )

    private fun canNotify(context: Context): Boolean {
        if (Build.VERSION.SDK_INT >= 33 &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) return false
        return NotificationManagerCompat.from(context).areNotificationsEnabled()
    }
}
