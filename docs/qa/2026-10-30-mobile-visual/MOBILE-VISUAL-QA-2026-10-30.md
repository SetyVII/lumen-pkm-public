# Lumen Mobile — revisión visual contra Capacities (30 de octubre de 2026)

Referencia: capturas Android de Capacities en `${LUMEN_WORKSPACE}/research/capacities/mobile/` y
`docs/qa/2026-10-29-capacities-audit/`, más la auditoría
`capacities_mobile_audit_and_lumen_redesign.md`. Implementación: Kotlin +
Jetpack Compose Material 3 en `mobile/`. Esta revisión no cambia el contrato de
datos ni la sincronización.

## Qué se corrigió

1. **Paleta.** Se sustituyó la rampa verde por el grafito medido en Capacities:
   fondo `#151518`, Home `#0D0D10`, superficie `#1A1A1D`, superficie alta
   `#232326`, selección `#3F3F41`, borde `#2A2A2E`, texto `#E4E3E2`, secundario
   `#A7A2A1` y acento `#FB2C36`. El tema claro conserva una rampa neutra cálida.
2. **Identidad de tipo única.** `ui/LumenTypeVisuals.kt` centraliza etiqueta,
   icono, contenedor oscuro y pictograma pastel para los 19 tipos. Los pares
   oscuros se midieron en Capacities: Cita `#5A2C3A/#FECDD3`, Definición
   `#453361/#DDD6FE`, Imagen `#573030/#FECACA`, Enlace web `#2F3957/#BFDBFE`,
   Libro `#543429/#FED7AA`, Tarea `#35467D/#EFF6FF`, etc. Home, el selector de
   creación, las menciones y las fichas usan ahora la misma fuente.
3. **Componentes compartidos.** `ui/LumenComponents.kt` añade barra superior de
   objeto, píldora segmentada, botón circular, insignia de tipo, chip de tipo,
   fila de propiedad, campo de editor sin borde, fila de objeto, estado vacío,
   línea de sincronización y grafo de resumen. Las cuatro fichas de objeto
   (Página, Libro, Enlace web, Imagen) dejaron de duplicar colores y estructura.
4. **Home.** Las filas de tipo dejaron de ser tarjetas: ahora son filas compactas
   de 44 dp con insignia de 28 dp, contador y chevron, agrupadas con 2 dp de
   separación (pitch ~46 dp frente a ~73 dp antes). Se añadió una fila
   «Crear objeto», una tarjeta de primer uso cuando el espacio está vacío y se
   conserva el carrusel de Fijados/Recientes. El fondo de Home es `#0D0D10` en
   oscuro.
5. **Dock.** Píldora flotante de 52 dp, fondo `#161619`, borde `#26262A`,
   inset horizontal de 24 dp. El primer slot es la píldora de espacio
   `#4D4746` con icono de casa, nombre y chevron; el botón `+` volvió a ser un
   icono plano, como en la referencia. Los iconos no seleccionados usan
   `#A7A2A1` y el seleccionado blanco.
6. **Diario.** Cabecera con día en azul `#50A2FF`, badge «Hoy» y selección en
   rojo `#FB2C36`, título de 28 sp con meses capitalizados, tira semanal sin
   tarjeta contenedora, editor sin borde y barra de accesorios sólo con el
   teclado visible. Las pestañas «Cronología» y «Creado» ahora muestran una
   línea de tiempo real de objetos actualizados/creados ese día.
7. **Crear (+).** Hoja `#151518`, tarjetas `#1A1A1D`, botones circulares
   `#232326`, tarjetas de 68 dp, cuadrícula de tipos de 84 dp y badges oscuros
   con pictogramas pastel. Se mantiene la creación instantánea sin diálogos
   intermedios.
8. **Fichas.** Barra superior compartida, pestañas con selección `#3F3F41`,
   propiedades como filas planas con divisores (sin tarjetas), editor sin borde,
   tarjeta OpenGraph con imagen 16:9 `ContentScale.Fit`, visor de imagen
   `#0D0D10` con píldora de zoom y acciones reales de fijar, copiar ID y
   eliminar con confirmación.
9. **Ajustes.** Se corrigió la ausencia de navegación de vuelta: ahora hay botón
   circular «Volver» y cabecera de superficie. Las tarjetas usan la superficie
   del tema.
10. **Barra de teclado.** Fondo fijo `#1D2024` con borde `#2E3238`, iconos
    claros y visibilidad ligada a `WindowInsets.ime`. Los diálogos de mención y
    barra `/` usan filas planas con insignias de tipo.
11. **Navegación atrás.** `BackHandler` cierra la ficha activa, el calendario o
    los ajustes antes de salir de la app. El tipo de colección abierto se guarda
    en el ViewModel: volver desde una ficha regresa a su colección y un segundo
    atrás vuelve a Home, sin expulsar al usuario de Lumen.

## Verificación realizada

- `./gradlew :app:compileDebugKotlin --offline`: **PASS**.
- `./gradlew :app:testDebugUnitTest --offline`: **PASS**.
- `./gradlew :app:assembleDebug --offline`: **PASS**; APK generado en
  `mobile/app/build/outputs/apk/debug/app-debug.apk`.
- Emulador `lumen-test` (1080 × 2400, densidad 420), tema oscuro, espacio de
  prueba creado con contraseña local. Se recorrieron y capturaron: Home, hoja
  Crear, cuadrícula de 18 tipos, ficha de Libro (Propiedades/Contenido),
  Diario (Nota diaria/Cronología/Creado), Tareas, Buscar, Ajustes, Calendario,
  ficha de Enlace web (Contenido/Incrustar) y visor de Imagen.
- Colores comprobados por muestreo de píxeles: Home `#0D0D10`, hoja `#151518`,
  tarjetas `#1A1A1D`, pestaña seleccionada `#3F3F41`, día `#50A2FF`, «Hoy» y
  día seleccionado `#FB2C36`, insignia de Libro `#543429/#FED7AA`, Enlace web
  `#2F3957/#BFDBFE`, Imagen `#573030/#FECACA`, Cita `#5A2C3A/#FECDD3`.
- Densidad comprobada: filas de tipo ~46 dp de pitch, filas de propiedad
  ~43 dp, dock 52 dp, tarjetas de creación 68 dp.
- Tema claro comprobado en el emulador: fondo `#F7F6F3`, superficie `#FFFFFF`,
  insignias con contenedores claros y pictogramas oscuros; el dock y el visor de
  imagen conservan su superficie oscura por diseño.
- Teléfono físico `ZY22M4JJDL` (1220 × 2712, densidad 450): se actualizó el APK
  y, tras desbloquear con huella, se recorrieron Home, Diario, hoja Crear,
  cuadrícula de tipos, colección «Páginas y notas», ficha de Nota, Tareas,
  Buscar, Ajustes y Calendario. No se crearon, editaron ni borraron objetos
  personales. Los colores medidos en el teléfono coinciden con el emulador:
  Home `#0D0D10`, Diario `#151518`, día `#50A2FF`, selección `#FB2C36`,
  insignias `#5A2C3A/#FECDD3`, `#453361/#DDD6FE`, etc.
- Montajes lado a lado de referencia y resultado en
  `${LUMEN_WORKSPACE}/scratch/2026-10-30-qa/lumen-mobile-visual-2026-10-30/` (Home, Crear, tipos, Diario, ficha y
  visor de imagen). Son evidencia local y no forman parte del paquete
  distribuible.

## Diferencias y límites

- La tipografía sigue siendo la sans del sistema (Roboto), no Inter. No se
  añadió una fuente remota ni un recurso sin licencia registrada.
- `LumenGraphView` es un resumen de conexiones, no un grafo interactivo con
  física y zoom.
- «Archivos» en la hoja Crear sigue siendo una acción futura; se muestra un
  aviso honesto en lugar de un selector simulado.
- El calendario es una cuadrícula mensual con miniaturas; no reproduce las
  tarjetas de notas del mes de Capacities.
- No se declara igualdad de píxeles ni paridad completa. No se auditaron
  lector de pantalla, alto contraste ni todos los tamaños de pantalla.
- Las capturas del emulador usan datos ficticios. No se modificaron notas
  personales del teléfono.

## Archivos principales

- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/theme/Theme.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/LumenTypeVisuals.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/LumenComponents.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/HomeScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/LumenDock.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/JournalScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/TasksScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/SearchScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/CreateModalBottomSheet.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/PageDetailScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/BookDetailScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/WebLinkDetailScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/ImageDetailScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/SettingsScreen.kt`
- `mobile/app/src/main/java/com/sety/lumen/mobile/ui/EditorKeyboardBar.kt`
