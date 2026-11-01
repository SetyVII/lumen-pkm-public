# Rendimiento y teclado móvil — 01-11-2026

La actualización anterior se había instalado como APK debug de 113 MiB, sin
optimización R8. Se observó lentitud general al abrir, escribir, desplazarse e
interactuar. Se inspeccionó el Motorola físico por ADB Wi-Fi; no se usaron sus
notas como fixtures ni se escribieron textos de prueba.

## Evidencia de diagnóstico

La primera muestra `dumpsys gfxinfo` registró 1687 frames, 6,82 % janky,
percentil 95 de 85 ms y 99 de 150 ms, con algunos frames superiores a un segundo.
GPU: percentil 95 de 6 ms. PSS aproximada de 169 MiB. No se deduce de esos datos
una fuga de memoria o saturación de la GPU.

Una traza ART de muestreo (20,38 s) mostró 23 llamadas a `LumenDatabase.objects`
y decenas de descifrados en el hilo de UI. Los tiempos bajo muestreo incluyen
sobrecoste del perfilador y no se presentan como latencia normal. La captura
inicial también mostraba un hueco amplio entre la barra de edición y el IME.

Las capturas privadas, XML de accesibilidad, perfiles y logs están sólo en
`${LUMEN_WORKSPACE}/scratch/2026-11-01-real-mobile-performance/`.
No se copian a Git capturas o datos de la biblioteca personal.

## Cambios

- `release` habilita R8 y reducción de recursos. La variante `optimized` hereda
  esa configuración con `debuggable=false` y mantiene el certificado local para
  actualizar la instalación existente sin perder la bóveda. No se presenta
  ese certificado de desarrollo como firma de distribución pública.
- `LumenDatabase` reutiliza snapshots de lectura en RAM, con un máximo de 128
  entradas. Escritos, confirmaciones push, pull, tipos y medios invalidan las
  lecturas. La caché se vacía al bloquear KeyVault. Una lectura concurrente
  anterior al bloqueo no puede volver a publicar su resultado en la caché.
- MainActivity usa `adjustResize`; AppRoot aplica el espacio del IME una vez.
  EditorKeyboardBar conserva sólo la altura de sus controles y el dock se
  oculta al escribir. El ajuste cubre diario y fichas de objetos.
- No cambian el formato de datos ni el cifrado en disco. Esta iteración no
  introduce una cola asíncrona de escritura ni afirma eliminar todo trabajo
  síncrono de la UI.

## Validación y límites

- 35 pruebas unitarias aprobadas, incluidas invalidación, resultados null y una
  lectura concurrente con bloqueo.
- La integración de caché en SQLite del emulador aprobó escritura, confirmación
  push, pull, asset, bloqueo/desbloqueo y borrado.
- La suite de UI completa con caché previa aprobó los 11 recorridos existentes;
  su nuevo caso de confirmación falló inicialmente por no reproducir el campo
  object_id que añade SyncEngine. Se corrigió el fixture y ese caso aprobó.
- La suite posterior, que añadía el teclado, se interrumpió por SIGABRT nativo
  de HWUI/RippleAnimator (Scudo: corrupted chunk header) en Android 16 x86_64.
  Después System UI del emulador quedó en ANR y el caso de foco/IME agotó el
  tiempo. No se etiqueta esa suite ni la aserción geométrica como aprobadas.
  El emulador se apagó; no se desactivaron animaciones en el Motorola.
- El APK optimizado se instaló con `adb install -r` en el Motorola y abrió la
  biblioteca real. PackageManager confirma ausencia de flag DEBUGGABLE.
  El usuario indicó «estuve probando parece funcional» tras la actualización.
  El ajuste del teclado está instalado; no se obtuvo una captura final del
  teclado abierto porque el usuario estaba navegando durante la comprobación.

Muestra breve de tres ciclos Inicio/Diario y desplazamiento, sin perfilador:

| Métrica de gfxinfo | Debug anterior | Optimized nuevo |
|---|---:|---:|
| Frames | 572 | 514 |
| Janky frames | 7 (1,22 %) | 4 (0,78 %) |
| Percentil 50 | 9 ms | 6 ms |
| Percentil 95 | 12 ms | 8 ms |
| Percentil 99 | 200 ms | 69 ms |

Es una señal favorable en ese recorrido, no un benchmark exhaustivo: hubo
calentamiento diferente, estado de scroll distinto y el usuario también estaba
probando la app. No se atribuye un porcentaje universal de mejora a cada cambio.

Guía oficial: [rendimiento de Compose y compilaciones release/R8](https://developer.android.com/develop/ui/compose/performance).

## Entrega instalada

`${LUMEN_WORKSPACE}/releases/0.6.1-performance/lumen-mobile.apk`

SHA-256: `03789d879aaabfb7a5f81ac0376db4a78e0b3cb203f265c1c4ba54c100371b37`.
Incluye BUILD-INFO.json y SHA256SUMS. Se conservaron datos y certificado.
El servidor personal no se reinició ni actualizó como parte de esta corrección.
