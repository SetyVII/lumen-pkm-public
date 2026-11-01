# Vistas guardadas y edición estructural — 01-11-2026

Continuación de [objetos y esquemas móviles](../2026-11-01-mobile-objects/README.md).
Código y pruebas con datos ficticios; sin despliegue en la instalación personal.

## Comportamiento implementado

- Colecciones: guardar una vista, reabrirla, actualizarla, renombrarla y borrarla
  sin borrar sus objetos. Persisten búsqueda, filtros, orden y presentación en
  `query.properties.view`, el formato existente de escritorio. La outbox normal
  la sincroniza; se puede trabajar sin conexión.
- El mismo LumenQuery por plataforma evalúa AND/OR anidados, aliases,
  links/property_links/backlinks, comparaciones, orden manual, ordenación
  múltiple y límite. Hay 12 casos comunes en `contracts/fixtures/saved-views.json`.
  Los valores JSON de propiedades estructuradas se comparan con claves ordenadas.
- Los filtros simples guardados vuelven a los controles editables. Pasar una
  vista de «Para revisar» a «Revisado» sustituye esa condición; los filtros
  complejos se conservan y se describen debajo de los controles. Un cambio de
  propiedad actualiza inmediatamente los resultados de la vista.
- «Añadir bloque»: párrafo, tabla o columnas. Tablas con edición de celdas,
  filas/columnas nuevas, eliminación con confirmación y alineación de columna.
  Columnas de diseño apiladas en móvil, con edición de bloques internos,
  creación, movimiento y eliminación de columnas. Se puede quitar el bloque
  estructural completo con confirmación.
- Las mutaciones estructurales conservan IDs, formato de runs, claves ajenas,
  imágenes y referencias anidadas. Invalidan sólo el HTML obsoleto. El texto
  de las celdas/columnas participa en búsqueda y las relaciones se recalculan
  recursivamente, evitando repetir imágenes incrustadas al pie del documento.
- Qt lee y serializa QTextTable/frames, evitando aplanar celdas y columnas.
  Conserva también grupos con estructuras anidadas y metadatos de runs. La
  búsqueda/reemplazo masiva en celdas sigue excluida hasta validar sus cursores
  y undo; la edición directa de la tabla sí se guarda.

## Verificación

- Android: **32 tests unitarios, 0 fallos**. Incluyen consulta compartida,
  extracción de un filtro guardado editable y estructuras comunes Qt/Android.
- Emulador aislado `emulator-5556`, sin emparejar: **11 tests de interfaz, 0
  fallos**, clases `ObjectWorkflowsTest` y `EditorRenderingTest`. Crear,
  actualizar, renombrar, eliminar y reabrir vistas; cambio de estado dinámico;
  editar/reabrir celdas y columnas, añadir/quitar estructuras y conservar
  outbox, vecinos, imágenes, checklist, relaciones y reproducción de audio.
- Qt: `lumen-views`, `lumen-editor-blocks`, `lumen-editor-media` y
  `lumen-code-math` aprobados con plataforma offscreen. La fixture
  `native-structures.json` se consume en ambos clientes. Las pruebas Qt
  verifican tabla editable real, reapertura, IDs, referencias, estilos,
  dimensiones de imagen y grupos anidados.
- Sobre el APK final se repitió además el recorrido completo de vista guardada
  (crear, reabrir, cambiar filtro, actualizar, cambiar propiedad, renombrar y
  eliminar): **1 test aprobado**.
- Backend: `go test ./...` aprobado.
- `git diff --check`: sin errores.

Se detectó y corrigió durante QA un fallo de medida: encapsular BasicTextField
sin trasladar su Modifier al contenedor hacía invisibles los textos en celdas.
La suite completa se repitió después de corregirlo.

Builds y logs completos en
`${LUMEN_WORKSPACE}/scratch/2026-11-01-mobile-views/`.
Android Gradle: `:app:testDebugUnitTest :app:assembleDebug
:app:assembleDebugAndroidTest --max-workers=2`.
Qt: imagen `lumen-qt-make:latest`, `.build/desktop-test`, `--parallel 2`.

APK debug 0.6.1, SHA-256:
`5e5a75a9758cd4c583cb030f4e9b2dafd176c7855d5ad13003d42c5da3c21169`.

## Evidencia visual

Datos ficticios, Android en tema claro, 1080×2400 px, densidad 420 dpi.
Las columnas se apilan para lectura y edición en el teléfono. El estado vacío
capturado en la vista guardada es el resultado de marcar el último enlace
pendiente como revisado; no se ha borrado el enlace.

- [Vista guardada y filtro activo](saved-view.png).
- [Tabla editada y reabierta](table-editing.png).
- [Columna editada y reabierta](column-editing.png).
- [Componente Qt con tabla, referencia e imagen en columnas](editor-native-structures.png):
  captura offscreen, estilo Fusion, 760×600; no verifica el compositor del host.

La referencia de interacción es la investigación previa de las vistas de libros
entre los espacios Notas y Pruebas de Capacities, documentada en la QA anterior.
Estas capturas son de Lumen con fixtures; no son capturas de Capacities.

## Límites y estado de instalación

Android presenta lista/vista general y agrupación; conserva los renderizadores
específicos del escritorio (kanban, calendario, muro...) en el spec sin ofrecer
sus seis interfaces. No se añade un diseñador general de consultas o esquemas,
ni controles de celdas combinadas. El formato Markdown de exportación no cambia.

Sólo se ha instalado en el emulador. ADB detectó el Motorola Edge 60 Fusion por
Wi-Fi; no se ha instalado, abierto ni escrito en el teléfono durante esta
iteración. El servidor personal no se ha reiniciado, migrado ni actualizado.
El arreglo de append de diario de la iteración anterior sigue necesitando un
despliegue separado en esa instalación. El emulador se apaga al acabar.
