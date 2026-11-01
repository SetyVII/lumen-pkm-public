# Lumen Mobile (Android nativo)

Compañero Android local-first de Lumen. Kotlin + Jetpack Compose Material 3, SQLite
cifrado, Keystore, sincronización LAN/USB, Share Sheet con OpenGraph local,
diario visual y widgets Glance.

No requiere Android Studio. Se compila con el SDK de línea de comandos y el
wrapper de Gradle incluido.

## Apariencia

La app sigue la referencia móvil de Capacities: fondo grafito `#151518`, Home
`#0D0D10`, superficies `#1A1A1D`, selección `#3F3F41`, acento `#FB2C36` y
badges de tipo con contenedor oscuro + pictograma pastel. Los tokens viven en
`ui/theme/Theme.kt` y `ui/LumenTypeVisuals.kt`. La revisión visual, las medidas
y los límites están en
[docs/qa/2026-10-30-mobile-visual/MOBILE-VISUAL-QA-2026-10-30.md](../docs/qa/2026-10-30-mobile-visual/MOBILE-VISUAL-QA-2026-10-30.md).

## Requisitos

- JDK 17 o superior (probado con JDK 25).
- Android SDK Platform 36 y Build-Tools 36.
- `adb` para instalar/probar.
- Opcional: `qrencode` para pintar el QR de emparejamiento en la terminal.

```bash
export ANDROID_HOME="$HOME/.local/share/android-sdk"
cd mobile
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

El APK queda en `mobile/app/build/outputs/apk/debug/app-debug.apk`.

## Emulador Android

En Fedora 44, `-gpu swiftshader_indirect` hace SIGSEGV por una denegación
SELinux de `execheap`. Usar `-gpu lavapipe` o `-gpu host`. Tabla de modos
probados y diagnóstico: [docs/guides/ANDROID-EMULATOR.md](../docs/guides/ANDROID-EMULATOR.md).

## Servidor y emparejamiento

### USB (sin Wi-Fi)

En la PC, desde la raíz del proyecto:

```bash
../../tools/scripts/usb-sync.sh
```

Después, en la app: **Ajustes → Emparejar → Usar USB** y pega el token de
`${LUMEN_WORKSPACE}/ops/personal/secrets/client.token` (o el que corresponda a tu instalación). La app
habla con `http://127.0.0.1:8765`; `adb reverse` lo redirige a la PC.

### Wi-Fi local

El servidor debe escuchar en la LAN, no sólo en loopback:

```bash
# Opcional pero recomendado: certificado local con la IP del PC
../../tools/scripts/generate-tls.sh 192.168.1.20

LUMEN_LISTEN=0.0.0.0:8765 \
LUMEN_MDNS=1 \
LUMEN_TLS_CERT=${LUMEN_WORKSPACE}/ops/personal/secrets/tls.crt \
LUMEN_TLS_KEY=${LUMEN_WORKSPACE}/ops/personal/secrets/tls.key \
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml up -d
```

Con el servidor Go nativo, las mismas variables se pasan al proceso. El anuncio
mDNS es `_lumen-sync._tcp.local.` y sólo se activa si el bind no es loopback o si
`LUMEN_MDNS=1`.

Genera el QR de emparejamiento en la PC:

```bash
LUMEN_API_URL=https://192.168.1.20:8765 \
LUMEN_TLS_CERT=${LUMEN_WORKSPACE}/ops/personal/secrets/tls.crt \
../../tools/scripts/pairing-qr.sh
```

El QR contiene `lumen://pair?api=...&token=...&fp=...`. La huella `fp` fija el
certificado TLS antes de la primera conexión; si se omite, la app usa TOFU
(confía en el primer certificado y avisa si cambia).

## Qué implementa

- **Cifrado local**: clave maestra de 32 bytes, PBKDF2-HMAC-SHA256 (210 000
  iteraciones), AES-256-GCM con nonce de 12 bytes y AAD `kind/id` / `asset/id`.
- **Keystore**: clave de dispositivo para sincronización en segundo plano y
  proyección del widget; clave biométrica con `BiometricPrompt` para desbloqueo.
- **SQLite + FTS5**: objetos, tipos, tombstones, assets, outbox, metadatos y
  proyección de tareas del widget.
- **Sync incremental**: `/v1/sync/status`, `/v1/sync/pull`, `/v1/sync/push` con
  `since_seq`, `op_id` idempotente, `base_revision`, merge de diario append,
  conflicto explícito y tareas por `updated_at`.
  El editor usa los bloques nativos del escritorio: imágenes en su posición,
  viñetas, encabezados, negrita/cursiva y divisores. Reconcilia el tramo editado,
  preserva los bloques vecinos y sus metadatos y sólo invalida `rich_text`.
  Las referencias se guardan por ID. El Markdown nuevo se interpreta con
  [CommonMark Java 0.27.1](https://github.com/commonmark/commonmark-java),
  licencia BSD de dos cláusulas. No implica paridad completa con el editor Qt.
- **Navegación**: historial por visita a un objeto; tanto Atrás del sistema como
  la flecha vuelven al objeto o pantalla de origen. `SaveableStateHolder`
  conserva la pestaña, búsqueda, posición de lista y selección del editor.
- **Medios**: miniaturas OG descargadas y cifradas; fotos de los últimos 90 días;
  PDFs y fotos antiguas bajo demanda (`pin`). Cada sync reintenta los medios
  elegibles que siguen faltando en disco, aunque no vengan en el último delta.
- **Descubrimiento**: `NsdManager` para `_lumen-sync._tcp.`, caché de última IP,
  prioridad USB `127.0.0.1:8765`, `ConnectivityManager` para Wi-Fi no medida.
- **Share Sheet**: para enlaces, actividad `Theme.NoDisplay` que encola el
  guardado y termina sin abrir una pantalla. WorkManager guarda un `web` con
  `status = "Para revisar"` y muestra el toast «Guardado en Weblinks correctamente».
  El payload en cola está cifrado. Una segunda tarea con conexión descarga y
  comprime la miniatura a WebP de hasta 800 px y la guarda cifrada; no añade
  entradas al diario. Mantiene la limpieza de tracking y resolución de Reddit.
  Compartir imágenes/texto conserva su flujo existente.
- **Diario visual**: foto del día, descripción, portada del diario, calendario
  mensual con miniaturas.
- **Widget Glance**: acciones Foto/Nota/Sync y lista de tareas con checkbox.
- **Notificaciones**: tareas que vencen hoy con Completar/Posponer y recordatorio
  opcional del diario.

## Estructura

| Ruta | Contenido |
|---|---|
| `data/CryptoBox.kt` | AES-GCM y PBKDF2. |
| `data/KeyVault.kt` | contraseña, Keystore, biometría, token. |
| `data/LumenDatabase.kt` | SQLite, FTS5, outbox, proyección del widget. |
| `data/LumenRepository.kt` | casos de uso: diario, tareas, enlaces, fotos. |
| `net/LumenApi.kt` | cliente HTTP y parseo del protocolo. |
| `net/SyncEngine.kt` | push/pull, descarga selectiva, USB/LAN. |
| `net/TlsPin.kt` | TOFU/pinning SHA-256. |
| `ui/` | pantallas Compose. |
| `widget/` | Glance AppWidget y acciones. |
| `sync/` | WorkManager, notificaciones y receivers. |

## Pruebas

```bash
cd mobile
./gradlew :app:testDebugUnitTest
./gradlew :app:assembleDebug
```

Las pruebas JVM cubren la limpieza canónica de URLs. La verificación manual en
dispositivo debe cubrir: alta con contraseña, crear tarea, captura de foto,
búsqueda, calendario, Share Sheet, sync USB, sync Wi-Fi, widget y notificación.

## Límites materiales

- El índice FTS5 es derivado y contiene título/contenido en claro para permitir
  búsqueda instantánea; los registros canónicos y los blobs sí están cifrados.
  Un adversario con acceso al SQLite local puede leer ese índice derivado.
- El servidor descifra el almacén en RAM para indexarlo. Esto no es E2EE.
- El certificado TLS local es autofirmado; la seguridad depende del pin/QR.
- HTTP está permitido para USB/localhost y LAN privada por compatibilidad. Usa
  HTTPS con pin para Wi-Fi.
- El APK de desarrollo no está firmado para Play Store.
- La sincronización de conflictos conserva ambas versiones en el servidor, pero
  la app móvil no ofrece todavía un editor de resolución lado a lado.

## Objetos y propiedades (01-11-2026)

Home y Crear usan los tipos registrados. Las fichas conservan esquemas
heredados y actuales: texto, números, booleanos, selectores, fechas/rangos y
relaciones por ID. Las colecciones ofrecen búsqueda y filtros de estado,
selección o relación; las fichas muestran referencias entrantes reales.
Archivos importa medios cifrados; los audios se reproducen localmente en RAM
con Media3. Crear un objeto desde una colección no modifica el diario.

Evidencia, dependencias y límites en
[QA de objetos móviles](../docs/qa/2026-11-01-mobile-objects/README.md).


## Vistas guardadas y bloques estructurados (01-11-2026)

En una colección, «Guardar vista» conserva filtros, búsqueda, orden y
presentación como un objeto `query` sincronizable. «Vistas guardadas» permite
reabrirlo; el mismo menú permite actualizar, renombrar y eliminar sólo la vista.
Cambiar una propiedad actualiza sus resultados también sin conexión. Se usa
el contrato de LumenQuery del escritorio, con grupos AND/OR, referencias,
orden manual, ordenación y límite; los diseños específicos del escritorio
permanecen guardados aunque Android muestre lista o vista general.

«Añadir bloque» permite insertar párrafos, tablas y grupos de columnas. Las
celdas son editables y admiten añadir/eliminar filas y columnas y alineación.
Los grupos de columnas se apilan en pantallas estrechas, permiten añadir,
mover y quitar columnas, y conservan los bloques internos e imágenes.
Escritorio lee y guarda esas estructuras como QTextTable, sin aplanarlas.

Pruebas y capturas: [QA de vistas y estructuras](../docs/qa/2026-11-01-mobile-views/README.md).


## Compilación para uso diario

Usar `:app:assembleOptimized` para instalar en el teléfono personal. Esta
variante aplica optimización R8, reducción de recursos y `debuggable=false`,
conservando el application ID y el certificado de las instalaciones locales
anteriores. `assembleDebug` se reserva al desarrollo y la instrumentación:
no sirve para evaluar el rendimiento de Compose.

```bash
ANDROID_HOME="$HOME/.local/share/android-sdk" ./mobile/gradlew -p mobile :app:assembleOptimized --max-workers=2
```

El resultado es `mobile/app/build/outputs/apk/optimized/app-optimized.apk`.
Para entregas se copia a `../releases/<versión>/` con SHA-256 y BUILD-INFO.
La variante `release` sigue requiriendo la firma de distribución; optimized
mantiene la firma local sólo para actualizar esta instalación sin borrar datos.

Las consultas repetidas a objetos/tipos/medios reutilizan snapshots descifrados
sólo en RAM. Las escrituras y confirmaciones de sincronización los invalidan;
bloquear la bóveda vacía esta caché. No se añade persistencia de notas en claro.
El ajuste al teclado se aplica una sola vez en AppRoot; la barra del editor no
vuelve a reservar su altura y el dock se oculta mientras está abierto.
