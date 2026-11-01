package com.sety.lumen.mobile

import android.os.Bundle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material3.Button
import androidx.compose.material3.Checkbox
import androidx.compose.material3.Icon
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
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import com.sety.lumen.mobile.ui.AppRoot
import com.sety.lumen.mobile.ui.theme.LumenTheme

class MainActivity : FragmentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            LumenTheme {
                LumenGate()
            }
        }
    }

    fun promptBiometricEnrollment(onSuccess: () -> Unit, onError: (String) -> Unit) {
        val app = application as LumenApplication
        val cipher = runCatching { app.vault.prepareBiometricEncryption() }.getOrElse {
            onError("No se pudo preparar la clave biométrica: ${it.message ?: "error de Keystore"}")
            return
        }
        val prompt = BiometricPrompt(
            this,
            ContextCompat.getMainExecutor(this),
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    super.onAuthenticationSucceeded(result)
                    val authorized = result.cryptoObject?.cipher ?: cipher
                    if (app.vault.completeBiometricEnrollment(authorized)) onSuccess()
                    else onError("No se pudo guardar la clave biométrica")
                }

                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    super.onAuthenticationError(errorCode, errString)
                    onError(errString.toString())
                }
            },
        )
        val info = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Activar huella")
            .setSubtitle("Confirma tu huella para proteger la clave maestra")
            .setNegativeButtonText("Cancelar")
            .setAllowedAuthenticators(BiometricManager.Authenticators.BIOMETRIC_STRONG)
            .build()
        prompt.authenticate(info, BiometricPrompt.CryptoObject(cipher))
    }

    fun promptBiometric(onSuccess: () -> Unit, onError: (String) -> Unit) {
        val app = application as LumenApplication
        val cipher = runCatching { app.vault.createBiometricCipher() }.getOrElse {
            onError("Configura la huella de nuevo en Ajustes")
            return
        }
        val prompt = BiometricPrompt(
            this,
            ContextCompat.getMainExecutor(this),
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    super.onAuthenticationSucceeded(result)
                    if (app.vault.unlockWithBiometricCipher(result.cryptoObject?.cipher ?: cipher)) onSuccess()
                    else onError("No se pudo desbloquear con huella")
                }

                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    super.onAuthenticationError(errorCode, errString)
                    onError(errString.toString())
                }
            },
        )
        val info = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Desbloquear Lumen")
            .setSubtitle("Usa tu huella para abrir tu espacio local")
            .setNegativeButtonText("Usar contraseña")
            .setAllowedAuthenticators(BiometricManager.Authenticators.BIOMETRIC_STRONG)
            .build()
        prompt.authenticate(info, BiometricPrompt.CryptoObject(cipher))
    }
}

@Composable
private fun LumenGate() {
    val app = androidx.compose.ui.platform.LocalContext.current.applicationContext as LumenApplication
    var setup by remember { mutableStateOf(app.vault.isSetup) }
    var unlocked by remember { mutableStateOf(app.vault.isUnlocked) }
    var error by remember { mutableStateOf("") }
    val activity = androidx.compose.ui.platform.LocalContext.current as? MainActivity

    when {
        !setup -> SetupScreen(
            onSetup = { password, biometric ->
                runCatching { app.vault.setup(password) }.onSuccess {
                    if (biometric) {
                        activity?.promptBiometricEnrollment(
                            onSuccess = { setup = true; unlocked = true; error = "" },
                            onError = { message -> setup = true; unlocked = true; error = "Huella no activada: $message" },
                        )
                    } else {
                        setup = true
                        unlocked = true
                    }
                }.onFailure { error = it.message ?: "No se pudo crear la clave" }
            },
            error = error,
        )
        !unlocked -> UnlockScreen(
            biometricEnabled = app.settings.biometricEnabled,
            error = error,
            onUnlock = { password ->
                if (app.vault.unlock(password)) {
                    unlocked = true
                    error = ""
                } else {
                    error = "Contraseña incorrecta"
                }
            },
            onBiometric = {
                activity?.promptBiometric(
                    onSuccess = { unlocked = true; error = "" },
                    onError = { error = it },
                )
            },
        )
        else -> AppRoot(
            onLock = {
                app.vault.lock()
                unlocked = false
            },
        )
    }
}

@Composable
private fun LumenMark() {
    Box(
        Modifier.size(74.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primaryContainer),
        contentAlignment = Alignment.Center,
    ) {
        Text("L", style = MaterialTheme.typography.displaySmall, color = MaterialTheme.colorScheme.onPrimaryContainer, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun SetupScreen(onSetup: (CharArray, Boolean) -> Unit, error: String) {
    var password by remember { mutableStateOf("") }
    var confirm by remember { mutableStateOf("") }
    var biometric by remember { mutableStateOf(true) }
    var localError by remember { mutableStateOf("") }
    Surface(Modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().padding(28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            LumenMark()
            Spacer(Modifier.height(22.dp))
            Text("Lumen Mobile", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.SemiBold)
            Text(
                "Tu diario, tareas y enlaces viven en este teléfono. Crea una contraseña maestra para cifrar la base local.",
                textAlign = TextAlign.Center,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 8.dp, bottom = 24.dp),
            )
            OutlinedTextField(
                value = password,
                onValueChange = { password = it },
                label = { Text("Contraseña maestra") },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(12.dp))
            OutlinedTextField(
                value = confirm,
                onValueChange = { confirm = it },
                label = { Text("Repetir contraseña") },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                modifier = Modifier.fillMaxWidth(),
            )
            Row(Modifier.fillMaxWidth().padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
                Checkbox(checked = biometric, onCheckedChange = { biometric = it })
                Text("Desbloqueo con huella", style = MaterialTheme.typography.bodyMedium)
            }
            val message = localError.ifBlank { error }
            if (message.isNotBlank()) {
                Text(message, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(vertical = 8.dp))
            }
            Button(
                onClick = {
                    localError = when {
                        password.length < 8 -> "Usa al menos 8 caracteres"
                        password != confirm -> "Las contraseñas no coinciden"
                        else -> ""
                    }
                    if (localError.isBlank()) onSetup(password.toCharArray(), biometric)
                },
                modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
            ) { Text("Crear espacio cifrado") }
        }
    }
}

@Composable
private fun UnlockScreen(biometricEnabled: Boolean, error: String, onUnlock: (CharArray) -> Unit, onBiometric: () -> Unit) {
    var password by remember { mutableStateOf("") }
    Surface(Modifier.fillMaxSize()) {
        Column(
            Modifier.fillMaxSize().padding(28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            LumenMark()
            Spacer(Modifier.height(20.dp))
            Text("Lumen está bloqueado", style = MaterialTheme.typography.headlineSmall, fontWeight = FontWeight.SemiBold)
            Text(
                "Introduce tu contraseña maestra o usa la huella.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = 6.dp, bottom = 22.dp),
            )
            OutlinedTextField(
                value = password,
                onValueChange = { password = it },
                label = { Text("Contraseña maestra") },
                leadingIcon = { Icon(Icons.Default.Lock, contentDescription = null) },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                modifier = Modifier.fillMaxWidth(),
            )
            if (error.isNotBlank()) {
                Text(error, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall, modifier = Modifier.padding(top = 8.dp))
            }
            Button(
                onClick = { onUnlock(password.toCharArray()) },
                modifier = Modifier.fillMaxWidth().padding(top = 14.dp),
            ) { Text("Desbloquear") }
            if (biometricEnabled) {
                TextButton(onClick = onBiometric, modifier = Modifier.padding(top = 6.dp)) {
                    Icon(Icons.Default.Fingerprint, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text("Usar huella")
                }
            }
        }
    }
}
