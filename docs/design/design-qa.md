# Lumen 0.3 — revisión de objetos y editor

Referencia: cuatro capturas aportadas por el usuario y navegación de lectura en su Capacities abierto. Implementación C++20/Qt Widgets; Inter empaquetada, iconos oficiales Tabler/MIT y package-open Lucide/ISC. Esta iteración trabaja sobre Lumen existente.

## Comparación y estados

Se compararon fuente y resultado en montajes conjuntos, sin estirar las imágenes. Los paneles @ y / se recortan a 430×386 píxeles y se muestran 1:1. Vista general: referencia 609×663, componente Qt del mismo ancho con Inter registrada; vacío: referencia 573×749, componente Qt del mismo ancho. Se retira la cabecera sólo para comparar la región común que empieza en las pestañas. La aceptación integrada captura también la ventana completa a 1448×650.

| Superficie | Verificación |
|---|---|
| @ vacío y búsqueda | Panel anclado, resultados con distintivos de tipo, selección y ayuda de teclado. La consulta permanece en la nota, que conserva foco. Tipos con submenú, búsqueda/creación y Fecha con calendario. |
| / | Crear un bloque, estándar, pequeño, H1–H4, viñetas y alfabética en el orden de la captura, más los demás formatos. Iconos SVG, selección redondeada, scroll y footer. IDs, texto, cursor, undo y JSON conservados. |
| Vista general y tarjetas | Vista general/Todo en una fila, recientes reales de la sesión y recién creados. Marco cuadrado de medios en galería/overview, imagen proporcional centrada, tipo arriba, título de una línea con elipsis, dominio/copia y chips de selección. Muro conserva sus extractos y altura variable. |
| Tipo vacío | Ilustración oficial de caja abierta, texto y acciones Nuevo/Importar. Una búsqueda sin coincidencias muestra otro estado con Limpiar filtros. La cabecera mantiene Nuevo. |
| Uso de objetos | Crear inline mantiene el diario y enlaza el ID; clic abre preview, Escape recupera la frase y el foco, Ctrl+clic abre pestaña. Los distintivos de tipo navegan a la colección. Crear/importar en una colección no arrastra fecha ni modifica el diario anterior. |

Los montajes y la referencia privada se conservan en `../../../scratch/2026-10-29-object-v03/lumen-object-v03/`, fuera del paquete. Las capturas publicadas usan fixtures ficticios o medios públicos.

## Defectos encontrados y corregidos

1. @ abría un formulario separado. Se mantiene ahora la escritura y la consulta en el bloque original; los submenús se operan desde el editor.
2. Qt teñía de azul los iconos seleccionados. Los SVG mantienen el color del tipo en sus estados de icono.
3. La barra de Todo forzaba más ancho que una vista de 573 px. El buscador ahora puede reducirse; se verifican tamaños solicitados y reales iguales, DPR 1.
4. Las tarjetas usaban títulos de dos líneas y un marco de altura fija. La galería y el overview usan ahora un marco cuadrado y una línea con elipsis; se reubican el dominio y su botón de copia juntos.
5. Cerrar una mención conservaba la nota/cursor pero perdía foco. Se restaura el widget de origen después de destruir el preview, con guardas para otra navegación o modal. Las respuestas tardías de preview de una navegación anterior se descartan.
6. Los nombres largos de tipo podían forzar ancho y desplazar la barra lateral. Se acorta su texto visible con elipsis, se conserva el nombre en el tooltip y las filas pueden reducirse dentro de la columna.
7. La creación desde colecciones añadía la fecha del diario. La fecha se incluye sólo cuando el origen es el diario.

## Límites observables

Se conserva la iconografía oficial elegida para Lumen. El selector @ usa el icono de tipo web; no reproduce el favicon de GDevelop de la referencia. Las tarjetas admiten `favicon_asset_id`, pero el capturador actual no obtiene automáticamente favicons. El orden de tipos depende del espacio. Radios, sombras, métricas tipográficas y algunos controles secundarios difieren; no se copian controles de comportamiento desconocido. No se declara igualdad de píxeles ni equivalencia completa con Capacities.

La revisión visual verifica componentes reales y los recorridos descritos; no es una auditoría completa de accesibilidad, rendimiento ni todos los compositores de Linux. Los tests del host usan Qt offscreen. El servidor, las claves y el volumen conservado no se modifican en 0.3.

## Resultado

**final result: passed**

Compilación conjunta C++20/Qt 6.4.2 sin warnings del compilador y componentes 5/5. Vistas/editor ampliados también pasan con Qt 6.11.2 del host. Aceptación completa contra la API desechable y Logseq real: `LUMEN_ACCEPTANCE_OK`. Incluye las regresiones de foco y los recorridos de objetos/importación descritos, además de propiedades, consultas, Kanban, guardado y medios. La aceptación inicial encontró defectos, se corrigieron y se repitió; las diferencias observables de fidelidad siguen declaradas arriba.

## Toggles — 01-11-2026

Cuerpo editable, flechas de apertura/cierre, guía lateral y estado compartido entre Qt y Android. Pruebas, capturas y alcance en [QA de toggles](../qa/2026-11-01-toggles/README.md).
