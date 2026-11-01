# Guía de diseño de Lumen

Esta es una especificación de continuidad del producto, no un registro de lo que hizo una IA. Describe cómo decidir, implementar y revisar una interfaz nativa que siga el proyecto. La versión de referencia es 0.3; los valores siguientes se contrastan con el código y design-qa.md cuando se modifiquen.

## Experiencia principal

El centro es la escritura. La organización aparece al mencionar, crear y relacionar objetos; no exige clasificar antes de pensar. Un libro, persona, proyecto, idea, enlace o PDF tiene identidad propia, propiedades y relaciones. La misma entidad aparece en el diario, una tarjeta, una consulta o el grafo; no se duplica para cada vista.

Antes de diseñar una pantalla, responder:

1. ¿Qué intenta hacer la persona en este momento?
2. ¿Puede hacerlo desde el teclado y volver a su frase?
3. ¿Qué cambia realmente en los datos y cómo se refleja?
4. ¿Qué verá si está vacío, está guardando, falla o llega una respuesta tarde?

No añadir un dashboard, un paso de clasificación, una confirmación o una animación sólo para llenar espacio. Las acciones deben estar conectadas a operaciones reales, no a controles decorativos.

## Referencia y criterio

Capacities es la referencia explícita. Observar tanto el aspecto como la secuencia: dónde se escribe la consulta, cómo se selecciona un tipo, qué ocurre al crear, qué conserva Escape y dónde vuelve el foco.

Una captura no demuestra un comportamiento. No inventar el significado de un control desconocido; inspeccionarlo en una sesión autorizada o documentar la diferencia. No modificar notas personales para producir una captura de prueba. Separar los datos de la referencia del fixture nativo y declarar esa diferencia.

Reproducir jerarquía, escala, estados y densidad antes de pulir sombras. Una captura bonita de un mockup no sustituye la implementación Qt. No envolver una web para simular que es nativa.

## Superficies y jerarquía

- Barra superior compacta: navegación, pestañas, búsqueda e información contextual.
- Navegación lateral: crear, calendario, todos los objetos, tipos, consultas y ajustes. Nombres largos se eliden sin desplazar la columna; el tooltip conserva el nombre.
- Página central: título, fila de etiquetas (asignadas y mencionadas en el contenido), propiedades, editor y objetos relacionados. Mantener una columna de lectura cómoda; no estirar el texto a todo el monitor.
- Inspector contextual: calendario, adjuntos y backlinks. Se puede ocultar y no debe quitar el contexto de escritura.
- Colección: título y Nuevo; debajo, Vista general/Todo y controles compactos en una sola fila. No duplicar una barra de filtros completa encima de otra.
- Preview de objeto: editable, con Abrir página y Cerrar, y guardado al salir. No confundirlo con el visor de archivos.

La versión actual usa sidebar de 278 unidades lógicas, barra superior de 41, inspector de 312 y columna de lectura de aproximadamente 708 útiles. Son referencias de la implementación actual, no dimensiones rígidas que deban causar overflow a otro tamaño.

## Tipografía y color

Inter se distribuye localmente con su licencia. No depender de Google Fonts o de una descarga para abrir la aplicación. Texto base de controles: 13 px; editor predeterminado: 16 px configurable; título de documento: 31 px; título de colección: 18 px. Los paneles @ y / usan filas de 16 px de texto, con ayuda de teclado de 13 px.

| Token oscuro actual | Valor | Uso |
|---|---|---|
| Fondo | #151518 | Documento y superficie principal. |
| Lateral | #0d0d10 | Sidebar, barra y contexto. |
| Tarjeta | #1b1b1e | Contenido agrupado. |
| Borde | #29292e | Separación discreta. |
| Hover | #242428 | Respuesta al puntero. |
| Selección | #2d2d31 | Estado activo. |
| Texto | #e4e3e2 | Contenido principal. |
| Secundario | #919097 | Ayuda y metadatos. |
| Foco/acento | #d2b7f5 | Foco y acciones destacadas. |

La paleta vive en MainWindow::applyTheme y los componentes reciben el tema. Respetar los modos claro/oscuro, presets, tamaño de fuente y reducción de movimiento. No introducir colores oscuros fijos que hagan ilegible el tema claro.

Color de tipo y color de estado son cosas distintas. El tipo usa su color para un distintivo reconocible; selección/foco no debe teñir de azul ni borrar su icono.

## Iconos, imágenes y recursos

Usar lumen_icons.hpp y los SVG empaquetados, no emojis, caracteres de fuentes o iconos del tema del sistema como sustitutos. Tabler/MIT es la familia principal; package-open de Lucide/ISC se usa en el vacío. Conservar licencias, versión y origen de recursos.

El icono de tipo tiene fondo de color y pictograma legible, también dentro de menciones. No dibujar a mano un pictograma que ya exista en los recursos. QPainter sí se utiliza para superficies, tarjetas y transiciones nativas.

Las portadas se ajustan proporcionalmente dentro de un marco, sin estiramiento ni recorte inesperado. En galería y overview, el marco es cuadrado: ancho de tarjeta menos 28 px. En tarjetas de 248 px, el alto actual es 384; las galerías adaptan el alto al ancho real. Muro conserva altura variable y extractos.

Orden de una tarjeta: distintivo de tipo, medio, título de una línea con elipsis, dominio copiable y propiedades/chips pertinentes. El tooltip permite recuperar el título completo. No dejar el botón de copia en la posición anterior si cambia el alto del título o la portada.

Notas y miniaturas se mantienen en memoria. El editor no debe cargar recursos arbitrarios desde una URL o archivo local; los medios pasan por la API autenticada. No descargar imágenes de la biblioteca personal para incluirlas en la entrega.

### Portadas e imágenes destacadas en notas y diarios (A futuro)

- **Sin imposición automática:** Pegar una imagen en el cuerpo de una nota o diario nunca debe convertirla automáticamente en la portada del objeto ni inyectar un banner superior no solicitado.
- **Selección explícita del usuario:** Permitir al usuario elegir expresamente qué imagen actúa como portada/destacada (por ejemplo, mediante la opción «Establecer como portada» en el menú contextual de la imagen dentro de la nota o un control en la cabecera), con opción de cambiarla, reencuadrarla o retirarla voluntariamente.

## Contrato de interacción

### Escribir y mencionar

@ y su consulta se escriben dentro del editor. El menú no abre un buscador separado ni roba foco. Mostrar recientes y tipos con iconos, selección clara, scrollbar y ayuda de teclado.

Flechas arriba/abajo navegan, Enter selecciona y Escape cancela según el comportamiento probado del editor. Entrar en un tipo permite encontrar o crear allí. Fecha muestra un calendario y enlaza el diario elegido. Crear inline inserta una referencia por ID y permite continuar en el mismo documento.

El panel actual mide 430×386 con filas de aproximadamente 41 px. Debe anclarse al cursor y elegir arriba/abajo según el espacio disponible. Comprobar el panel cerca del borde inferior, no sólo al principio de una nota.

### Dar formato

/ abre Crear un bloque junto al cursor y filtra mientras se escribe en la nota. Mantener estándar, pequeño, H1–H4, viñetas, alfabética y los otros formatos existentes. Cambiar estilo conserva texto, cursor, ID del bloque y undo.

Las marcas internas de referencia y sus iconos no se convierten en sintaxis #visible ni en caracteres residuales cuando se borra la última mención.

### Abrir y volver

Un clic en una mención abre preview; Ctrl+clic abre pestaña. Escape y Cerrar vuelven al contexto anterior con texto, posición y foco intactos. Abrir página o navegar a un tipo debe guardar antes de cambiar contexto.

Una respuesta tardía no debe abrir una ficha sobre otra navegación ni devolver foco a un editor retirado. El cierre de un preview o pestaña de fondo no puede sobrescribir el documento activo.

### Colecciones y vacíos

Vista general muestra recientes reales de la sesión y recién creados. Todo conserva el mismo ámbito y evaluador de consulta, con lista, galería, tabla, muro, Kanban e incrustación. Cambiar representación no cambia los IDs del resultado.

Un tipo sin objetos muestra ilustración, texto humano y Nuevo/Importar. Una búsqueda sin coincidencias muestra Limpiar filtros. No anunciar que no existen objetos cuando sólo están filtrados.

Nuevo e Importar respetan el tipo de origen. Crear desde una colección no añade la fecha del último diario ni lo modifica. Importar Markdown conserva texto; no anunciar conversión enriquecida mientras no exista.

## Respuesta, teclado y accesibilidad

Hover, foco, selección, pulsación y deshabilitado deben ser distinguibles. El foco debe verse en la acción concreta, incluso cuando una regla CSS por ID sobrescriba estilos generales.

Mantener nombres accesibles, etiquetas y tooltips en controles sólo con icono. Permitir Escape, Enter y Tab; comprobar el orden de foco. No transmitir un estado exclusivamente con color. Verificar texto largo, aumento de fuente, tema claro y errores.

La animación comunica hover, selección o apertura; no desvanece el documento entero al escribir. Respetar Reducir animaciones. No atribuir un borde del compositor a la aplicación sin reproducirlo.

## Cómo debe trabajar una IA en UI

1. Leer el código del componente y esta guía. Localizar qué ya existe.
2. Definir estados y recorrido de aceptación con una frase concreta.
3. Observar referencia autorizada; registrar medidas, tema, viewport y comportamiento.
4. Implementar en el módulo existente y conectar acciones a datos reales.
5. Ejecutar pruebas pertinentes en el entorno aislado de RUNBOOK.md.
6. Capturar la UI real. Comparar fuente y resultado en una misma imagen a escala declarada, con regiones equivalentes y sin estirar.
7. Corregir defectos de flujo, overflow, tipografía o proporción; repetir las capturas afectadas.
8. Actualizar design-qa.md con evidencia, diferencias y alcance. No marcar passed si un recorrido requerido sigue fallando.

La revisión mínima incluye diario vacío/con contenido, @ vacío/búsqueda/tipo/creación, /, preview/cierre/foco, colección vacía/filtrada, tarjetas con portadas de proporciones distintas y título largo. Elegir los estados afectados por el cambio; no repetir todo sin motivo.

Offscreen valida componentes y permite capturas reproducibles. Una revisión del comportamiento de ventanas, activación y decoraciones requiere además el compositor real. No presentar una prueba como la otra.
