package com.sety.lumen.mobile.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Sync
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sety.lumen.mobile.QrScannerActivity
import com.sety.lumen.mobile.data.SyncStatus

@Composable
fun SettingsScreen(
    vm: LumenViewModel,
    revision: Long,
    onLock: () -> Unit,
    onBack: () -> Unit,
) {
    var pairing by remember { mutableStateOf(false) }
    var wipe by remember { mutableStateOf(false) }
    var resendAllConfirm by remember { mutableStateOf(false) }
    var biometric by remember { mutableStateOf(vm.settings.biometricEnabled) }
    var autoSync by remember { mutableStateOf(vm.settings.autoSync) }
    var reminder by remember { mutableStateOf(vm.settings.journalReminder) }
    var reminderHour by remember { mutableStateOf(vm.settings.reminderHour) }
    var securityError by remember { mutableStateOf("") }
    val counts = remember(revision) { vm.counts() }
    val context = LocalContext.current

    if (pairing) {
        PairingScreen(vm, onBack = { pairing = false })
        return
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .background(MaterialTheme.colorScheme.surface)
                .statusBarsPadding(),
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 12.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                LumenCircleButton(
                    icon = Icons.AutoMirrored.Filled.ArrowBack,
                    contentDescription = "Volver",
                    onClick = onBack,
                )
                Text(
                    "Ajustes",
                    modifier = Modifier.padding(horizontal = 12.dp),
                    style = MaterialTheme.typography.headlineSmall,
                    fontWeight = FontWeight.Bold,
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }
        }

        LazyColumn(
            modifier = Modifier
                .weight(1f)
                .fillMaxWidth(),
            contentPadding = PaddingValues(18.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp),
        ) {
            item {
                Text(
                    "Todo es local. El servidor de la PC solo transporta deltas cifrados.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        item {
            SettingsCard("Conexión") {
                Text(vm.settings.serverUrl, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
                Text(
                    if (vm.settings.serverId.isBlank()) "Sin emparejar" else "Servidor ${vm.settings.serverId.take(8)} · ${if (vm.settings.tokenEncrypted.isBlank()) "sin token" else "token guardado"}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(10.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { pairing = true }) {
                        Icon(Icons.Default.Link, contentDescription = null)
                        Text("Emparejar")
                    }
                    TextButton(onClick = { vm.syncNow() }) {
                        Icon(Icons.Default.Sync, contentDescription = null)
                        Text("Sincronizar")
                    }
                }
                if (vm.settings.tokenEncrypted.isNotBlank()) {
                    TextButton(onClick = { vm.disconnect() }) { Text("Desconectar", color = MaterialTheme.colorScheme.error) }
                }
            }
        }
        item {
            SettingsCard("Sincronización") {
                SettingsSwitch("Sincronizar al volver a Wi-Fi", autoSync) {
                    autoSync = it
                    vm.settings.autoSync = it
                }
                SettingsRow("Cola local", "${counts.second} operación(es)")
                SettingsRow("Objetos", "${counts.first} · Medios ${counts.third}")
                SettingsRow("Última sync", vm.settings.lastSyncAt.ifBlank { "Nunca" })
                if (vm.syncMessage.isNotBlank()) {
                    Text(vm.syncMessage, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                if (vm.syncError.isNotBlank()) {
                    Text(vm.syncError, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.error)
                }
                TextButton(onClick = { vm.testConnection { } }) {
                    Icon(Icons.Default.Refresh, contentDescription = null)
                    Text("Probar conexión")
                }
                if (vm.settings.tokenEncrypted.isNotBlank()) {
                    TextButton(onClick = { resendAllConfirm = true }) {
                        Text("Reenviar todo al servidor…")
                    }
                }
            }
        }
        item {
            SettingsCard("Seguridad") {
                SettingsSwitch("Desbloqueo con huella", biometric) { enabled ->
                    if (enabled) {
                        val activity = context as? com.sety.lumen.mobile.MainActivity
                        if (activity == null) {
                            securityError = "Abre Lumen desde la app para activar la huella."
                            biometric = false
                            return@SettingsSwitch
                        }
                        activity.promptBiometricEnrollment(
                            onSuccess = {
                                biometric = true
                                vm.settings.biometricEnabled = true
                                securityError = ""
                            },
                            onError = {
                                biometric = false
                                securityError = "Huella no activada: $it"
                            },
                        )
                    } else {
                        vm.vault.disableBiometric()
                        biometric = false
                        vm.settings.biometricEnabled = false
                        securityError = ""
                    }
                }
                if (securityError.isNotBlank()) {
                    Text(securityError, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.labelSmall)
                }
                TextButton(onClick = onLock) {
                    Icon(Icons.Default.Lock, contentDescription = null)
                    Text("Bloquear ahora")
                }
            }
        }
        item {
            SettingsCard("Diario visual") {
                SettingsSwitch("Recordatorio del diario", reminder) {
                    reminder = it
                    vm.setReminder(it, reminderHour)
                }
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                    Text("Hora del recordatorio", style = MaterialTheme.typography.bodyMedium)
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        TextButton(onClick = { reminderHour = (reminderHour + 23) % 24; vm.setReminder(reminder, reminderHour) }) { Text("−") }
                        Text("%02d:00".format(reminderHour), style = MaterialTheme.typography.titleMedium)
                        TextButton(onClick = { reminderHour = (reminderHour + 1) % 24; vm.setReminder(reminder, reminderHour) }) { Text("+") }
                    }
                }
                Text(
                    "Las fotos se optimizan y se limpian sus coordenadas GPS antes de guardarse.",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        item {
            SettingsCard("USB sin Wi-Fi") {
                Text(
                    "Conecta el teléfono por cable y ejecuta en la PC:\nlumen-pkm/tools/scripts/usb-sync.sh\n\nLa app usa primero 127.0.0.1:8765 por USB y luego busca el servidor por Wi-Fi.",
                    style = MaterialTheme.typography.bodySmall,
                )
            }
        }
        item {
            SettingsCard("Datos locales") {
                Text(
                    "Borrar elimina la base cifrada, las fotos y la clave maestra de este teléfono. La PC no se modifica.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                TextButton(onClick = { wipe = true }) { Text("Borrar datos del teléfono", color = MaterialTheme.colorScheme.error) }
            }
        }
        item {
            Text(
                "Lumen Mobile ${com.sety.lumen.mobile.BuildConfig.VERSION_NAME} · SQLite + AES-256-GCM + Keystore · Medios sellados en reposo",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
    }

    if (wipe) {
        ConfirmDialog(
            title = "¿Borrar todo?",
            body = "Esta acción no se puede deshacer. Tendrás que emparejar el teléfono de nuevo.",
            confirmLabel = "Borrar",
            onDismiss = { wipe = false },
            onConfirm = {
                wipe = false
                vm.wipe()
                onLock()
            },
        )
    }

    if (resendAllConfirm) {
        ConfirmDialog(
            title = "¿Reenviar todo al servidor?",
            body = "Pone en cola todos los objetos y medios locales para subirlos al servidor emparejado. Lo que ya coincida no se duplica y lo divergente se fusiona conservando ambas versiones.",
            confirmLabel = "Reenviar",
            onDismiss = { resendAllConfirm = false },
            onConfirm = {
                resendAllConfirm = false
                vm.resendAll { }
            },
        )
    }
}

@Composable
private fun SettingsCard(title: String, content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = MaterialTheme.shapes.medium,
        colors = androidx.compose.material3.CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surface,
        ),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
            )
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

@Composable
private fun SettingsRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun SettingsSwitch(label: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = MaterialTheme.typography.bodyMedium)
        Switch(checked = checked, onCheckedChange = onChange)
    }
}

@Composable
private fun PairingScreen(vm: LumenViewModel, onBack: () -> Unit) {
    var api by remember { mutableStateOf(vm.settings.serverUrl) }
    var token by remember { mutableStateOf("") }
    var fingerprint by remember { mutableStateOf(vm.settings.certFingerprint) }
    var error by remember { mutableStateOf("") }
    var busy by remember { mutableStateOf(false) }
    var status by remember { mutableStateOf<SyncStatus?>(null) }
    val context = LocalContext.current

    val scanner = rememberLauncherForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        val raw = result.data?.getStringExtra("uri") ?: return@rememberLauncherForActivityResult
        val uri = runCatching { Uri.parse(raw) }.getOrNull() ?: return@rememberLauncherForActivityResult
        uri.getQueryParameter("api")?.let { api = it }
        uri.getQueryParameter("token")?.let { token = it }
        uri.getQueryParameter("fp")?.let { fingerprint = it }
        error = ""
    }

    LazyColumn(
        modifier = Modifier.fillMaxWidth().statusBarsPadding(),
        contentPadding = PaddingValues(18.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item {
            Row(verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = onBack) { Icon(Icons.Default.ArrowBack, contentDescription = "Volver") }
                Text("Emparejar con la PC", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.SemiBold)
            }
            Text(
                "En la PC ejecuta ./scripts/pairing-qr.sh y escanea el QR. También puedes copiar la URL y el token.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        item {
            Button(onClick = { scanner.launch(Intent(context, QrScannerActivity::class.java)) }, modifier = Modifier.fillMaxWidth()) {
                Icon(Icons.Default.QrCodeScanner, contentDescription = null)
                Text("Escanear QR")
            }
        }
        item {
            OutlinedTextField(
                value = api,
                onValueChange = { api = it; error = "" },
                label = { Text("URL del servidor") },
                placeholder = { Text("http://192.168.1.20:8765") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                TextButton(onClick = { api = "http://127.0.0.1:8765" }) { Text("Usar USB") }
                TextButton(onClick = { api = api.replace("127.0.0.1", "192.168.1.20") }) { Text("Ejemplo Wi-Fi") }
            }
        }
        item {
            OutlinedTextField(
                value = token,
                onValueChange = { token = it; error = "" },
                label = { Text("Token de emparejamiento") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        item {
            OutlinedTextField(
                value = fingerprint,
                onValueChange = { fingerprint = it },
                label = { Text("Huella TLS SHA-256 (opcional)") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        if (error.isNotBlank()) {
            item { Text(error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall) }
        }
        status?.let {
            item {
                Text(
                    "Conectado a ${it.serverId.take(8)} · ${it.headSeq} cambios en la PC",
                    color = MaterialTheme.colorScheme.primary,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
        item {
            Button(
                onClick = {
                    busy = true
                    error = ""
                    vm.pair(api, token, fingerprint) { result ->
                        busy = false
                        result.onSuccess {
                            status = it
                            vm.syncNow()
                        }.onFailure {
                            error = it.message ?: "No se pudo emparejar"
                        }
                    }
                },
                enabled = !busy,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (busy) "Conectando…" else "Emparejar y sincronizar") }
        }
        item {
            Text(
                "El token se guarda cifrado con una clave de Android Keystore. La huella TLS evita aceptar un certificado distinto en la primera conexión.",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}
