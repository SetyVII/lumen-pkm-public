# Incidencias comunicadas por el usuario

Actualizado para Lumen 0.2, 29 de octubre de 2026. Las incidencias originales se conservan; esta actualización diferencia corrección implementada, evidencia técnica disponible y comprobaciones del usuario todavía pendientes. No se borran datos para ocultar una incidencia.

## Apertura de imágenes y PDF

1. **Visores demasiado grandes y superpuestos.** Corregido en el nuevo `MediaViewer`: ventana con tamaño limitado al área de la pantalla, cabecera y controles separados del contenido. La imagen conserva su proporción y ofrece ajuste a ventana/ancho/100 % y zoom. **Pruebas de componentes y aceptación autenticada PASS** con imagen vertical y PDF de tres páginas; capturas finales revisadas. La comodidad y las decoraciones exteriores requieren la valoración del usuario en su compositor.

2. **Cierre y controles poco claros.** Corregido: nombre del archivo, botón Cerrar y Escape; PDF con anterior/siguiente, página actual/total, ajuste y zoom. La posición de PDF se conserva en memoria al volver a abrirlo durante la sesión. **Prueba Qt/ASAN PASS** de navegación, cierre, error y reapertura. La aceptación conjunta de upload, lectura autenticada, apertura y cierre pasó; la página solicitada permanece después del primer frame.

3. **Borde azul muy prominente.** El visor tiene ahora estilos propios y controles de foco coordinados con la aplicación. **Origen del contorno original no demostrado.** Puede intervenir la decoración o el foco del sistema de ventanas; no se atribuye a Qt, Lumen o al compositor sin reproducción. Pendiente revisar el visor final en el mismo entorno Wayland del usuario.

4. **Contenido de pruebas visible.** El título «QA … editado», «Lumen PDF fixture» y los medios generados son datos ficticios de las comprobaciones anteriores. **Se conservan; no se borran ni se sustituyen automáticamente.** La imagen verde se utilizó como fixture, pero su captura original por sí sola no prueba si hubo además un fallo de visualización. Los nuevos tests generan imágenes con proporciones controladas y PDFs multipágina; las demostraciones visuales utilizan un servidor separado. El arranque normal no inserta ejemplos.

### Evidencia original

- [Visor de imagen aportado por el usuario](../../../research/capacities/audit/evidence/2026-10-29/visor-imagen.png).
- [Visor de PDF aportado por el usuario](../../../research/capacities/audit/evidence/2026-10-29/visor-pdf.png).

## Calendario y respuesta del diario

5. **El calendario no navega al día pulsado.** Implementada una navegación con fecha solicitada inmutable, generación de vista y descarte de respuestas antiguas. El minicalendario distingue selección, hoy y presencia de contenido; ofrece clic, foco, flechas y Enter. El diario parte de datos en memoria cuando están disponibles y conserva el guardado del origen antes de cambiar de fecha. **Pruebas de componentes de calendario PASS. Pendiente la aceptación integrada final de navegación con texto sin guardar, dos solicitudes rápidas y Enter/clic contra API real.** No se da por reproducida ni resuelta la experiencia del usuario sólo por leer el código.

6. **Lentitud al utilizar el diario.** El backend responde tras el commit cifrado y la outbox, y sincroniza con Logseq en un worker serial sin mantener bloqueadas las lecturas HTTP durante la ejecución del CLI. El cliente reutiliza objetos y medios en RAM, limita respuestas antiguas y muestra estado de carga/guardado/sincronización. **Pruebas Go de concurrencia/recuperación PASS**, incluidos CLI lento y nuevas escrituras. No hay una medición p50/p95 de la interfaz que permita afirmar una latencia o fluidez general; el recorrido integrado pasó y el ejecutable instalado arrancó contra la biblioteca conservada. Una valoración de fluidez con trabajo prolongado y hardware declarado sigue pendiente.

7. **Falta de indicaciones al pasar el cursor.** Implementados hover, foco, selección y tooltips en calendario, tarjetas, grafo, controles y referencias. Las tarjetas pintadas dibujan sus propios estados; las menciones comunican la apertura con Ctrl+clic al editar. **Pruebas de interacción por componentes PASS; pendiente recorrido visual final con ratón y teclado** a escala real.

8. **Faltan animaciones y transiciones.** Incorporadas transiciones por componente y apertura de visores; se evita animar toda la superficie mientras se escribe. La opción **Reducir animaciones** desactiva o simplifica el movimiento. La distribución por fuerzas del grafo también respeta esa preferencia. **Implementado y revisado visualmente.** La comodidad queda sujeta a la valoración del usuario. Las capturas estáticas y las pruebas offscreen no acreditan por sí solas fluidez ni accesibilidad de las animaciones.

## Referencia y verificación

La implementación parte del [análisis visual y funcional de Capacities](../design/DESIGN.md) y sus [criterios de referencia](../design/DESIGN.md). Además incorpora seis representaciones, consultas manuales guardadas, propiedades tipadas y medios reutilizables.

El estado completo de pruebas, instalación, capturas y distribución está en [VERIFICATION.md](VERIFICATION.md). Las evidencias finales están registradas en dicho documento. Ninguna comprobación de componentes equivale a aceptación del usuario.


## Cierre de implementación 0.2

El 29-10-2026 se completaron cinco pruebas de componentes y la aceptación nativa integrada sobre un servidor separado con Logseq real. Se verificaron calendario visible/Enter, guardado durante navegación rápida, menciones, consultas, preview/pestañas y medios autenticados. La revisión visual conjunta y sus límites constan en [design-qa.md](../design/design-qa.md). Los visores muestran nombre, salida por botón/Escape, zoom/ajuste y navegación de páginas; la restauración inicial respeta la primera navegación del usuario. No se ha demostrado que el borde azul exterior proviniese de Qt: las decoraciones del compositor quedan fuera del control del visor.

La instalación conservada fue actualizada sin eliminar fixtures ni datos existentes: treinta objetos y seis adjuntos idénticos, volumen y claves originales, grafo validado y cola pendiente vacía.

## Cambio de tipo de bloque en el editor (30-10-2026)

9. **Al convertir un bloque en tarea y después en viñeta o encabezado, la casilla o el triángulo seguían visibles; el texto existente tampoco adoptaba el tamaño ni la negrita del encabezado.** Corregido en `RichEditor::applyBlockStyle`: los prefijos `○ `, `✔ `, `▶ ` y `▼ ` se retiran al abandonar tarea/toggle, y el formato de carácter del nuevo estilo se aplica al texto ya existente, no solo al formato de inserción. Los encabezados conservan su seminegrita al guardar y cargar JSON. **`lumen-editor-blocks` PASS** con las secuencias párrafo→tarea→viñeta→encabezado→párrafo, tarea↔toggle, tarea marcada→lista numerada, conservación de ID y undo; `lumen-editor-media` y `lumen-code-math` PASS. La captura offscreen se revisó sin marcas residuales y con el encabezado visiblemente mayor y en seminegrita. Límite: la captura es offscreen; no sustituye la revisión en Wayland del usuario.

## Sangría, código y foco (30-10-2026)

10. **Al convertir una lista en párrafo o encabezado, el texto seguía desplazado a la derecha.** La sangría de `QTextListFormat` quedaba también en el `QTextBlockFormat` al salir de la lista. Corregido: al abandonar una lista se limpia la sangría del bloque; al convertir entre tipos de lista se conserva el nivel. **`lumen-editor-blocks` PASS** con Tab/Shift+Tab en lista y en bloque normal, y con la comprobación de que salir de la lista deja `indent() == 0`.

11. **Salir del bloque de código solo era posible con Enter en la última línea vacía o Backspace en un bloque vacío.** Añadidos `Ctrl+Enter` y `Alt+Enter` para salir sin borrar el código; Enter y Shift+Enter siguen insertando texto dentro del bloque. **`lumen-code-math` PASS** con ambos atajos.

12. **Keyboard First al crear:** el foco vuelve al editor al insertar una referencia creada desde `@` o al abrir un objeto recién creado desde `Ctrl+N`, en lugar de quedarse en el botón/diálogo. **Pendiente de prueba integrada con servidor**; los tests de componente del editor y el binario compilan y pasan.

## Descripción única y tipos personalizados (30-10-2026)

13. **En un enlace web aparecían dos filas de descripción: `Descripción` (`summary`) y `description`.** El servidor guardaba el texto del preview en `description` y el panel añadía `summary`. Corregido: `summary` es la descripción editable canónica; `og_description` queda como metadata interna; `description` se acepta como alias heredado al leer y se elimina al guardar. El servidor normaliza `PATCH`/sync y los clientes móviles leen `summary` con fallback a `description`/`og_description`. **`lumen-properties` PASS**, `go test ./...` PASS y tests unitarios móviles PASS.

14. **Crear un tipo propio con reglas distintas requería crearlo vacío y buscar después el editor de propiedades.** El diálogo de nuevo tipo permite copiar las propiedades de un tipo base y abre el editor al crearlo; el editor permite añadir, actualizar y quitar propiedades (texto, número, fecha, casilla, opciones y relaciones con tipo destino/múltiples). Los objetos siguen pudiendo conservar propiedades adicionales aunque no estén en el esquema. **Pendiente:** herencia/composición automática de varios tipos, propiedades obligatorias/por defecto y plantillas por tipo.
