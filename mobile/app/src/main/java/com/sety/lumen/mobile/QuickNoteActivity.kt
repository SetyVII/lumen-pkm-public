package com.sety.lumen.mobile

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sety.lumen.mobile.data.todayIso
import com.sety.lumen.mobile.ui.LumenViewModel
import com.sety.lumen.mobile.ui.theme.LumenTheme

class QuickNoteActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val date = intent.getStringExtra("date") ?: todayIso()
        setContent {
            LumenTheme {
                QuickNoteScreen(date, onDone = { finish() })
            }
        }
    }
}

@Composable
private fun QuickNoteScreen(date: String, onDone: () -> Unit) {
    val vm: LumenViewModel = viewModel()
    var text by remember { mutableStateOf("") }
    var isTask by remember { mutableStateOf(false) }
    var dueDate by remember { mutableStateOf(date) }
    var error by remember { mutableStateOf("") }
    Surface(Modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().statusBarsPadding().verticalScroll(rememberScrollState()).padding(18.dp),
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = onDone) { Icon(Icons.Default.ArrowBack, contentDescription = "Cerrar") }
                Column {
                    Text("Nota rápida", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                    Text("Se guarda en $date", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            Spacer(Modifier.height(14.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(selected = !isTask, onClick = { isTask = false }, label = { Text("Nota") })
                FilterChip(selected = isTask, onClick = { isTask = true }, label = { Text("Tarea") })
            }
            Spacer(Modifier.height(10.dp))
            OutlinedTextField(
                value = text,
                onValueChange = { text = it; error = "" },
                label = { Text(if (isTask) "¿Qué hay que hacer?" else "¿Qué quieres recordar?") },
                modifier = Modifier.fillMaxWidth(),
                minLines = 5,
            )
            if (isTask) {
                Spacer(Modifier.height(10.dp))
                OutlinedTextField(
                    value = dueDate,
                    onValueChange = { dueDate = it },
                    label = { Text("Fecha límite (YYYY-MM-DD)") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            if (error.isNotBlank()) {
                Text(error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
            }
            Spacer(Modifier.height(16.dp))
            Button(
                onClick = {
                    if (text.isBlank()) {
                        error = "Escribe algo primero"
                    } else {
                        vm.quickNote(text, isTask, if (isTask) dueDate else null, date)
                        onDone()
                    }
                },
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (isTask) "Crear tarea" else "Guardar nota") }
            TextButton(onClick = onDone, modifier = Modifier.fillMaxWidth()) { Text("Cancelar") }
        }
    }
}
