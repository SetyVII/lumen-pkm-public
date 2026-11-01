# Unreleased — backend increment (2026-11-01)

- Block REST CRUD with revision guards, nested block search, live source resolution and incoming-reference counts.
- Reusable audio/video/file objects across upload, promotion and mobile sync; safe generic downloads and shared-asset cleanup.
- Yearly, weekday, month-end and last-weekday recurrence, with atomic skip/excuse outcomes.
- Habit objects with date-keyed counts, targets, scheduled weekdays and current/best streak statistics.
- Backend only; no personal deployment or new desktop/mobile interface. See `docs/implementation/backend/blocks-media-recurrence-habits.md`.

# 0.6.1 — 2026-10-31

- **Medios sellados en reposo (móvil)**: las fotos y adjuntos se cifran en disco con la clave maestra del espacio (AES-256-GCM, mismo contenedor que la base y AAD ligado al nombre de fichero); el texto claro solo vive en RAM al mostrar o subir, con copia temporal en el caché privado al compartir y limpieza al arrancar. Los medios heredados de versiones anteriores se re-sellan al primer uso.
- **Sincronización tolerante**: el servidor descarta enlaces y valores de propiedades `object` que aún no resuelven en los puts de sync (igual que ya hacía con tombstones) y desambigua títulos duplicados como `"<título> (2)"`, de modo que ninguna operación puede bloquear la outbox para siempre. La vía HTTP directa del escritorio mantiene la validación estricta.
- **Re-emparejamiento completo**: `POST /v1/types` acepta un `id` anclado por el cliente (idempotente si ya existe), el móvil registra sus tipos personalizados al empezar cada sync y "Reenviar todo al servidor" re-encola toda la biblioteca local. Probado de punta a punta re-emparejando un Motorola contra un servidor desechable con cola bloqueada.
- **Orden topológico en la outbox**: los objetos preceden a las operaciones que los enlazan, preservando las relaciones en el primer pase; los ciclos los rompe el servidor descartando una dirección.
- **Diagnóstico visible**: los avisos de operaciones rechazadas persisten en el mensaje de sync de Ajustes en lugar de taparse con el resumen final, y las operaciones de outbox ilegibles se registran en el log.
- Verificado en dispositivo real: push/pull con la biblioteca personal, fusión de conflicto de diario, tombstones, widget en vivo con tarea real, y barrido forense de la base cifrada (sin contenido en claro; solo metadatos de esquema como nombres de fichero, por decisión de esquema).

# 0.6.0 — 2026-10-30

- **Etiquetas en todos los objetos y fila bajo el título**: los tipos integrados que no la tenían (`journal`, `person`, `task`, `image`, `pdf`, `query`) reciben la propiedad `tags` al arrancar (`NewApp` la añade a almacenes previos sin tocar definiciones del usuario). La página de objeto y la vista previa muestran bajo el título una fila de chips: etiquetas asignadas (abren el objeto Etiqueta), etiquetas mencionadas en el contenido (borde discontinuo; al pulsar saltan al bloque que las contiene con `focusBlock` y lo resaltan) y acceso al selector reutilizado de propiedades (`LumenProperties::chooseRelationValue`). `RichEditor::tagMentions()` resuelve las menciones con su bloque, deduplicadas y con resolución de tipo para contenido antiguo.
- **Workspace reestructurado** según `estructura-lumen.md`: el código vive solo en `lumen-pkm/`; la instalación personal, backups y secretos en `ops/`; las referencias de terceros en `research/`; los entregables en `releases/`; los temporales en `scratch/`. `desktop/src` se divide por dominio (`app`, `editor`, `objects`, `views`, `ui`, `sync`), hay presets CMake (`dev`, `test`, `release`, `asan`) y los builds van a `.build/`. El binario del escritorio y `extension/dist/` dejan de estar versionados.
- **Logseq retirado por completo**: desaparecen el adaptador, el worker, la outbox de exportación, el runtime Node y el grafo Logseq. El almacén SQLite cifrado propio es la única fuente de verdad.
- **Índice en memoria** (`memIndex`) con metadatos, tipos y backlinks inversos; el contenido, las propiedades y los blobs permanecen cifrados en SQLite y se descifran bajo demanda. Un PDF no se mantiene en RAM entre peticiones. Se actualiza sólo tras un `COMMIT`.
- **Migraciones versionadas** (`schema_version`) y limpieza de `sync_status`/`sync_error` y de la outbox antigua sin generar cambios falsos para el móvil.
- **Borrado O(k)** de backlinks, tombstones con protección frente a resurrecciones, ciclo de vida de assets/blobs y `Store.Fsck()` / `-fsck`.
- **CAS opcional** en `PATCH /v1/objects/{id}` con `base_revision` y 409 con el objeto vigente; el escritorio lo envía en el guardado principal.
- **Sincronización móvil**: `applyPull` no pisa objetos con outbox pendiente, `confirmPush` marca `synced` al confirmar el push, se filtran enlaces a tombstones y se fusionan listas de IDs en conflictos.
- **Tipos**: `video` estándar y propiedad `tags` (`target_type: tag`) en los tipos de contenido. Las definiciones siguen siendo por tipo.
- **Escritorio — línea horizontal a nivel 1**: al escribir `---` (o insertar el bloque desde `/`) el separador sale de la lista envolvente y ya no hereda sangrías, niveles ni fondos del bloque anterior (tarea, cita, grupo, encabezado). La línea ocupa el ancho real de la columna de texto y se reajusta al redimensionar o alternar el diseño amplio, también en notas guardadas con el ancho anterior de 680 px; el reajuste no ensucia el historial de undo ni dispara guardados.

# 0.5.0 — 2026-10-30

- **Lumen Mobile Android nativo** (`mobile/`): Kotlin + Jetpack Compose Material 3, sin WebView ni Android Studio. SQLite + FTS5, cifrado AES-256-GCM, PBKDF2 y Android Keystore; desbloqueo por contraseña/huella.
- **Sincronización incremental** en el servidor Go: `sync_changes` monotónico, `GET /v1/sync/status`, `POST /v1/sync/pull` y `POST /v1/sync/push` con `op_id` idempotente, `base_revision`, merge append de diarios, conflicto explícito y tareas por `updated_at`.
- **LAN/USB**: anuncio mDNS `_lumen-sync._tcp.local.`, descubrimiento `NsdManager`, prioridad `adb reverse` (`tools/scripts/usb-sync.sh`), TLS opcional con pin SHA-256/TOFU y QR de emparejamiento (`tools/scripts/generate-tls.sh`, `tools/scripts/pairing-qr.sh`).
- **Diario visual móvil**: Foto del día con optimización WebP, limpieza de GPS, tarjetas con pie de foto, portada del diario y calendario mensual de recuerdos.
- **Share Sheet OG local**: enlaces guardados como `web` con `status = "Para revisar"`, limpieza de tracking, normalización de youtu.be/redd.it, pipeline Reddit (`vxreddit` + Arctic Shift) y miniatura WebP descargada en el teléfono.
- **Widgets y notificaciones**: widget Glance con Foto/Nota/Sync y tareas; notificaciones de tareas con Completar/Posponer y recordatorio opcional del diario.
- **Servidor OG mejorado**: `cleanURL`, deduplicación de enlaces, `status` por defecto en el tipo `web`, aceptación de preview/imagen pre-descargada por el cliente y soporte Reddit.

# 0.4.0 — 2026-10-30

- Borrado atómico e inmutabilidad de referencias inline: cualquier pulsación de `Backspace` o `Delete` sobre o tocando un chip de referencia (`[icono] título`) elimina el chip íntegro en un único bloque de edición atómico (revertible limpiamente con `Ctrl+Z`), impidiendo que el cursor rompa o borre letra por letra el título del objeto enlazado.
- Atajos Markdown en vivo ampliados:
  - Encabezados: `# ` (H1), `## ` (H2), `### ` (H3), `#### ` (H4) con conversión instantánea al pulsar espacio al inicio del bloque.
  - Listas y tareas: `- `, `* `, `+ ` para viñetas; `1. ` para numerada; `a. ` para alfabética; `[] `, `[ ] `, `()` para tarea pendiente; `[x] ` para tarea completada.
  - Cita y bloque agrupado: `> ` o `" ` para cita; `>> ` para contenedor/grupo (`group`).
  - Línea horizontal / divisor: `---` o `***` al inicio de un bloque vacío inserta un separador visual de ancho completo (`divider`).
  - Formato inline y disparadores: `**texto**` / `__texto__` (negrita), `~~texto~~` (tachado), `` `código` `` (código inline) y `[[` para invocar el selector de referencias de objetos.
- Menú `/` completo al estilo Capacities:
  - Activación tanto al inicio de línea como tras cualquier espacio en blanco en mitad de una frase.
  - Bloques estructurales incorporados: `Ícono` (selector rápido de emojis/iconos), `Grupo` (callout contenedor con fondo suave), `Matemáticas` (fórmula LaTeX), `Código` (unificado), `Línea horizontal` (`divider`), `Dos columnas` (layout 50/50), `Tabla` (rejilla 3×3 editable), `Estándar`, `Pequeño`, `Encabezados 1–4`, `Listas`, `Cita`, `Tarea`.
  - Sección «Crear un objeto»: listado dinámico con todos los tipos base y registrados (`Nuevo Podcast`, `Nuevo Etiqueta`, `Nuevo Persona`, `Nuevo Libro`, `Nuevo Página`, `Nuevo Tarea` con distintivo cohete, `Nuevo Tweet`, `Nuevo PDF`, `Nuevo Audio`, `Nuevo Archivo`, etc.), con soporte para drill-down `/<tipo>/<título>`.
- Nuevos tipos base incorporados en servidor y escritorio: `tag`, `podcast`, `tweet`, `audio`, `file`, `page` con iconos oficiales y esquemas dedicados.

# 0.3.1 — 2026-10-29

- Flujo de creación y referencias inline al estilo Capacities: selector `@` con soporte de delimitador `/` (`@<tipo>/`) para filtrado y drill-down directo a «Buscar o crear», badge coloreado de tipo y exploración de colecciones.
- Modal de creación contextual en tarjeta centrada (`GuardedDialog`): permite capturar título y cuerpo de texto multilínea sin perder el contexto del documento de origen, insertando automáticamente el chip inline `[Icon] Título`.
- Enlaces entrantes (Backlinks) contextuales enriquecidos: tarjetas de referencia con icono y distintivo de tipo, detección de relaciones tipadas (`Libro → Autor`) y fragmentos/citas de transclusión que enlazan directamente al bloque exacto de origen.
- Tipos de objeto base ampliados en servidor y escritorio: soporte para `atomic` (Nota atómica con icono `box`), `task` (Tarea con `circle-check`), `quote` (Cita con `quote`), y propiedades enriquecidas `category` y `topic` en enlaces web (`web`).
- Gestión de tareas contextuales: atajo `() ` en el editor y bloque `/task` con checkbox circular interactivo (`○` / `✔`) alternable al hacer clic.
- Detección inteligente de enlaces web pegados: conversión automática de URLs HTTP/HTTPS individuales a tarjetas incrustadas mediante captura web.

# 0.3.0 — 2026-10-29

- Selector @ sin quitar el foco de la nota: búsqueda inline, recientes en RAM, submenús por tipo, creación y fecha con calendario. Menciones con SVG de tipo conservadas al guardar/cargar y deshacer.
- Menú / de 430×386 con iconos, teclado, pequeño, H1–H4 y listas alfabéticas además de los bloques existentes. Texto, cursor, IDs y estructura conservados.
- Vista general/Todo con tabs en una sola fila, recientes reales, tarjetas con marco de medios, tipo, URL copiable y chips de selección. Estado vacío con icono oficial y crear/importar; filtros sin coincidencias con salida propia.
- Distintivos de tipo navegables, clic de mención a preview y Ctrl+clic a pestaña. Crear desde una colección ya no arrastra la fecha del diario.
- Importación de notas UTF-8 y medios con ámbito de tipo, sin escribir en el diario de origen. Iconos oficiales Tabler/MIT y package-open Lucide/ISC incluidos.

# 0.2.0 — 2026-10-29

- Ventana nativa con escala, Inter, colores y distribución basados en la inspección de Capacities; navegación, pestañas, historial, hover, foco y reducción de movimiento.
- Calendario clic/teclado, día/semana/mes y tres días centrados en la fecha seleccionada. Guardado durable antes de abandonar una nota y prioridad de la última navegación solicitada.
- Outbox cifrada atómica y sincronización serial en segundo plano con Logseq; revisiones, recuperación, tombstones y estados observables.
- Bloques con IDs, selector @ junto al cursor, creación de tipos/objetos, barra de selección, formatos, enlaces, embeds, preview editable y backlinks por bloque.
- Propiedades tipadas y relaciones; seis vistas con evaluador común, AND/OR, búsqueda, orden, grupos, columnas, edición, consultas guardadas y Kanban/deshacer.
- Imágenes/PDF como objetos reutilizables; promoción de adjuntos existentes, miniaturas en RAM y portadas proporcionales. Visores con nombre, cierre, zoom, ajuste y navegación PDF que conserva la intención del usuario.
- Grafo con fuerzas, filtros, navegación por teclado y exportación; eliminado el límite decorativo de 45 objetos.
- Protección contra sobrescritura al cerrar previews, cerrar pestañas de fondo o adjuntar mientras se guarda. API y edición externa en Logseq invalidan formatos anteriores cuando cambia el texto plano.

No incluye E2EE, aplicación Android, offline, colaboración o un host de plugins aislados. El grafo Logseq del servidor permanece en claro. Ver README y docs/tracking/VERIFICATION.md.
