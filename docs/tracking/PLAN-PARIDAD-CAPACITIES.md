# Plan de paridad con Capacities

Fecha: 2026-10-31. Fuentes de estudio: documentación oficial completa de
Capacities (`https://docs.capacities.io/llms-full.txt`, 16 k líneas, todas las
páginas de docs.capacities.io), portal de desarrolladores con la API v1.0.0
(`https://developers.capacities.io` + OpenAPI de `api.capacities.io`) y notas
de versiones (`https://capacities.io/api/releases.json`, hasta release-73 /
v1.72.7, octubre 2026). Este plan NO sustituye a `PENDIENTES.md` (backlog
operativo) ni a `INCIDENCIAS.md`: es el mapa de funcionalidades importantes de
Capacities que faltan en Lumen y cómo implementarlas, respetando la política
de producto (sin IA, sin telemetría, sin servicios externos, monousuario).

Reglas que aplican a todo el plan (AGENTS.md):

- Contratos primero: si un ítem cambia el formato de objeto o la API, se
  actualiza `contracts/SYNC.md` y `contracts/fixtures/` en el mismo cambio.
- Builds: `cmake --preset test` en `lumen-qt-make:latest`, `--parallel 2`;
  `go test ./...`; `pnpm test` en `extension/`.
- Cada fase termina con una pasada de aceptación registrada en
  `docs/tracking/VERIFICATION.md`.

## 1. Modelo de Capacities en una página

Capacities organiza todo como **objetos tipados**. Conceptos clave:

- **Tipos básicos** (built-in): page, whiteboard, tag, image, weblink, audio,
  PDF, file, tweet, AI chat, query, table. **Tipos custom** con propiedades.
- **Propiedades**: por defecto (título, alias, descripción, icono, creado-en,
  tags) y normales (texto, content/blocks, número, checkbox, fecha con hora y
  rango, label/select, object-select con **enlace bidireccional** y conjunto
  fijo, portada, recurso de archivo).
- **Editor de bloques**: texto con interfaces combinables (jerarquía, toggle,
  lista, cita, emoji, to-do), highlight blocks, código, matemáticas, tablas,
  layout multi-columna, group block, y **referencias a bloques** con
  transclusión.
- **Organización**: tipos (qué es) → tags (tema, entre tipos) → labels (categoría
  dentro de un tipo) → **colecciones** (lista manual dentro de un tipo) →
  **consultas** (filtros guardados con sort/group/limit/show) → secciones
  (sidebar y dashboard por tipo).
- **Tiempo**: calendario (día/3 días/semana/mes), daily notes con estado
  "revisado", date links, propiedades de fecha con rangos, tareas recurrentes.
- **Weblinks**: objeto con URL, título, descripción, portada + propiedades
  predefinidas **Category / Topic / Keywords** (en Capacities las llena su
  análisis de IA; son fijas, no personalizables), pestañas Browser/Reader/
  Summary, anotaciones subrayadas vinculadas a bloques de nota.
- **Vistas**: inline, link block, small/wide card, embed (objeto); table,
  gallery, wall, list, kanban, embed (datos) con **cálculos** (suma, media,
  mediana, min/max, conteos, % checkbox); grafo completo del espacio con
  filtros, red de tags, búsqueda y export PNG; **whiteboards** (lienzo
  infinito); modo presentación.
- **Búsqueda**: command palette full-text offline, anchor search (`^`),
  frases exactas, filtro por tipo, búsqueda extendida, find-in-page con
  reemplazo, linking search (`@`/`[[`) con creación tipada (`@type/Nombre`),
  búsqueda de bloques (`((`), alias.
- **Portabilidad**: export completo Markdown + CSV + media con links locales;
  export programado; bulk import desde Notion/Obsidian/Evernote/Apple
  Notes/Keep/OneNote/Tana/Mem; papelera; historial de versiones.
- **Plataforma**: offline-first con SQLite local, resolución visual de
  conflictos, apps de escritorio/móvil/tablet, extensión web, integraciones de
  captura (email, WhatsApp, Telegram, Kindle, Readwise), API REST nueva
  (developers.capacities.io) y **servidor MCP** (OAuth 2.1 + PKCE).

## 2. Estado de Lumen (resumen del inventario v0.6)

Ya implementado y vigente (no se repite en el plan): objetos tipados con 19
tipos por defecto y esquemas editables; propiedades text/number/date/boolean/
select/object-relation; tags como objetos; backlinks con contexto de bloque;
editor de bloques con `@` y `/`, código, LaTeX, tablas, dos columnas, iconos;
diario + calendario día/3 días/semana/mes + timeline; seis vistas + consultas
guardadas con filtros AND/OR; grafo con filtros y PNG; captura web OG (servidor,
Android) con limpieza de URLs y pipeline Reddit; extensión MV3 clipper; sync
móvil con cifrado, mDNS y resolución de conflictos del lado servidor; temas.

## 3. Matriz de paridad

| Funcionalidad Capacities | Estado Lumen | Fase |
|---|---|---|
| Colecciones dentro de un tipo | Ausente (audit 2026-10-29) | F1 |
| Plantillas por tipo | Ausente (INCIDENCIAS) | F1 |
| Alias de objetos/tags | Ausente | F1 |
| Conversión de tipo + fusión de objetos | Ausente | F1 |
| Papelera con restauración | Parcial (tombstones sin UI) | F1 |
| Consultas: filtros por enlaces/backlinks, bloques vacíos | Ausente | F2 |
| Cálculos en vistas (suma/media/%) | Ausente | F2 |
| Consultas variables ("este objeto") embebidas | Ausente | F2 |
| Dashboards por tipo con secciones | Parcial (sin secciones) | F2 |
| Fechas con rango | Ausente | F2 |
| Tareas recurrentes | Ausente | F2 |
| Find-in-page + reemplazo | Ausente | F2 |
| Referencias a bloques + transclusión | Ausente | F3 |
| Mermaid en bloques de código | Ausente | F3 |
| Weblink: notas en el cuerpo (Reader) | Parcial (audit) | F3 |
| Weblink: anotaciones/highlights | Ausente | F3 |
| Importación Markdown → bloques | Parcial (texto plano) | F3 |
| Export completo del espacio (MD+CSV+media) | Ausente | F3 |
| Unlinked mentions | Ausente | F3 |
| Historial de versiones del objeto | Parcial (revisiones sin UI) | F4 |
| Offline de escritorio (caché en disco) | Ausente (solo RAM) | F4 |
| Grabadora de audio | Ausente (tipo `audio` existe) | F4 |
| Modo presentación | Ausente | F4 |
| Grafo: red de tags, búsqueda, animación | Parcial | F4 |
| Espacios (multi-workspace) | Ausente | Decisión |
| Publicar/compartir enlaces públicos | Ausente | Decisión |
| API/MCP para agentes externos | Parcial (API HTTP existe) | Decisión |
| Captura por email/Telegram/WhatsApp | Ausente | Decisión |
| Whiteboards | Ausente | F5 (opcional) |
| IA (chat, autofill, semántica, media analysis, related) | Fuera de política | — |
| Multiusuario / E2EE servidor | Fuera de diseño | — |

Nota sobre la consulta original de weblinks: en Capacities, **Category/Topic/
Keywords son propiedades predefinidas del tipo weblink**, no personalizables,
pensadas para filtrar con consultas (sustituyen colecciones manuales). En
Capacities las rellena IA; en Lumen el equivalente sin IA es que sean
**selectores editables a mano** con opciones sugeridas por dominio.

## 4. Fases

### Fase 0 — Higiene (antes de todo)

1. **Arreglar la aceptación 0.6** (SIGSEGV offscreen tras
   `10-empty-typed-collection`, `VERIFICATION.md:21`). Todo lo demás se apoya
   en tests estables.
2. Cerrar los pendientes de `PENDIENTES.md` de la extensión (OG local, WebP,
   paridad `cleanWebUrl`) — son prerrequisito de la F3 de weblinks.

### Fase 1 — Núcleo de objetos (paridad esencial)

**F1.1 Colecciones** — Tamaño: M. Apps: server + desktop + contracts.
Lista manual y ordenada de objetos **de un mismo tipo**, con página propia
(consultas/etiquetas no lo cubren: es curación a mano). Implementación: objeto
de tipo `collection` con `properties.items` (object-relation, múltiple) y
`properties.type_id` fijo; en desktop, entrada en la vista del tipo (chips bajo
el título) y en la barra lateral del tipo; arrastrar desde list/gallery añade.
Contrato: nuevo subtipo de objeto + regla "un objeto puede estar en N
colecciones". Ref: docs.capacities.io/reference/collections.

**F1.2 Plantillas por tipo** — Tamaño: M. Apps: server + desktop + contracts.
Objeto marcado `is_template` del tipo (Capacities: plantilla pertenece a un
tipo); al crear un objeto del tipo, "Aplicar plantilla" copia propiedades +
bloques; variables `{date:...}` en título y en propiedades de fecha (usar el
parser de fechas de tareas ya existente); plantilla por defecto por tipo para
el diario. Contrato: flag en objeto. Ref: reference/templates.

**F1.3 Alias** — Tamaño: S. Apps: server (búsqueda) + desktop.
Propiedad por defecto `aliases` (lista de strings); búsqueda de alias en
palette y en linking search; el alias elegido se inserta como texto del link.
Ref: reference/properties#aliases.

**F1.4 Conversión de tipo y fusión** — Tamaño: M. Apps: server + desktop.
Convertir tipo con diálogo de mapeo de propiedades (qué propiedad va a dónde;
las incompatibles se conservan como texto); fusionar dos objetos del mismo tipo
(el conservado retiene propiedades, las vacías se rellenan, el contenido se
anexa, los links apuntan al conservado, el otro queda en tombstone).
Ref: reference/content-types#type-conversions, #merge-two-objects.

**F1.5 Papelera** — Tamaño: S-M. Apps: server + desktop.
Los tombstones ya existen; añadir `deleted_at`/preview retenido, lista de
papelera en desktop con buscar/restaurar/vaciar, y "no mostrar objetos en
papelera" en pickers (ya se filtran backlinks; auditar pickers de vistas).
Ref: reference/trash.

### Fase 2 — Datos, vistas y tiempo

**F2.1 Consultas: filtros relacionales y de contenido** — Tamaño: M.
Filtros nuevos en LumenQuery: `linked_objects_count`, `linked_objects_include`,
`linked_objects_type`, equivalentes de backlinks, `blocks_content_empty/
not_empty`, `in_collection`. Permite "notas huérfanas" y colas de mantenimiento
(como en Capacities). Ref: reference/queries.

**F2.2 Cálculos en vistas** — Tamaño: S-M. Apps: desktop.
En table (pie de columna) y list/gallery/wall/kanban (cabecera): suma, media,
mediana, min/max, rango (número); más antiguo/más reciente (fecha); conteo y %
marcado (checkbox); conteo de vacíos (resto). Por grupo cuando la vista agrupa.
Ref: reference/views#calculations.

**F2.3 Consultas variables embebidas** — Tamaño: M. Apps: desktop.
Consulta embebida en una página con filtro `this_object` (variable): "todas las
reuniones con esta persona" funciona en cada persona sin duplicar la consulta.
Requiere: referencias a consultas embebidas ya existentes → añadir resolución
de variable en el evaluador + en plantillas (con F1.2 da el patrón
Capacities). Ref: reference/queries#variable-queries.

**F2.4 Dashboards por tipo con secciones** — Tamaño: M. Apps: desktop.
Vista del tipo con pestañas configurables: "Todo" + secciones integradas
(recientes, sin tags, sin backlinks, sin colección, colecciones, consultas
guardadas del tipo). Persistir en `properties.view` del tipo o en un objeto
`query`. Ref: reference/content-types#object-dashboards, reference/sections.

**F2.5 Fechas con rango + hora** — Tamaño: S-M. Apps: contracts + server +
desktop + mobile.
Propiedad de fecha con hora opcional y fecha fin opcional; en el calendario un
rango aparece en todos los días que abarca; el parser natural de tareas ya
resuelve la entrada. Ref: reference/dates-and-daily-notes#date-ranges.

**F2.6 Tareas recurrentes** — Tamaño: M. Apps: server + desktop + mobile.
Regla de recurrencia (simple: cada N días/semanas/meses + día de semana) en la
tarea; al completar se genera la siguiente instancia con fecha nueva y registro
de ocurrencias (texto en el cuerpo de la tarea o propiedad de lista); las
consultas existentes las filtran por fecha sin cambios. Ref:
reference/task-management#recurring-tasks.

**F2.7 Find-in-page con reemplazo** — Tamaño: S. Apps: desktop.
`Ctrl+F` sobre el objeto abierto (incluye bloques, tablas y código) con
reemplazo de coincidencia actual y "reemplazar todo" dentro del objeto.
Ref: reference/search#find-in-page.

### Fase 3 — Editor y portabilidad

**F3.1 Referencias a bloques + transclusión** — Tamaño: L. Apps: contracts +
server + desktop.
Los bloques ya tienen IDs estables. Añadir: `((` búsqueda de bloques, copiar
referencia de bloque (menú de 6 puntos), referencia inline y embebida
(transclusión: editar el bloque embebido edita el original; el contador de
referencias en el bloque original lista los objetos que lo referencian).
Contrato: nuevo tipo de span en `rich_text` + conteo de referencias derivado.
Es el ítem de mayor valor de potencia del editor. Ref:
reference/block-based-linking.

**F3.2 Mermaid** — Tamaño: S-M. Apps: desktop.
En bloques de código con lenguaje `mermaid`, renderizar el diagrama (integrar
mermaid via webengine o un motor C++ ligero; decisión técnica en su momento) y
exportar PNG. Sin servicios externos: el render es local. Ref:
reference/mermaid-diagrams.

**F3.3 Weblinks: cuerpo, Reader y anotaciones** — Tamaño: L. Apps: server +
desktop + extension + contracts.
En tres pasos independientes:
1. **Notas en el cuerpo del weblink** (gap del audit 2026-10-29): el tipo `web`
   ya tiene content; asegurar que el editor abre notas debajo de la tarjeta de
   preview como cualquier media object (etiqueta "Notas", como en Capacities).
2. **Reader local**: al capturar, guardar el texto del artículo (lectura
   server-side ya existe en `preview.go`; añadir extracción de contenido tipo
   Reader y guardarlo como propiedad `reader_content` + búsqueda full-text).
   Sin IA: es scraping, no análisis.
3. **Anotaciones**: subrayados con color sobre el Reader, cada highlight crea
   un bloque de resaltado (highlight block) en las notas del weblink, anclado
   por posición/texto; eliminar el bloque elimina el highlight. Ref:
   reference/basic-types/weblinks, reference/highlight-blocks.

**F3.4 Importación Markdown → bloques** — Tamaño: M. Apps: desktop (+ server).
Convertidor Markdown → bloques (encabezados, listas, citas, tablas, código,
math, imágenes locales como assets, `[[wikilinks]]` → links por título con
resolución al importar el lote completo). Con F3.6 habilita migración desde
Obsidian/Notion exportado sin herramientas externas. Ref: reference/bulk-import.

**F3.5 Export completo del espacio** — Tamaño: M. Apps: server + tools.
Endpoint de export: ZIP con carpeta por tipo, un `.md` por objeto (front matter
con propiedades), assets con nombres legibles, links convertidos a links
locales entre archivos (grafo intacto), CSV por tipo para datos tabulares.
Herramienta `tools/scripts/export-space.sh` para backup personal. Es el ítem
de libertad de datos más importante para un PKM auto-hospedado. Ref:
reference/export.

**F3.6 Unlinked mentions** — Tamaño: S-M. Apps: desktop.
Sección bajo backlinks: apariciones del título (o alias, con F1.3) en texto sin
link, con botón para convertir en link. Reutiliza el índice de búsqueda del
servidor. Ref: reference/unlinked-mentions.

### Fase 4 — Plataforma y robustez

**F4.1 Historial de versiones** — Tamaño: M. Apps: server + desktop.
El servidor ya lleva `revision`; añadir snapshots de contenido en cambios
(tamaño acotado: conservar N por objeto) y UI de historial con vista previa y
restaurar (restaurar crea una nueva revisión, no borra). Complementa la
resolución de conflictos actual del sync móvil. Ref:
reference/version-history.

**F4.2 Caché de escritorio en disco** — Tamaño: L. Apps: desktop.
Persistir el índice + objetos en disco (cifrado, como mobile) para arranque
frío y trabajo sin servidor; cola de cambios pendientes con replay al
reconectar (el protocolo push idempotente ya existe para móvil: reutilizar
`op_id`/`base_revision` en el cliente desktop). Ref: misc/offline-support.

**F4.3 Grabadora de audio** — Tamaño: M. Apps: desktop (+ mobile).
Grabar desde el editor y el diario (Qt Multimedia), guardar como asset del
tipo `audio` con notas debajo; en mobile, botón de grabación en captura
rápida. La transcripción sería IA → fuera de política (ver §5). Ref:
reference/basic-types/audio.

**F4.4 Modo presentación** — Tamaño: S-M. Apps: desktop.
Overlay fullscreen que divide el objeto por encabezados H1-H4 (y divisores) en
diapositivas; título como portada; imagen final al costado; recuerda la última
diapositiva. Ref: reference/presentation-mode.

**F4.5 Grafo: red de tags y búsqueda** — Tamaño: S. Apps: desktop.
En la vista de grafo: mostrar tags como nodos (modo "red de tags"), búsqueda
que enfoca un objeto, timeline animado opcional. El grafo ya es navegable y
exporta PNG. Ref: reference/graph-view.

**F4.6 Favicon en capturas** — Tamaño: S. Ya anotado en docs/README: fetch de
favicon en `preview.go` y mostrarlo en tarjetas de weblink.

### Fase 5 — Opcionales (solo tras F1-F4)

**F5.1 Whiteboards** — Tamaño: XL. Lienzo infinito con shapes, frames,
flechas, objetos del espacio como tarjetas. En Qt es un proyecto propio
(QGraphicsView); alto costo, valor más bajo para un PKM personal que F1-F4.
Solo si hay demanda real. Ref: reference/whiteboards.

**F5.2 Personalización de tarjetas / layouts de página** — Tamaño: M.
Qué propiedades muestra la small card y en qué orden; layouts de página
(ficha/enciclopedia/perfil). Ref: reference/page-layouts,
reference/small-card-view-customization.

## 5. Decisiones de producto abiertas (no planificadas)

1. **Espacios**. Capacities permite múltiples workspaces; Lumen es
   mono-espacio con títulos globalmente únicos. Para uso personal, un único
   espacio + tags suele bastar. Si se quieren espacios: namespace `space_id`
   en objetos + selector + exclusión entre espacios en búsqueda. Costo alto en
   sync y contratos; no se recomienda ahora.
2. **Publicar/compartir**. Enlace público de solo lectura para un objeto
   (página/web) servido por el servidor con token de share. Factible y aislado;
   decidir si encaja en el modelo "personal, privado por defecto".
3. **API/MCP para agentes externos**. La API HTTP ya es el punto de extensión
   documentado. Referencia actual de Capacities (nueva API v1.0.0,
   developers.capacities.io, fuera de docs.capacities.io): tokens Bearer
   **ligados a un solo espacio** con scopes `api:read`/`api:write`, header
   `X-Capacities-Api-Version`, semántica last-write-wins, SDK TypeScript
   (`@capacities/api`) y 17 endpoints: `/space`, `/space/structures`,
   `/object`, `/object/url` (crear desde URL), `/object/media/upload`
   (+`part`/`complete`/`abort`, subida por trozos), `/object/markdown`
   (crear con Markdown), `/objects/search`, `/objects/structure`,
   `/objects/tag`, `/objects/collection`, `/blocks/append`,
   `/blocks/daily-note/append`, `/blocks/block`, `/block`. Su servidor MCP
   (`https://api.capacities.io/mcp`, OAuth 2.1 + PKCE) expone search, lectura,
   creación, append al diario y CRUD de bloques para Claude/ChatGPT/Cursor.
   Ideas trasladables a Lumen sin IA dentro de la app: scopes por token,
   `create-from-URL` (reutiliza `/v1/capture`), crear/anexar **vía Markdown**
   (reutiliza el conversor de F3.4), subida por trozos para assets grandes y
   un endpoint `/v1/journal/{fecha}/append`. Un servidor MCP es el puente
   natural hacia agentes externos sin romper la política de "sin IA": el
   agente vive fuera. Es una decisión de política, no técnica.
4. **Captura por email/Telegram**. Un gateway en el servidor que reciba
   texto/links/imágenes al diario. Factible (self-hosted), pero añade
   superficie de ataque; posponer hasta después de F3.
5. **IA**. Chat, auto-tagging, autofill, búsqueda semántica, análisis de
   media (transcripción, resúmenes de weblink, Category/Topic/Keywords
   automáticos) y Related Content dependen de IA → **fuera de política**
   (AGENTS.md). El plan diseña los equivalentes manuales (selectores para
   categoría/tema, consultas para related). Si la política cambiara, el ítem
   mínimo viable sería análisis de weblink (rellenar las propiedades F3.3).

## 6. Orden recomendado de ejecución

F0 → F1.1, F1.2 (colecciones y plantillas cambian el uso diario) → F3.5
(export: libertad de datos antes de crecer más el formato) → F2.1, F2.2
(consultas y cálculos rentan sobre lo existente) → F1.3, F1.4, F1.5 → F3.3
(weblinks, el flujo de captura es central en Lumen) → F2.4-F2.7 → F3.1
(referencias de bloques) → F3.4, F3.6 → F4 en orden numérico.

Cada ítem cierra con: tests (Go y editor-blocks), actualización de
`contracts/SYNC.md` + fixtures si toca formato, nota en `CHANGELOG.md` y
pasada de aceptación en `VERIFICATION.md`.

## 7. Referencias rápidas

- Docs completas: https://docs.capacities.io/llms-full.txt (índice: /llms.txt)
- Weblinks: https://docs.capacities.io/reference/basic-types/weblinks
- Consultas: https://docs.capacities.io/reference/queries
- Bloques y referencias: https://docs.capacities.io/reference/blocks,
  /reference/block-based-linking
- Sync/offline: https://docs.capacities.io/misc/offline-support
- API nueva (v1.0.0): https://developers.capacities.io (overview, concepts,
  referencia OpenAPI en https://api.capacities.io/openapi.json; la beta en
  docs.capacities.io/developer/api.md se descontinúa el 2026-09-01)
- Portal de producto para developers: https://capacities.io/developers
- MCP: https://docs.capacities.io/developer/model-context-protocol
  (servidor en https://api.capacities.io/mcp)
- Releases: https://capacities.io/api/releases.json
- Audit previo propio: docs/qa/2026-10-29-capacities-audit/
