# Instrucciones para continuar Lumen

Este archivo se aplica a este proyecto y sus subdirectorios. Las instrucciones del usuario prevalecen. Lumen es un PKM personal nativo, basado en objetos y diario, cuya referencia de interacción y apariencia es Capacities.

## Leer antes de cambiar

1. [Índice y estado del proyecto](docs/README.md).
2. **[Flujo de trabajo de Qt 6 Desktop, compilación, tests y editor](docs/guides/QT-DESKTOP-WORKFLOW.md)** (imprescindible antes de compilar o tocar `desktop/`).
3. [Diseño e interacción](docs/design/DESIGN.md) para cualquier cambio de UI.
4. [Arquitectura e invariantes](docs/architecture/ARCHITECTURE.md).
5. [Arranque y pruebas](docs/guides/RUNBOOK.md) antes de ejecutar servicios o aceptación.
6. [Contrato de datos](contracts/SYNC.md) si se cambia persistencia. El almacén propio es la fuente de verdad; Logseq ya no forma parte de la arquitectura.

No confundas el historial de pruebas en VERIFICATION.md con una verificación de tu cambio actual.

## Decisiones del producto

- Escritorio C++20/Qt 6 Widgets nativo. No sustituirlo por Electron, WebView, una web o un framework nuevo sin petición del usuario.
- API en Go con almacén propio SQLite cifrado (AES-256-GCM). No reintroducir Logseq, Node ni un segundo motor de persistencia.
- Extensión MV3 en JavaScript. Usar siempre pnpm; nunca npm, yarn ni npx.
- Priorizar escribir, relacionar y volver al contexto; objetos tipados y propiedades antes que carpetas o sintaxis visible.
- No introducir IA, telemetría, servicios externos, fuentes remotas ni dependencias por defecto.
- Reutilizar los módulos y recursos existentes. No crear un segundo editor, evaluador de consultas o sistema de iconos.

## Estructura

- `desktop/src/{app,editor,objects,views,ui,sync}` — `app/main.cpp` es el punto de entrada; `MainWindow` está declarado en `app/main_window.hpp` y definido por dominio en `app/`, `ui/`, `views/` y `sync/`.
- `contracts/` — `SYNC.md` y `fixtures/` compartidos por las cuatro apps.
- `tools/docker/` y `tools/scripts/` — Dockerfiles y scripts reutilizables.
- `sandbox/compose.test.yaml` — servidor desechable del puerto 8879.
- `docs/{guides,design,architecture,tracking,qa,screenshots,archive}` — documentación.
- Builds en `.build/<componente>-<preset>/` (ignorado por Git). Entregables en `../releases/<versión>/`. Temporales en `../scratch/<fecha>-<tarea>/`.

## Protección de la instalación

- El Compose de uso diario vive en `../ops/personal/compose.yaml` y publica 127.0.0.1:8765. Las pruebas que crean, modifican o borran datos deben usar `sandbox/compose.test.yaml`, puerto 8879, volumen y claves propios.
- No ejecutar seeds, aceptación, harness de extensión ni pruebas de recuperación contra la biblioteca personal.
- No regenerar claves existentes. No imprimir tokens, copiar secretos a la documentación, incluirlos en capturas ni añadirlos a Git o al paquete.
- No eliminar volúmenes de la instalación personal, reiniciar/migrar la instalación personal ni cerrar el cliente del usuario para probar una interfaz.
- El cliente mantiene notas y medios en RAM. QSettings se reserva a preferencias visuales. No introducir una caché de notas en disco como efecto secundario.
- El almacén y los adjuntos del servidor están cifrados en disco, pero no son E2EE: el servidor descifra el contenido en RAM para indexarlo y servirlo.

## Forma de trabajar

1. Inspeccionar archivos, estado de servicios y cambios existentes. En un checkout con Git, revisar status/diff; si no hay repositorio, no inventar commits o ramas.
2. Convertir la petición en estados y recorridos concretos; distinguir lo observado de lo supuesto.
3. Hacer el cambio mínimo coherente. Conservar IDs, undo, foco, cursor, origen de escrituras y preferencias.
4. Ejecutar las pruebas pertinentes y documentar el resultado real. Un componente aprobado no prueba la integración, y offscreen no prueba el compositor.
5. Para UI, capturar el componente o ventana real con datos ficticios. Comparar referencia y resultado juntos, con escala, tema y estado declarados; volver a capturar los defectos corregidos.
6. Explicar qué cambió, cómo se verificó y los límites materiales. No declarar paridad completa con Capacities, ausencia de conflictos concurrentes ni seguridad auditada.

## Compilación y entrega

- En esta máquina, el host tiene las bibliotecas de ejecución (`/lib64/libQt6*.so.6`), pero `cmake`, `ninja`, `g++` y las cabeceras de Qt 6 viven en la imagen Docker local `lumen-qt-make:latest`. No pierdas tiempo ejecutando `rpm -qa`, `find /usr` ni buscando `cmake` en el host; consulta [docs/guides/QT-DESKTOP-WORKFLOW.md](docs/guides/QT-DESKTOP-WORKFLOW.md).
- Compilar siempre dentro de `.build/<componente>-<preset>/` con `--parallel 2` (nunca `-j$(nproc)` para evitar OOM de `cc1plus`, y nunca crear `desktop/build-*`). No sobrescribir el ejecutable distribuido durante una prueba de componentes.
- Para actualizar un binario en uso, instalar primero un archivo temporal y reemplazarlo con mv; no usar cp sobre el ELF abierto ni matar la aplicación del usuario. `tools/scripts/build-desktop.sh` (y `--install`) ya usa `lumen-qt-make:latest` automáticamente cuando `cmake` no está en el host.
- Una entrega con binario nuevo va a `../releases/<versión>/` y debe actualizar BUILD-INFO.json y las pruebas/capturas pertinentes. El paquete excluye secretos, datos, backups, compilaciones y referencias privadas.
- Fuentes e iconos deben tener licencia y origen registrados. La preferencia personal por una fuente de pago no concede una licencia para descargarla o redistribuirla.
