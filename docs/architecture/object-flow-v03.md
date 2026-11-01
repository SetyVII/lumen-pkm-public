# Revisión del flujo de objetos para v0.3

29 de octubre de 2026. Revisión de sólo lectura de `desktop/src/main.cpp`,
`desktop/src/lumen_editor.cpp`, `desktop/src/lumen_views.cpp`,
`desktop/tests/acceptance.cpp` y la auditoría local de Capacities. No se ha escrito
en la API de producción ni se han abierto procesos GUI.

## Referencia visual recibida

- `/tmp/codex-clipboard-547bd5f6-e044-4b3a-8a9d-e594d41b0a5b.png`: `@`
  junto al cursor, objetos recientes con icono de tipo, tipos con flecha y pie de
  ayuda para flechas/Enter/Escape; contexto diario conservado.
- `/tmp/codex-clipboard-16648bf9-a271-4b6a-b6b2-3a493485c306.png`: `/`
  anclado al bloque, menú compacto con iconos y pie de teclado.
- `/tmp/codex-clipboard-2b4e613f-6f50-4d18-abc8-9d166f92d944.png`: colección
  con cabecera, `+ Nuevo`, `Vista general`/`Todo`, personalización y sección
  `Recientemente abierto`.
- `/tmp/codex-clipboard-638358c3-1a38-4be4-9f66-fd5d3480ea89.png`: `Todo`
  vacío mantiene contador y controles, explica la ausencia y ofrece `+ Nuevo` e
  importación. La captura demuestra apariencia, no el funcionamiento de importar.

## Cinco acciones que conviene completar

1. **Escribir `@`, elegir un tipo y crear o reutilizar un objeto sin salir de la
   frase.** Ya existen el popup, el callback de creación y los IDs persistentes.
   Faltan la jerarquía reciente/tipos de la captura y los iconos de tipo dibujados
   de forma coherente. Evitar convertir toda la fila a un texto con glifos de
   disponibilidad incierta. El popup debe mantener cursor y foco al cerrar.
2. **Abrir una mención como preview mediante clic normal y otra pestaña con
   Ctrl+clic.** Actualmente el editor editable exige Ctrl+clic, y `openPreview`
   interpreta ese Ctrl como pestaña. Actualizar la interacción y su tooltip como
   una sola decisión. Las tarjetas y backlinks ya llaman a preview.
3. **Entrar a un tipo y escoger `Vista general` o `Todo`.** La general debería
   enseñar objetos realmente abiertos, y `Todo` mantener la consulta común con
   sus seis presentaciones. Un estado vacío debe conservar su tipo y filtros y
   ofrecer una creación concreta. `updated_at` no equivale a abierto reciente.
4. **Crear desde un tipo y editar sus propiedades desde su cabecera.** El `+ Nuevo`
   debe heredar el tipo visible y el menú del tipo debe exponer el esquema ya
   implementado. Distinguir este origen del diario: `newObject` añade hoy
   `properties.date = day` a todas las creaciones, incluso fuera del diario.
   Solo asignar esa fecha cuando el contexto o la propiedad lo requiera.
5. **Leer de dónde viene una referencia y abrir su bloque.** El inspector actual
   muestra título y un tooltip con los primeros 240 caracteres del objeto entero.
   Ya existe `referenceBlock(source, target)`: usar el bloque que contiene la
   referencia para una frase visible y para enfocar ese bloque en preview. Una
   relación de propiedad sin bloque debe indicar su campo en lugar de inventar
   un fragmento.

## Integración y conservación de datos

- Mantener `canLeave` antes de reconstruir el editor por navegación o cambio de
  tipo. Una colección no debe recibir una copia del texto que se estaba editando.
- El callback de creación inline usa `QPointer<RichEditor>` y `origin`: si ya no
  existe su editor, añade la relación al origen con `add_links`. Conservar este
  patrón y no enviar contenido/propiedades viejos en esa ruta tardía.
- `createdObject` distingue el origen diario y llama a `insertReference` sin
  navegar. Una nueva cabecera de colección debe mantener esa diferencia.
- El preview ya espera el guardado duradero antes de cerrarse. El cierre de
  preview del mismo objeto actualiza la página de fondo; existe una regresión
  real para título y cuerpo. Conservarla.
- No usar `rebuildSidebar` o `renderEditor` para reflejar sólo el cambio de una
  presentación; los datos de vista deben actualizarse por IDs.
- Los estados visuales tienen reglas por `#nav`, `#quiet`, `#reference`,
  `#objectChip` y `#typeBadge`. Nuevos nombres de controles deben tener reglas
  equivalentes de hover/foco/pressed, y `objectName` estable para aceptación.
- Riesgo previo fuera del alcance visual: `pollWorkspace` actualiza sync y
  revision del objeto abierto sin recargar contenido/propiedades. El servidor
  no comprueba una revisión recibida al aplicar PATCH. Dos clientes editando el
  mismo objeto pueden perder cambios por último guardado; la revisión no debe
  presentarse como protección contra conflictos. No exige ampliar v0.3 a un
  sistema de merge.

## Pruebas nuevas propuestas para `desktop/tests/acceptance.cpp`

Todas se deben ejecutar con el guard existente `LUMEN_ACCEPTANCE_DISPOSABLE=1`
sobre loopback distinto de 8765, usando objetos ficticios y limpieza por ID.
No validar sólo textos de etiquetas ni constantes repetidas del código.

1. **Crear desde `@` con diario sucio:** escribir un marcador antes del cursor,
   abrir el selector real con una tecla, elegir el tipo Libro, introducir un
   título único y activar Crear. Verificar que el mismo editor/origen conserva
   cuerpo y ruta, que la mención devuelve un ID y que API guarda ese ID en links
   sin duplicados. Borrar la última mención y deshacer antes de guardar para
   comprobar que la relación vuelve.
2. **Apertura real de mención:** obtener `cursorRect` del texto anclado y hacer
   clic en el viewport. Verificar preview visible y ruta de fondo conservada;
   Escape debe devolver foco, cursor y contenido. Ctrl+clic debe crear pestaña
   con el ID correcto. La condición de modalidad/atajo debe ser la elegida en
   implementación, no llamar `openPreview` directamente.
3. **Creación tipada desde vacío:** acotar una colección por un tipo ficticio
   sin objetos; activar el CTA visible y crear un objeto. Comprobar type_id real,
   pertenencia a la colección y ausencia de enlace accidental al último diario.
   Si se corrige la fecha implícita, verificar que no hereda la fecha del diario.
4. **General/Todo y presentaciones:** abrir dos IDs de un mismo tipo, regresar
   a la general y comprobar que aparecen como recientes por apertura; `Todo`
   debe devolver el conjunto completo. Cambiar representación no cambia IDs ni
   modifica content/properties en la API.
5. **Backlink al bloque:** crear una fuente con dos bloques y una referencia en
   el segundo; abrir el backlink visible. Comprobar que el cursor del preview
   está en el ID del bloque de referencia. Conservar el primer bloque para que
   enfocar siempre el inicio no pueda producir un falso positivo.

Las pruebas 1 y 2 tienen mayor prioridad: cubren el flujo cotidiano y la ruta de
guardado que más cambia. La 3 protege la distinción entre creación en tipo y en
diario. General/Todo y backlinks pueden añadirse cuando root confirme sus APIs
y `objectName` definitivos.

## Avance de la verificación

- Con autorización de root se implementaron las pruebas 1, 2 y 3 en
  `desktop/tests/acceptance.cpp`. La creación inline se activa con las teclas
  reales en el editor; la apertura usa `QTest::mouseClick` sobre el ancla visible.
- Se compiló `lumen-acceptance` en un directorio de build temporal (`scratch/` o `.build/`), usando la imagen
  existente `lumen-qt-pdf-build:latest`. No se tocó `desktop/build-review`.
- Comprobados por ejecución los dos rechazos previos a cualquier petición API:
  URL de producción 8765 con flag de prueba y URL 8879 sin flag de prueba
  terminan con código 64.
- Lectura pública de `/health` en producción: `ready`, `pending=0`,
  `sync_running=false`, Logseq disponible y sin error de sincronización. No se
  ha escrito en producción; este estado no verifica la UI de v0.3.
- La ejecución completa sobre 8879 finalizó con `LUMEN_ACCEPTANCE_OK` en
  `${LUMEN_WORKSPACE}/scratch/2026-10-29-object-v03/lumen-object-v03/acceptance.log`. Incluye creación inline por tipo,
  eliminación/deshacer de referencia, guardado del diario de origen, clic en
  mención, cierre con Escape y devolución de foco, Ctrl+clic a pestaña,
  creación desde tipo realmente vacío, importación TXT a Proyecto y PNG como
  objeto Image independiente, bytes autenticados y las regresiones anteriores.
- El tipo vacío es un esquema ficticio de nombre único creado antes de iniciar
  la ventana. No se borra ningún tipo previo. Al no existir DELETE de tipos,
  ese esquema queda sólo en la API desechable; los objetos de prueba se limpian
  por sus IDs y los diarios recuperan su contenido previo.
- El cuerpo del libro inicial lleva un marcador de consulta único: permite
  comprobar guardar/reabrir una consulta tras renombrarlo sin incluir el libro
  adicional creado desde `@`.
- Cinco suites de componentes pasaron en `${LUMEN_WORKSPACE}/scratch/2026-10-29-object-v03/lumen-object-v03/final-build.log`:
  views, calendar, properties, editor-media y graph. La navegación del recorrido
  de aceptación final midió 143 ms al abrir y 118 ms con cambios de fecha rápidos;
  son mediciones de una ejecución local, no una comparación de rendimiento.
- Revisadas visualmente las capturas 08, 09 y 10 en
  `${LUMEN_WORKSPACE}/scratch/2026-10-29-object-v03/lumen-object-v03/acceptance-captures/`: icono inline visible, preview y
  estado vacío real con acciones. Tras corregir el desbordamiento de filas de
  navegación, se revisaron otra vez 08 y 10: iconos y principio de etiquetas
  quedan visibles y el nombre de tipo largo se abrevia manteniendo el contador.
- Añadida y pasada una regresión del tipo largo: comprueba que la fila ficticia
  existe con su tooltip completo, que el área de navegación tiene recorrido
  horizontal cero y que el contenido no supera la anchura del viewport.
