# Flujo de trabajo de Qt 6 Desktop: compilación, dependencias, tests y corrección de UI

Esta guía documenta cómo está configurado el entorno de escritorio C++20 / Qt 6 en esta máquina y cuál es el procedimiento exacto para compilar, inspeccionar dependencias, ejecutar pruebas unitarias/visuales y corregir el editor o la interfaz sin perder tiempo ni violar la estructura del workspace.

---

## 1. Entorno de la máquina: Host vs. Contenedor (`lumen-qt-make:latest`)

> [!IMPORTANT]
> **No busques `cmake`, `ninja`, `qmake` ni paquetes `-devel` de Qt 6 en el host** con `rpm -qa`, `dnf`, `find /usr` o `which cmake`. El host no tiene instalado el toolchain de compilación C++/Qt.

La máquina separa la **ejecución gráfica** de la **compilación**:

| Capa | Dónde vive | Qué contiene | Para qué se usa |
|---|---|---|---|
| **Ejecución nativa (Host)** | `/lib64/libQt6*.so.6` (Fedora en el host) | Bibliotecas dinámicas de ejecución (`Qt6Widgets`, `Qt6Gui`, `Qt6Core`, `Qt6Network`, `Qt6Svg`, `Qt6Pdf`, `Qt6PdfWidgets`) y sesión Wayland/X11 | Abrir la app real en pantalla con `tools/scripts/run-desktop.sh` |
| **Compilación y tests offscreen (Docker)** | Imagen local `lumen-qt-make:latest` | `g++` (C++20), `cmake`, `ninja`, cabeceras `qt6-*-dev` y plugin `QT_QPA_PLATFORM=offscreen` | Compilar `.build/desktop-*` y pasar los tests (`ctest` / binarios de prueba) |

### Cómo comprobar las dependencias del binario en el host (1 solo comando)

El binario generado dentro de `lumen-qt-make:latest` enlaza dinámicamente contra las bibliotecas `/lib64/libQt6*.so.6` del host. Para verificar que no falta ninguna biblioteca ni símbolo en el host:

```bash
ldd -r ${LUMEN_WORKSPACE}/lumen-pkm/.build/desktop-release/lumen | grep -E 'not found|undefined' || echo "OK: dependencias resueltas"
```

---

## 2. Cómo compilar y pasar tests rápidamente

Todos los builds de escritorio viven en `${LUMEN_WORKSPACE}/lumen-pkm/.build/desktop-<preset>/`.
- **PROHIBIDO** crear carpetas `desktop/build-*` dentro de `lumen-pkm/desktop/`.
- **PROHIBIDO** trabajar en `${LUMEN_LEGACY_WORKSPACE}/`.
- **Usa siempre `--parallel 2`**: compilar `lumen_editor.cpp` y `main_window_ui.cpp` con `-O2 -g` y `--parallel $(nproc)` agota la memoria del contenedor y termina en `c++: fatal error: Killed signal terminated program cc1plus`.

### 2.1 Compilar y ejecutar únicamente el test del módulo que estás tocando (vía rápida)

No recompiles los 10 ejecutables de prueba cuando solo estás tocando el editor o una vista. Compila únicamente el target afectado dentro de `lumen-qt-make:latest`:

```bash
docker run --rm -u "$(id -u):$(id -g)" \
  -v "${LUMEN_WORKSPACE}:/work" -w /work/lumen-pkm \
  lumen-qt-make:latest bash -lc '
    cmake --preset test -S desktop &&
    cmake --build .build/desktop-test --parallel 2 \
      --target lumen-editor-blocks lumen-editor-media lumen-code-math &&
    QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-editor-blocks &&
    QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-editor-media &&
    QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-code-math
  '
```

### Mapa de targets de test en `desktop/CMakeLists.txt`

| Target CMake | Archivo de test | Qué verifica (sin necesitar servidor) |
|---|---|---|
| `lumen-editor-blocks` | `desktop/tests/editor_blocks_test.cpp` | Alineación (`x = 32`), tareas (`todo`), `Enter`/`Backspace`/`Tab`, divisor `---`, centrado del tirador `⋮⋮` y arrastre de bloques (`performBlockMove`) |
| `lumen-editor-media` | `desktop/tests/editor_media_test.cpp` | IDs estables de bloque, `undo/redo`, roundtrip JSON, menciones `@`, menú `/`, visor PDF e imágenes |
| `lumen-code-math` | `desktop/tests/code_math.cpp` | Bloques de código, resaltado sintáctico y fórmulas matemáticas LaTeX |
| `lumen-views` | `desktop/tests/views.cpp` | Colecciones, tablas, galería, muro y filtros |
| `lumen-calendar` | `desktop/tests/calendar.cpp` | Calendario y navegación de fechas |
| `lumen-properties` | `desktop/tests/properties.cpp` | Panel de propiedades tipadas y relaciones |
| `lumen-tasks` | `desktop/tests/tasks.cpp` | Gestión y filtrado de tareas |
| `lumen-graph` | `desktop/tests/graph.cpp` | Vista de grafo de objetos |
| `lumen-acceptance` | `desktop/tests/acceptance.cpp` | **Requiere servidor desechable en `:8879`** (`sandbox/compose.test.yaml`) |

### 2.2 Compilar el binario de escritorio para uso real (`run-desktop.sh`)

`tools/scripts/build-desktop.sh` compila `.build/desktop-release/lumen` (usando automáticamente `lumen-qt-make:latest` si `cmake` no está instalado en el host):

```bash
cd ${LUMEN_WORKSPACE}/lumen-pkm
tools/scripts/build-desktop.sh
```

Para publicar el binario en `${LUMEN_WORKSPACE}/releases/0.6/lumen` de forma atómica (`cp` temporal + `mv`, sin romper un proceso abierto):

```bash
cd ${LUMEN_WORKSPACE}/lumen-pkm
tools/scripts/build-desktop.sh --install
```

Para lanzar la aplicación en el escritorio del usuario:

```bash
${LUMEN_WORKSPACE}/lumen-pkm/tools/scripts/run-desktop.sh
```

---

## 3. Guía para corregir el editor (`RichEditor`) y la vista de página (`MainWindow`)

Antes de escribir programas sonda en `scratch/`, revisa directamente los archivos fuente y sus invariantes geométricos:

### 3.1 Archivos clave de UI y editor

- [`desktop/src/editor/lumen_editor.hpp`](../../desktop/src/editor/lumen_editor.hpp) y [`desktop/src/editor/lumen_editor.cpp`](../../desktop/src/editor/lumen_editor.cpp) — `RichEditor`: bloques estructurados, atajos Markdown, listas, tareas (`todo`), divisores (`divider`), tirador `⋮⋮` (`blockHandle_`) y drag & drop (`performBlockMove`).
- [`desktop/src/ui/main_window_ui.cpp`](../../desktop/src/ui/main_window_ui.cpp) — `MainWindow::renderEditor`: maquetación de `documentCanvas` (título de página/diario, filtros `"Todo | Nota diaria | Creado hoy"`, encabezado `"Nota diaria"`, `RichEditor` y secciones inferiores).
- [`desktop/src/app/main_window.cpp`](../../desktop/src/app/main_window.cpp) — hoja de estilos global QSS (`#documentCanvas`, `#documentTitle`, `#editor`).

### 3.2 Invariantes de alineación horizontal y sangría

1. **Eje izquierdo único en `x = 32 px`**:
   - `RichEditor` reserva `document()->setDocumentMargin(32)` para que el tirador `⋮⋮` (`blockHandle_`, de `18×22 px`) quepa a la izquierda del texto en `x = 6..24`.
   - Por tanto, en `MainWindow::renderEditor` ([`main_window_ui.cpp`](../../desktop/src/ui/main_window_ui.cpp)), los contenedores fuera del editor (`headerPanel`, `heading` `"Nota diaria"`, `notesFooter` y `createdPanel`) **deben mantener `setContentsMargins(32, 0, 32, 0)`**, mientras que el widget `editor` tiene margen `0`. Así el título de la fecha, `"Nota diaria"`, los párrafos, las tareas y los divisores comparten exactamente el mismo eje vertical (`x = 32`).
2. **Tareas (`todo`) y `toggle` sin `leftMargin` extra**:
   - Los bloques `todo` (`○ ` / `✔ `) y `toggle` (`▶ ` / `▼ `) deben tener `leftMargin(0)` tanto en `setStructuredBlocks` como en `applyBlockStyle`. Nunca les pongas `setLeftMargin(8)`.
3. **Sangría de listas y tabuladores**:
   - `document()->setIndentWidth(16)` (16 px por nivel de lista/bloque, nunca el valor por defecto de 40 px de Qt).
   - `setTabStopDistance(24)` y captura de `Tab`/`Shift+Tab` en `RichEditor::keyPressEvent` para cambiar el nivel de lista (`QTextListFormat::indent`) o de bloque (`QTextBlockFormat::indent`) sin insertar caracteres `\t` crudos.
4. **Divisor `---` y centrado del tirador `⋮⋮`**:
   - El bloque `divider` contiene un único carácter `U+FFFC` con `QTextImageFormat` de `height = 2` y ancho `viewport()->width() - 2 * documentMargin()`.
   - Siempre debe llevar `Qt::AlignLeft` y `divCharFmt.setFontPointSize(1)` en el carácter y en el `blockCharFormat` para que la altura de línea sea de `2 px` reales y `positionBlockHandle()` centre el icono `⋮⋮` sobre la línea (`r.top() + (r.height() - blockHandle_->height()) / 2`).
   - `blockAtViewportPoint(point)` debe evaluar solo la coordenada vertical `docY` (nunca filtrar por `X`, o dejará de funcionar cuando el ratón entre al carril izquierdo `x = 0..31`) y priorizar la banda vertical del divisor.

### 3.3 Invariantes de `QTextCursor` al mover o borrar bloques (`performBlockMove` / `deleteBlock`)

En `QTextDocument`, `select(QTextCursor::BlockUnderCursor)` se comporta distinto en el **primer bloque del documento (`document()->begin()`)** que en el resto:
- En cualquier bloque `n > 0`, selecciona el separador de párrafo anterior + el texto del bloque.
- En el **bloque 0**, no hay separador anterior; si borras el texto y haces `deleteChar()`, Qt fusiona el bloque 1 dentro del bloque 0 y **machaca el `QTextBlockFormat` y el `QTextList` del bloque 1 con los del bloque 0**.

Por eso, en `performBlockMove`:
1. **Inserta primero en el destino (`target`)** mientras `source` y `target` están intactos.
2. En `DropAfter` con `target.next().isValid()`, el bloque que se parte es `destBlock = target.next()`; restaura `destBlock.blockFormat()` (no `target.blockFormat()`).
3. **Elimina después `source`**: si `source == document()->begin()`, guarda antes `nextBlk.blockFormat()`, `nextBlk.charFormat()` y `nextBlk.textList()` y restáuralos tras borrar el rango `[0, nextBlk.position())`.

---

## 4. Errores frecuentes que ningún agente debe repetir

1. **No ejecutar diagnósticos a ciegas del sistema (`rpm -qa`, `find /usr`, `cmake --system-information`)**: usa directamente el contenedor `lumen-qt-make:latest` como se muestra en la sección 2.
2. **No crear múltiples programas sonda (`probe.cpp`, `probe2.cpp`, `probe3.cpp`) para entender `QTextCursor`**: añade el caso de prueba directamente en [`desktop/tests/editor_blocks_test.cpp`](../../desktop/tests/editor_blocks_test.cpp) y ejecútalo con `lumen-editor-blocks`.
3. **Si generas una captura de verificación en `scratch/<fecha>-<tarea>/`, ábrela y verifícala visualmente** antes de dar por buena la tarea (no dejes imágenes antiguas o corruptas en disco).
4. **No tocar nunca `ops/personal/` ni el contenedor del puerto `8765`**.
