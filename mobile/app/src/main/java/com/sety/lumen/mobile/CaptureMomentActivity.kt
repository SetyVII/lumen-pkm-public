package com.sety.lumen.mobile

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
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
import androidx.compose.material.icons.filled.PhotoCamera
import androidx.compose.material.icons.filled.PhotoLibrary
import androidx.compose.material3.Button
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
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import coil.compose.AsyncImage
import com.sety.lumen.mobile.data.todayIso
import com.sety.lumen.mobile.ui.LumenViewModel
import com.sety.lumen.mobile.ui.theme.LumenTheme
import java.io.File

class CaptureMomentActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val date = intent.getStringExtra("date") ?: todayIso()
        setContent {
            LumenTheme {
                CaptureMomentScreen(date = date, onDone = { finish() })
            }
        }
    }
}

@Composable
private fun CaptureMomentScreen(date: String, onDone: () -> Unit) {
    val context = LocalContext.current
    val vm: LumenViewModel = viewModel()
    var uri by remember { mutableStateOf<Uri?>(null) }
    var caption by remember { mutableStateOf("") }
    var error by remember { mutableStateOf("") }
    var saving by remember { mutableStateOf(false) }
    var cameraUri by remember { mutableStateOf<Uri?>(null) }

    fun newCameraUri(): Uri {
        val file = File(context.cacheDir, "moment-${System.currentTimeMillis()}.jpg")
        return FileProvider.getUriForFile(context, "${context.packageName}.fileprovider", file)
    }

    val takePicture = rememberLauncherForActivityResult(ActivityResultContracts.TakePicture()) { success ->
        if (success) uri = cameraUri
    }
    val pickImage = rememberLauncherForActivityResult(ActivityResultContracts.PickVisualMedia()) { picked ->
        if (picked != null) uri = picked
    }
    val cameraPermission = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) {
            val target = newCameraUri()
            cameraUri = target
            takePicture.launch(target)
        } else {
            error = "Se necesita permiso de cámara para capturar el momento"
        }
    }

    fun openCamera() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) {
            val target = newCameraUri()
            cameraUri = target
            takePicture.launch(target)
        } else {
            cameraPermission.launch(Manifest.permission.CAMERA)
        }
    }

    Surface(Modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().statusBarsPadding().verticalScroll(rememberScrollState()).padding(18.dp),
        ) {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = onDone) { Icon(Icons.Default.ArrowBack, contentDescription = "Cerrar") }
                Column {
                    Text("Foto del día", style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold)
                    Text(date, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            Spacer(Modifier.height(12.dp))
            if (uri != null) {
                AsyncImage(
                    model = uri,
                    contentDescription = "Foto elegida",
                    modifier = Modifier.fillMaxWidth().height(360.dp),
                    contentScale = ContentScale.Crop,
                )
            } else {
                Surface(
                    Modifier.fillMaxWidth().height(260.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant,
                    shape = MaterialTheme.shapes.large,
                ) {
                    Column(Modifier.fillMaxSize(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center) {
                        Text("Elige una foto para el diario", style = MaterialTheme.typography.titleMedium)
                        Text(
                            "Se optimiza, se guarda en WebP y se limpian las coordenadas GPS.",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(top = 6.dp, start = 24.dp, end = 24.dp),
                        )
                    }
                }
            }
            Spacer(Modifier.height(12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                Button(onClick = { openCamera() }, modifier = Modifier.weight(1f)) {
                    Icon(Icons.Default.PhotoCamera, contentDescription = null)
                    Text("Cámara")
                }
                Button(
                    onClick = { pickImage.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) },
                    modifier = Modifier.weight(1f),
                ) {
                    Icon(Icons.Default.PhotoLibrary, contentDescription = null)
                    Text("Galería")
                }
            }
            Spacer(Modifier.height(12.dp))
            OutlinedTextField(
                value = caption,
                onValueChange = { caption = it },
                label = { Text("Descripción breve") },
                placeholder = { Text("Café leyendo en el parque") },
                modifier = Modifier.fillMaxWidth(),
                minLines = 2,
            )
            if (error.isNotBlank()) {
                Text(error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
            }
            Spacer(Modifier.height(16.dp))
            Button(
                onClick = {
                    val selected = uri ?: return@Button
                    saving = true
                    error = ""
                    vm.captureMoment(selected, caption, date, vm.settings.stripGps) { result ->
                        saving = false
                        result.onSuccess { onDone() }.onFailure { error = it.message ?: "No se pudo guardar la foto" }
                    }
                },
                enabled = uri != null && !saving,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (saving) "Guardando…" else "Guardar en el diario") }
            TextButton(onClick = onDone, modifier = Modifier.fillMaxWidth()) { Text("Cancelar") }
        }
    }
}
