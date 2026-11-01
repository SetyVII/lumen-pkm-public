# Objetos, esquemas y editor Android — 01-11-2026

## Referencia observada

Se inspeccionaron por navegador los espacios Notas, Test y Pruebas de Capacities,
con el permiso del usuario. Son configuraciones diferentes; la inspección por
sí sola no establece qué versión del producto originó cada diferencia.

| Área | Observación | Aplicación en Lumen Android |
|---|---|---|
| Libro en Notas | Autor como contenido, Calificación, Recomendado por, Medio y Cover Image; dos vistas principales. | Compatibilidad con propiedades heredadas por nombre. |
| Libro en Pruebas | Autores, Valoración, Tipo, Género y Línea de lectura; varias vistas de lectura y mantenimiento. | Campos por esquema, selecciones propias del tipo, relaciones múltiples y fechas/rangos. No se copian sus vistas guardadas. |
| Enlace web | URL, resumen, miniatura, etiquetas, categoría y tema; opciones dependientes del espacio. | Propiedades del esquema recibido, filtros de selección/relación y estados de revisión compatibles. |
| Definición nueva | Título, etiquetas y cuerpo con bloques. | Crear tipos/objetos propios y usar el editor común. |
| Audio | Captura por subida o enlace. | Importación local cifrada y reproducción; no grabación ni descarga automática desde enlaces externos. |

Se creó únicamente «Lumen · prueba de compatibilidad de Definición» en Pruebas,
con texto explícitamente ficticio. No se editaron los objetos existentes ni se
importó una biblioteca de Capacities a Lumen. La captura privada está en
`scratch/2026-11-01-mobile-objects/`, fuera del repositorio.

## Implementación

- Home enumera los tipos registrados y sus objetos, incluidos personalizados.
  La creación permite título y tipo reales, no muestra distintivos Pro y no
  introduce objetos vacíos de tipos inexistentes. Se puede crear un nuevo tipo
  local con etiquetas; el sync registra ese ID antes de subir sus objetos.
- Crear desde una colección no añade contenido al diario. Las imágenes usan
  el selector de fotos y «Archivos» abre el selector del sistema para importar
  audio/PDF/archivos; el límite actual del transporte es 20 MB por archivo.
- Fichas muestran campos textuales, numéricos, booleanos, selectores y relaciones
  por ID, con cardinalidad y tipo de destino del esquema. Las fechas pueden
  conservar inicio/fin/hora. Las propiedades de un tipo no se globalizan.
- La colección realmente accesible desde Home ofrece búsqueda, estado y filtros
  por selecciones/relaciones. «Por revisar»/«Para revisar» y «Leído»/«Revisado»
  son compatibles al filtrar; al guardar se usa la opción admitida por el esquema.
- Las pestañas Gráfico ahora muestran objetos vinculados y enlaces entrantes
  reales y navegables. No son un grafo geométrico completo ni nodos decorativos.
  `property_links` se conserva; eliminar una mención retira su enlace explícito
  y mantiene las relaciones de propiedades.
- Cerrar delimitadores Markdown al escribir interpreta negrita/cursiva/código
  sin comerse espacios ni contagiar el formato al texto posterior. Los enlaces
  externos son pulsables. Código nativo conserva sus delimitadores literales.
- Texto antes/después de una imagen en el mismo bloque se muestra y se edita
  sin perder el run de imagen. El menú permite insertar imágenes existentes.
  Checklists usan casillas reales. Las tablas nativas leen el texto desde los
  runs de sus celdas y conservan el formato disponible.
- Añadir al diario genera deltas de bloques nativos. La API los concatena
  conservando el JSON previo, dimensiones e IDs, e invalida el HTML obsoleto.
  Dos imágenes distintas en el mismo minuto no se descartan por compartir U+FFFC;
  reintentar el mismo `op_id` sigue siendo idempotente.
- Audio usa Media3 con `ByteArrayDataSource`: los archivos quedan cifrados,
  se descifran en RAM y el reproductor se libera al cerrar la ficha. Se ofrecen
  reproducción, pausa e inicio y se muestra la duración.

## Verificación

- Gradle: **PASS**, 26 pruebas unitarias y ambos APK debug compilados.
- `ObjectWorkflowsTest` + `EditorRenderingTest`: **PASS**, `OK (8 tests)` en
  emulador Android 16/API 36, 1080×2400, tema claro, dos núcleos y 1536 MB de RAM.
  Se comprueban filtros/cambio persistido de estado, selección de género, autor
  como relación frente a texto heredado, rango de lectura, backlinks, dos deltas
  de imagen, audio WAV cifrado con reproducción/pausa, imágenes mixtas, tablas
  y checklists. Los datos sólo se escriben en un emulador sin emparejar.
- `MobileNavigationTest`: **PASS** en esta iteración antes de los últimos
  cambios del editor; los recorridos de volver al origen no se modificaron.
- `go test ./...`: **PASS**. Además, las pruebas nuevas de esquema compartido
  y append nativo: **PASS**, contra almacenes temporales.
- `git diff --check`: **PASS**.

Capturas de datos ficticios revisadas visualmente:
[estados](review-filter.png), [género](schema-filter.png),
[relaciones](typed-relations.png), [imagen y texto](mixed-image-text.png),
[audio](audio-local.png).

APK debug SHA-256:
`75b618ddaa82b517bde12a716ae7a240d0b46480f3ead02fddc77c718adf3c6a`.

## Continuación

Las vistas guardadas y la edición estructural se amplían en
[la siguiente verificación](../2026-11-01-mobile-views/README.md). Los resultados
que siguen describen esta iteración anterior.

## Alcance pendiente (en esta iteración)

No se afirma paridad completa con Capacities. Tablas/columnas/grupos no tienen
edición estructural completa; las tablas se representan y preservan. El móvil
no ejecuta todavía todas las consultas/vistas guardadas del escritorio ni tiene
un diseñador completo de esquemas. El audio se reproduce en la ficha; no hay
grabación ni servicio de reproducción en segundo plano.

Los cambios están compilados e instalados sólo en el emulador de pruebas.
El teléfono y el servidor personal no se han actualizado ni reiniciado.
El arreglo del append del servidor necesita despliegue para actuar en esa
instalación. El emulador se apaga al terminar.

Media3 ExoPlayer 1.11.1: [origen y versiones oficiales](https://developer.android.com/jetpack/androidx/releases/media3),
licencia Apache-2.0; CommonMark Java permanece en 0.27.1 (BSD-2-Clause).
