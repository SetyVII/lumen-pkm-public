# Toggles editables y compartidos — 01-11-2026

## Comportamiento

Un toggle contiene un título y `children` editables. ▶ contrae y ▼ expande;
el marcador se guarda en `text` y `runs`, junto con el cuerpo completo, en
`properties.blocks`. Abrir/cerrar usa el guardado habitual del objeto, de modo
que el siguiente push/pull transmite el estado a los demás clientes. No es una
preferencia local ni necesita una migración del servidor.

Qt representa el título y el cuerpo en frames nativos, con sangría de 24 px y
guía vertical. `/toggle` y el menú de bloque crean el contenedor. Enter en el
título abre y entra al cuerpo; Enter en el último párrafo vacío permite salir.
El cierre conserva los hijos, los IDs, las imágenes y el formato; los toggles
anidados conservan su propio estado. El menú Eliminar sobre el título elimina
el contenedor y su cuerpo en una operación reversible. Mover y duplicar el
título incluyen el subárbol completo; duplicar asigna IDs nuevos. Buscar un texto oculto
abre sus toggles antecesores y guarda ese estado.

Android usa el mismo contrato. «Añadir bloque → Toggle» crea el contenedor;
el título y los bloques internos se editan con el editor nativo existente.
No se muestra el marcador como texto editable ni como placeholder del cuerpo.

## Evidencia

Datos ficticios de `contracts/fixtures/toggles.json`. Qt Widgets, Fusion,
render offscreen, oscuro, 760×340 px a DPR 1. Android Compose en emulador API 36,
oscuro, 1080×2400 px. La referencia son las capturas aportadas por el usuario;
no se consultó ni modificó su biblioteca de Capacities.

- Qt: creación, edición, cierre, reapertura, estado anidado, recarga JSON,
  deshacer/rehacer, teclado, borrado reversible y cuerpos con tablas/columnas.
- Android: pruebas JVM del estado, copia y espejo textual; instrumentación
  `ToggleRenderingTest` con edición del cuerpo, cierre, recarga y reapertura.
- Backend: `TestToggleBodyAndStateAcrossDesktopAPIAndMobileSync`, usando
  `makeApp(t)` con almacén y claves temporales. POST de escritorio → push móvil
  con edición/cierre → GET de escritorio → PATCH de apertura → pull móvil.

Las capturas usan componentes reales. La referencia tiene métricas diferentes;
se comparan la jerarquía del título, la flecha, la sangría y la guía del cuerpo,
sin declarar coincidencia exacta de píxeles.

## Capturas revisadas

- [Qt abierto](toggle-expanded.png) y [cerrado](toggle-collapsed.png).
- [Qt con tablas/columnas ocultas, tema claro](toggle-complex-collapsed.png).
- [Android abierto](android-expanded.png) y [cerrado](android-collapsed.png).

Montaje local referencia/Qt/Android en
`scratch/2026-11-01-toggles/comparison.png` (fuera del repositorio): referencia
sin reescalar, Qt a DPR 1 y Android reducido de densidad 420 a 1 px por dp.

## Alcance

No se ha instalado esta entrega en el teléfono personal ni reemplazado la
versión de escritorio en uso. Las pruebas API no equivalen a sincronización
comprobada entre esas dos instalaciones reales. Se mantienen las limitaciones
de conflictos concurrentes descritas en el contrato; abrir/cerrar modifica el
objeto y sigue esas mismas reglas. No se tocó `ops/` ni el servicio personal.

## Resultado de esta ejecución

`EDITOR_BLOCKS_TEST_OK`, `EDITOR_MEDIA_OK`, `CODE_MATH_TEST_OK`.
`go test ./...` correcto. Gradle: pruebas JVM y APK debug/optimized correctos;
`ToggleRenderingTest`: OK (1 test) en emulador. No se ejecutaron las pruebas
contra la biblioteca personal.
