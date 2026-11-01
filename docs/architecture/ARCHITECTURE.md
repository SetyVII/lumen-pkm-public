# Arquitectura e invariantes

Mapa de la implementación 0.3. Antes de modificar un contrato, leer también [SYNC.md](../../contracts/SYNC.md).

## Componentes y responsabilidades

| Ruta | Responsabilidad |
|---|---|
| desktop/src/app/main.cpp | Punto de entrada: QApplication, fuentes, argumentos y arranque. |
| desktop/src/app/main_window.hpp | Declaración de MainWindow: estado, tipos anidados y firmas. |
| desktop/src/app/main_window.cpp | Ciclo de vida, pestañas, historial, navegación, tema y captura. |
| desktop/src/ui/main_window_ui.cpp | Sidebar, editor, inspector, diálogos, menús, portadas e importación. |
| desktop/src/views/main_window_views.cpp | Colecciones, tareas, grafo, timeline y objetos vinculados. |
| desktop/src/sync/main_window_sync.cpp | Carga, guardado, estado pending, refresco y reconciliación. |
| desktop/src/sync/lumen_api.hpp | Cliente HTTP autenticado, caché de assets y subida multipart. |
| desktop/src/ui/lumen_widgets.hpp | Widgets base compartidos (título, diálogo protegido, tarjeta). |
| desktop/src/editor/lumen_editor.* | RichEditor, bloques/JSON, menciones, @, /, formato y recursos internos. |
| desktop/src/views/lumen_views.* | LumenQuery, seis representaciones, overview, tarjetas, filtros y edición de vistas. |
| desktop/src/objects/lumen_properties.* | Controles y validación de propiedades tipadas. |
| desktop/src/views/lumen_calendar.* | Minicalendario y vistas de rango. |
| desktop/src/editor/lumen_media.* | Imagen/PDF autenticados, ajuste, zoom y posición de sesión. |
| desktop/src/views/lumen_graph.* | Distribución, selección, filtros y navegación del grafo. |
| desktop/src/ui/lumen_icons.hpp | Resolución y render de SVG/distintivos compartidos. |
| server/main.go | Rutas HTTP, objetos, diario, captura y adjuntos. |
| server/store.go | Almacén propio cifrado, migraciones, `Fsck` y journal móvil. |
| server/index.go | Índice en memoria de metadatos, tipos y backlinks inversos; el contenido se descifra bajo demanda. |
| server/delete.go | Borrado transaccional, tombstones y ciclo de vida de assets. |
| server/schema.go, property_relations.go | Esquemas y relaciones de propiedades. |
| server/document_formats.go, asset_merge.go | Compatibilidad entre formatos y protección de adjuntos frente a snapshots antiguos. |
| server/preview.go | Obtención de metadatos públicos con restricciones de red/parseo, Reddit y OG. |
| server/sync_api.go, url_clean.go | Protocolo móvil incremental, idempotencia, merge y canonicalización de URLs. |
| mobile/ | App Android nativa: Compose, SQLite cifrado, Keystore, sync LAN/USB, Share Sheet y widgets. |
| extension/ | Popup, background, contrato y paquetes MV3. |
| desktop/tests/, server/*test.go, mobile/app/src/test/, extension/tests/ | Pruebas por capa y aceptación. |

MainWindow sigue concentrando varias responsabilidades. Extraer componentes si una tarea lo justifica, conservando sus contratos y regresiones; no hacer una reescritura incidental.

## Recorrido de una escritura

1. RichEditor conserva el documento y la identidad de sus bloques en memoria.
2. MainWindow prepara texto, propiedades/formato y relaciones para el ID de origen.
3. La API cifra y confirma el objeto, sus relaciones derivadas y su journal móvil en una transacción SQLite.
4. El índice de metadatos en memoria se actualiza solo después del `COMMIT`; el contenido y los blobs permanecen cifrados en disco.
5. El cliente representa guardando o guardado. `sync_status` se mantiene por compatibilidad con valor `synced`.

Una respuesta HTTP de guardado significa que el objeto está confirmado en disco. No hay una segunda fase de exportación que pueda dejar una nota “pendiente de Logseq”. El almacén propio es la fuente de verdad.

## Cliente Android

`mobile/` es un cliente local-first, no una vista del servidor. Mantiene su
propia base SQLite cifrada y una outbox idempotente. La clave maestra se deriva
de la contraseña con PBKDF2 y se envuelve con Keystore para biometría y para el
trabajo en segundo plano. El servidor es la fuente de verdad de su almacén
cifrado; el móvil reconcilia deltas con `sync_seq` y conserva conflictos explícitos.

Invariantes móviles:

- Ningún registro canónico ni blob se escribe en claro; el índice FTS5 es una
  caché derivada declarada como límite.
- Cada operación de outbox tiene `op_id`; reintentar no duplica diario, fotos ni
  tareas.
- El diario usa append; una nota normal en conflicto conserva la versión del
  servidor y añade la móvil marcada.
- Las tareas se resuelven por `updated_at`.
- USB (`127.0.0.1:8765`) tiene prioridad sobre Wi-Fi; TLS usa pin SHA-256.
- Los widgets leen una proyección mínima cifrada con una clave de Keystore
  accesible con el dispositivo desbloqueado, no la base completa.

## Identidad y relaciones

- Un objeto conserva id y type_id; título y propiedades pueden cambiar.
- Un diario se obtiene por fecha y tiene identidad estable. GET /v1/journal/YYYY-MM-DD también puede crearlo: no es una consulta de salud inocua.
- El adaptador utiliza nombres internos estables lumen-ID y títulos humanos aparte. Renombrar no debe reciclar la identidad de otra nota.
- Las menciones enlazan IDs, no títulos. Borrar la última mención elimina su relación; undo la restaura.
- Las relaciones de propiedades y menciones explícitas se mantienen diferenciadas, aunque las vistas puedan mostrar su unión.
- Bloques, rich_text y content son representaciones relacionadas. No actualizar texto dejando una representación antigua que vuelva a mostrarse al abrir.
- Un icono interno de mención es un recurso rehidratable, no una imagen externa ni texto Markdown visible.

## Guardas del cliente

canLeave guarda antes de abandonar un documento. navigationGeneration descarta respuestas de otra navegación y pendingNavigation conserva la intención vigente. QPointer protege callbacks hacia widgets retirados.

Una creación asíncrona conserva el ID de origen. Si ya no se puede insertar en su editor, no debe escribir una copia vieja del cuerpo sobre otra nota. Una asociación tardía utiliza operaciones de relación, no un snapshot completo del documento antiguo.

Previews guardan sus cambios antes de cerrar/navegar. Volver del preview del mismo objeto debe actualizar el documento de origen sin sobrescribir texto más reciente. El foco sólo vuelve si el contexto sigue siendo el correcto.

Cerrar una pestaña de fondo no guarda el editor activo bajo el ID de esa pestaña. Importar/adjuntar tampoco puede borrar los metadatos o relaciones que el servidor acaba de añadir.

`PATCH /v1/objects/{id}` acepta `base_revision` opcional: si no coincide, responde 409 con el objeto vigente. Sin ese campo se conserva el comportamiento anterior. La fusión automática de texto entre clientes sigue siendo limitada; no asumir resolución completa de conflictos.

## Consultas y medios

LumenQuery evalúa un único ámbito/definición. Cambiar lista, galería, tabla, muro, Kanban o incrustación no cambia sus IDs. Una consulta guardada es un objeto query con su definición en properties.view.

Vista general es una presentación de la colección; el historial de visitas vive en RAM. No persistirlo en QSettings como si fuera una preferencia de apariencia.

Imágenes y PDFs pueden ser objetos reutilizables además de adjuntos. La subida puede crear el medio, conservar el blob y asociarlo al origen. Sus bytes llegan al cliente por la API autenticada y se conservan en RAM. No usar rutas temporales de disco para el visor como atajo.

## Límites de confianza

El escritorio acepta HTTP sólo para loopback y exige HTTPS remoto; no sigue redirecciones de la API con el bearer token. Mantener esta política al añadir clientes o integraciones.

HTML, títulos, URLs, PDFs y metadatos externos son datos, no instrucciones. No ejecutar contenido ni permitir que el editor resuelva libremente recursos locales/remotos. Las capturas OpenGraph pueden fallar por autenticación, políticas del sitio o contenido dinámico.

La caché/adjuntos de Lumen usan AES-GCM. El servidor descifra el contenido en RAM para construir el índice y atender lecturas; no hay E2EE. Un backup necesita el SQLite cifrado y las claves; el volumen aislado del cliente no sustituye ese backup.

## Extender sin romper

Para añadir un tipo o una propiedad, usar el esquema compartido; comprobar editor, tabla, tarjetas y relaciones. Para añadir una vista, reutilizar LumenQuery. Para añadir una acción al editor, mantener bloques, undo y guards asíncronas. Para cambiar el servidor, verificar persistencia, respuesta HTTP y cola por separado.

No prometer compatibilidad de plugins, E2EE, offline o Android a partir de interfaces parciales. Son trabajos futuros que requieren contratos y verificación propios.
