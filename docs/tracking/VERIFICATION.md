# Verificación de Lumen

> **Nota 0.6 (30-10-2026).** Las entradas de 0.3–0.5 describen la arquitectura
> con Logseq y se conservan como historial. La verificación de la retirada de
> Logseq, el índice en memoria, `Fsck`, CAS y los cambios móviles está en la
> sección 0.6 de este documento y en el `CHANGELOG.md`.

Actualizado el 30 de octubre de 2026. Este documento distingue código implementado, pruebas con resultado disponible y comprobaciones todavía pendientes. Se utilizan fixtures ficticios y volúmenes separados de la biblioteca conservada. Una captura o un test local no acredita despliegue remoto, auditoría de seguridad ni aceptación del usuario.

## 0.6 — Retirada de Logseq e índice en memoria

Comprobado el 30-10-2026 en este checkout:

- `cd server && go test -race ./...` y `go vet ./...`: **PASS**.
- `memIndex` guarda solo metadatos, tipos y backlinks; el contenido y los blobs se descifran bajo demanda. Los PDFs no se retienen en RAM entre peticiones.
- Benchmark local (i5-1135G7, `-benchtime=1x`): listar 5 000 objetos descifrando payloads ~38 ms; backlinks de metadatos con 5 000 fuentes ~12 ms. Son cifras de esta máquina, no una promesa de producción.
- `go run . -fsck` sobre un almacén temporal: **PASS**.
- Imagen Docker reconstruida sin Node: **PASS**. Contenedor aislado en `127.0.0.1:8879` con secretos y volumen temporales: `/health` devolvió `status=ready`, `source_of_truth=lumen-store`, `pending=0`; `-healthcheck` del binario devolvió 0; crear, editar con `base_revision`, consultar backlinks y `-fsck` funcionaron.
- `mobile/`: `./gradlew :app:compileDebugKotlin --offline` y `:app:testDebugUnitTest --offline`: **PASS**.
- `desktop/`: compilación en contenedor Qt y `ctest` de `views`, `calendar`, `properties`, `tasks`, `code-math`, `editor-media` y `graph`: **7/7 PASS**.
- Aceptación nativa contra el servidor aislado: alcanzó la creación inline y la colección vacía; el proceso Qt offscreen terminó con SIGSEGV después de `10-empty-typed-collection` sin imprimir `LUMEN_ACCEPTANCE_OK`. No se declara aceptación completa. El fallo es de la ruta de aceptación/entorno Qt, no de los tests de componente; queda pendiente localizarlo.

## 0.6.1 — Corrección del SIGSEGV de aceptación y paridad del menú de bloque (31-10-2026)

Comprobado el 31-10-2026 en el worktree `desktop/block-handle-parity`
(servidor desechable 8879 con claves y volumen propios):

- **Causa del SIGSEGV localizada con ASan**: heap-use-after-free en
  `LumenViews::visibleIds()`. `clearLayout()` quitaba las páginas
  reemplazadas con `deleteLater()` sin ocultarlas, así que la vista obsoleta
  seguía visible y localizable por `findChildren` hasta que el bucle de
  eventos ejecutaba el borrado diferido. Corregido ocultando el widget antes
  de `deleteLater()` (el patrón que `lumen_calendar.cpp` ya usaba).
- `lumen-acceptance` (build ASan) contra el servidor aislado: **PASS**,
  imprime `LUMEN_ACCEPTANCE_OK` y termina con salida 0.
- `lumen-views`, `lumen-calendar`, `lumen-tasks`, `lumen-properties`,
  `lumen-graph` (build ASan): **5/5 PASS**.
- `lumen-editor-blocks`, `lumen-editor-media`, `lumen-code-math` (build
  estándar, offscreen): **3/3 PASS**. El primero añade cobertura de
  agrupado multi-bloque (orden e ids de los hijos), desagrupado,
  `lumen://block/<id>` en portapapeles, pegado embed/inline, colores de
  bloque y mover/sangrar desde el menú del tirador.
- Detalles del alcance de UI y la lista de dependencias de backend:
  `docs/implementation/desktop/block-handle-parity.md`.

## Revisión visual móvil — 30-10-2026

- `mobile/`: `./gradlew :app:assembleDebug :app:testDebugUnitTest --offline`:
  **PASS**.
- Emulador `lumen-test` (1080 × 2400, densidad 420, tema oscuro y claro) y
  teléfono `ZY22M4JJDL` (1220 × 2712, densidad 450): recorridos de Home, Crear,
  tipos, Diario, Tareas, Buscar, Ajustes, Calendario, ficha de objeto y visor de
  imagen. No se modificaron notas personales.
- Colores medidos: Home `#0D0D10`, superficies `#1A1A1D`, selección `#3F3F41`,
  día `#50A2FF`, acento `#FB2C36`, insignias de tipo medidas en Capacities.
- Detalle, montajes y límites en
  [MOBILE-VISUAL-QA-2026-10-30.md](../qa/2026-10-30-mobile-visual/MOBILE-VISUAL-QA-2026-10-30.md).

## Documentación del emulador Android

Revisión del 30-10-2026: añadido `docs/ANDROID-EMULATOR.md` y enlaces desde
`docs/README.md` y `mobile/README.md`. Documenta el SIGSEGV del emulador
37.2.13 en Fedora 44 por la denegación SELinux `execheap` con SwiftShader, y
los modos comprobados: `-gpu lavapipe` arrancó en 22 s; `-gpu host` arrancó en
18,5 s. Comprobados 15 enlaces locales y 6 bloques `bash` con `bash -n`. No se
repitieron pruebas de la app ni del backend.

## Documentación de continuidad y entorno reproducible

Revisión del 29-10-2026: añadidos AGENTS.md y las guías de diseño, arquitectura, arranque y pruebas, enlazadas desde docs/README.md y README.md. Comprobados seis documentos,33 enlaces locales y17 bloques de shell mediante bash -n. Esto comprueba enlaces y sintaxis, no ejecuta cada comando de instalación o recuperación.

El nuevo server/compose.test.yaml pasó config --quiet y una prueba real: etiqueta de prueba del backend existente, secretos nuevos externos, proyecto lumen-pkm-test, puerto8879, volumen nuevo y grafo lumen-test. Llegó a ready con Logseq disponible; el cliente distribuido ejecutó --smoke con configuración aislada y terminó con salida0. Después de la escritura inicial, la cola volvió a0 sin error. El entorno temporal y su volumen recién creado se retiraron al finalizar; no se reutilizaron claves ni volumen personales.

No se cambió ni recompiló código C++/Go/JavaScript en esta tarea. No se repitió la aceptación completa ni se probaron los procedimientos de backup/restauración. La instalación personal continuó en8765; se comprobó su salud por separado.

## Verificación específica de 0.3

Cambios limitados al cliente C++/Qt y sus recursos; el servidor, volumen y claves de la instalación conservada no se migran en esta iteración. Las cifras de conservación de treinta objetos de la sección 0.2 son históricas; el usuario ha creado objetos después.

- **PASS — compilación conjunta** C++20/Qt 6.4.2. Cinco componentes, 5/5: vistas, calendario, propiedades, editor/medios y grafo. Las versiones ampliadas de vistas y editor también pasan con Qt 6.11.2 del host.
- **PASS — aceptación integrada 0.3**, Qt 6.11.2 del host sobre la API desechable 8879 y Logseq real. Se verifican foco/texto/cursor al cerrar preview, Ctrl+clic, tipo realmente vacío, importaciones y las regresiones anteriores. La primera prueba encontró pérdida de foco; se corrigió y el recorrido completo se repitió. El estado filtrado sin resultados se prueba separado del tipo vacío, sin borrar datos existentes.
- **PASS — revisión visual 0.3.** Montajes conjuntos de @ y / a 430×386 y de overview/vacío a ancho común609/573, con Inter registrada y DPR1. Se corrigieron ancho forzado de la barra, marco de portada y títulos. Diferencias de fuentes ópticas, iconografía, radios y favicons declaradas en `design-qa.md`.

- **PASS — binario y paquete 0.3.** Sustitución atómica del ejecutable, SHA256 registrado y archivo extraído probado con `--smoke` contra la instalación conservada (salida0). El paquete incluye fuentes, binario y licencias; excluye secretos, capturas privadas y directorios de compilación. Comprobación de valores de secretos sobre todos los archivos y comprobación del hash del binario empaquetado: PASS.
- **PASS — comprobación de lectura de la instalación conservada.** API ready, Logseq disponible, cola0, sin error de sincronización y31 objetos antes/después. No se cambia el servidor ni sus volúmenes en esta versión. El servidor de pruebas también llega a cola0 antes de apagarse.

El recorrido integrado usa exclusivamente la API desechable 8879 y un grafo Logseq separado. Incluye @ real y creación tipada sin abandonar la frase, guardar/deshacer, clic/Escape/Ctrl+clic, creación desde tipo vacío sin fecha residual, importación TXT en proyecto y PNG reutilizable con bytes autenticados, además de las regresiones de vistas/consultas/medios de 0.2. Las capturas y tests del host usan Qt offscreen; no equivalen a verificar todos los compositores de Linux.

## Evidencia histórica de 0.2

Los resultados siguientes se conservan como historial. No se han repetido pruebas del backend ni la migración en 0.3 porque esta revisión no cambia el servidor.

## Entorno

- Linux x86_64; host Fedora con Qt 6.11.2.
- Cliente C++20/Qt Widgets, Network, SVG, PDF y PdfWidgets. Compilación de componentes en Debian con Qt 6.4.2; sin WebView ni Electron en el cliente.
- Servidor Go, SQLite cifrado propio y runtime oficial Logseq 2.0.2/Node 24, sin iniciar Electron.
- Extensión MV3 en JavaScript; herramientas invocadas mediante pnpm.

## Implementación de esta versión

| Área | Código implementado |
|---|---|
| Diario y calendario | Destino de navegación capturado, descarte de respuestas antiguas, carga desde objetos en memoria y guardado previo del origen. Día, tres días, semana y mes; minicalendario con clic, teclado, foco, selección e indicadores. |
| Guardado | Commit cifrado y outbox atómicos antes de responder; worker serial para Logseq, sin mantener el bloqueo HTTP durante el CLI. Revisiones, recuperación tras reinicio, errores y reintentos observables. |
| Editor | Bloques con IDs estables y JSON, HTML compatible, selección flotante, enlaces contextuales, selector `@` con búsqueda/tipos/creación y navegación por teclado. Menciones y presentación como bloque; creación tardía conserva el origen. |
| Objetos y propiedades | Preview editable, apertura de página, backlinks a bloques, pestañas e historial. Esquemas de texto, número, fecha, booleano, selección y relación; edición del esquema del tipo. |
| Vistas y consultas | Lista, galería, tabla, muro, kanban e incrustación. Evaluador común por IDs; búsqueda, filtros Y/O anidados, propiedades, orden, agrupación y límite. Guardar/reabrir la definición como objeto `query`. Tabla editable y movimiento de kanban con deshacer. |
| Grafo | Distribución por fuerzas con repulsión espacial para colecciones mayores; todos los objetos, sin tope de 45. Tipos, filtros, relaciones explícitas/de propiedades deduplicadas, hover, foco, teclado, arrastre, zoom y exportación PNG explícita. |
| Medios | Imagen/PDF reutilizables además del adjunto, promoción explícita de archivos antiguos. Visor con nombre, cierre/Escape, ajuste/zoom y PDF multipágina con posición en memoria de sesión. |
| Sistema visual | Inter y Tabler locales; tema Capacities claro/oscuro y presets. Hover, foco y transiciones de componentes, con reducción de movimiento. Cachés de objetos y medios en RAM. |

La existencia de estas rutas en el código no sustituye su aceptación conjunta ni una comparación visual final al mismo viewport que la referencia.

## Pruebas con evidencia disponible

| Recorrido | Resultado y alcance |
|---|---|
| Backend unitario/concurrencia | `go test -race -count=3 ./...` y `go vet ./...`: **PASS**. Incluye HTTP mientras el CLI está bloqueado, escrituras durante sincronización, reinicio/outbox, borrado y tombstones, esquemas/relaciones, medios atómicos y protección frente a autosave anterior a un upload/promote. |
| Motor Logseq real aislado | Dos pruebas opt-in: **PASS**. Diario nativo y texto vacío, referencias, recuperación de outbox, PDF, consulta guardada, relación de propiedad, borrar/recrear diario y `graph validate` oficial. Se ejecutaron sobre grafos de pruebas, no sobre la instalación conservada. |
| Vistas, calendario y propiedades | Tres pruebas de componentes: **PASS** en Qt 6.4.2 del contenedor y Qt 6.11.2 del host. Cubren el evaluador común, edición/kanban, calendario y controles tipados. La tanda final conjunta posterior a los últimos ajustes visuales pasó 5/5. |
| Editor y medios | **PASS, exit 0**, Qt 6.4.2 con AddressSanitizer (`detect_leaks=0`). IDs estables, dividir/deshacer, JSON y bloques incrustados, foco por ID, eliminación de última mención y del título dejando sólo el icono, deshacer, selector/Enter/Escape, creación asíncrona, barra de selección y recursos privados. PDF de tres páginas, navegación, reapertura en la página previa, imagen vertical proporcional, zoom/ajuste, error y cierre. |
| Grafo | **PASS, exit 0**, Qt 6.4.2 con AddressSanitizer. Noventa objetos sin el antiguo límite; unión de relaciones, filtros, teclado/Enter, zoom/ajuste, exportación PNG, vacío y aceptación de 1800 objetos. El tiempo del fixture no se publica como medida p95 de la aplicación. |
| Extensión, verificación 0.2 | Nueve tests y harness contra API real: **PASS** en esta revisión, incluido el harness HTTP contra la imagen final en una API desechable. Incluye validación, worker, token de sesión, permisos/origen, transporte, 401, captura, lectura y asociación al diario. No equivale a instalarla en el perfil real del navegador. |

Los tests del editor y grafo generan sus propios medios/datos ficticios. Las advertencias del plugin Qt offscreen sobre `raise()` o captura de teclado no prueban el comportamiento del compositor Wayland. AddressSanitizer se ejecutó con detección de fugas desactivada; esta prueba no constituye una auditoría integral de memoria ni accesibilidad.

## Aceptación conjunta y publicación

- **PASS — compilación final conjunta mediante el CMake distribuido.** C++20/Qt 6.4.2, cinco componentes (vistas, calendario, propiedades, editor/medios y grafo): 5/5, sin warnings del compilador. Incluye regresiones de estilos tras renombrar componentes y de navegación de PDF antes del primer frame.
- **PASS — aceptación nativa extendida contra API desechable y Logseq real.** Qt 6.11.2 del host: navegación con texto pendiente y solicitudes rápidas, Enter visible del calendario, menciones/unlink, seis vistas sobre los mismos IDs, guardar/reabrir consulta, propiedades, preview del mismo objeto, cerrar una pestaña de fondo con texto pendiente, Kanban/deshacer y medios autenticados. La página 3/3 del PDF permanece tras esperar 140 ms. Los selectores del test buscan widgets visibles para no escribir en un editor retirado mediante deleteLater.
- **PASS — comprobación visual final y capturas sustituidas.** Diario, galería de cuatro columnas y tres días a 1448×650; comparaciones conjuntas a tamaño original con la referencia privada, y trece capturas del recorrido de aceptación. Se revisaron el visor en página 3/3, estilos de paneles, proporción de portadas y las diferencias declaradas en [design-qa.md](../design/design-qa.md).
- **PASS — instalación Docker conservada actualizada.** Imagen final `aae1fe1cf831…`, mismo volumen y claves; comparación fría antes/después: treinta objetos idénticos, seis adjuntos/blobs con SHA256 idéntico, siete tipos originales intactos y tres nuevos. API autenticada, `graph validate`, Docker healthy, Logseq disponible y `pending: 0` verificados después del reinicio. Backup y rollback privados conservados.
- **PASS — ejecutable y archivo distribuible.** Binario x86_64 instalado mediante reemplazo atómico, arranque contra la instalación conservada y ventana Wayland. Archivo fuente/binario inspeccionado: no incluye secretos, datos, backups, capturas privadas ni cachés de compilación. Las únicas capturas distribuidas usan datos ficticios.

La instalación conservada tiene un antecedente verificado de migración a Docker, igualdad de objetos/adjuntos, volumen persistente y `pending: 0`. También pasó la integración nativa de la versión anterior. Es evidencia histórica de conservación; no se presenta como aceptación automática de la versión 0.2.

## Repetir las pruebas

Componentes, sin API:

```bash
cmake -S desktop -B desktop/build-native -DCMAKE_BUILD_TYPE=Release -DLUMEN_BUILD_TESTS=ON
cmake --build desktop/build-native --parallel 2
ctest --test-dir desktop/build-native --output-on-failure
```

`lumen-acceptance` requiere `LUMEN_ACCEPTANCE_DISPOSABLE=1`, una API loopback desechable en un puerto distinto de 8765 y credenciales provisionadas para ese servidor. El propio test rechaza el puerto de la instalación conservada. Crea datos ficticios y restaura sus diarios de prueba; no debe apuntarse a una biblioteca personal. `lumen-integration` es un alias compatible del mismo recorrido de aceptación.

Backend y extensión:

```bash
cd server
go test -race ./...
go vet ./...
cd ../extension
pnpm test
pnpm build
```

Las pruebas con Logseq son opt-in, documentadas en [contracts/SYNC.md](../../contracts/SYNC.md). El modo `--seed-demo` escribe ejemplos en el diario: usarlo exclusivamente en un servidor de demostración. El arranque normal no añade ejemplos.

## Privacidad y límites prácticos

El cliente no escribe cachés de notas ni miniaturas en disco: conserva esos datos en RAM; QSettings contiene preferencias de interfaz y del selector de archivos, no el cuerpo de las notas. El HTML no resuelve recursos HTTP ni archivos locales. Los visores reciben bytes autenticados, no crean archivos temporales descifrados y rechazan PDF como imagen de una sola página mediante detección previa del formato. Las posiciones de lectura duran lo que dure el proceso.

**No hay E2EE.** El almacén y los blobs propios de Lumen usan AES-GCM; el grafo y los índices Logseq están en claro en el servidor. El host necesita su propio cifrado de volumen y política de backups. El servidor puede leer notas y medios. La caché en memoria tampoco garantiza ausencia de rastros del sistema operativo de un ordenador ajeno.

OpenGraph mantiene validaciones de red, tamaño y formato, sin cookies/token del usuario ni ejecución de JavaScript del sitio. No se garantiza extracción en Instagram, TikTok, sitios privados o proveedores que bloqueen la petición. El enlace se conserva si la preview falla.

Siguen sin validar o fuera del alcance:

- Instalación real de la extensión y permisos/CORS de su ID real en Chrome/Firefox.
- Servidor público, HTTPS remoto, VPN, cifrado físico y recuperación completa en otro host.
- p50/p95 de navegación, guardado y escritura con una carga/hardware declarados. No hay cifras de fluidez general publicadas.
- Todos los gestores de ventanas, escalados, dispositivos gráficos, lectores de pantalla y distribuciones Linux.
- App Android, widgets, offline, multicuenta, plugins aislados y resolución de conflictos entre editores simultáneos.
- Anotaciones PDF, importación universal de Logseq y equivalencia completa con consultas/funciones de Capacities.

`pending` confirma guardado durable de Lumen y sincronización aún pendiente. `synced` confirma la escritura de la revisión enviada al adaptador oficial. Consultar un HTTP 200 de health no basta: revisar disponibilidad del motor, cola y objeto. [Semántica de outbox, medios y propiedades](../../contracts/SYNC.md).


## Backend blocks, media, recurrence and habits — 2026-11-01

Local backend/API increment; implementation and limits:
[blocks-media-recurrence-habits](../implementation/backend/blocks-media-recurrence-habits.md).

- Full `go test -race -count=1 ./...`: passed (24.117 s in the final full-suite pass).
- After the final reopened-occurrence timestamp correction, the focused
  recurrence/concurrency regressions also passed with `-race -count=1` (2.585 s).
- `go vet ./...`, targeted `gofmt -l` and `git diff --check`: clean.
- `go build -o ../.build/server-backend-power/lumen-server .`: passed after the final code change.
- In-process HTTP journeys use temporary encrypted SQLite stores and synthetic
  fixtures. Coverage includes nested block lookup, live references, stale edits,
  prevention of mixed-body data loss, template subtree identity, table-cell
  search, delete/restore, habit sync/export/restart, outcome retry/concurrency,
  media classification across all upload paths and final-owner blob cleanup.
- No desktop/mobile UI acceptance or codec/player validation was performed.
  No personal service, volume or keys were read, restarted, deployed or migrated.
