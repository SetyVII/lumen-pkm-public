# Navegación, compartir enlaces y miniaturas — 01-11-2026

## Cambios

Cada visita a un objeto tiene una entrada propia en `ObjectHistory` y una
clave de estado de Compose. La flecha y `BackHandler` retiran una entrada,
recargan el objeto anterior y restauran su pestaña y estado guardado. Las
pantallas raíz conservan diario/fecha, pestaña de Cronología, búsqueda y scroll.
Las selecciones de pestaña también exponen su estado a accesibilidad.

Compartir un enlace usa `Theme.NoDisplay`: encola el trabajo y termina sin
presentar el diálogo anterior. El primer worker guarda en Weblinks localmente
y muestra «Guardado en Weblinks correctamente» sólo tras guardar. El payload de
WorkManager está cifrado. No se añade el enlace al diario. Otro worker espera
conexión, obtiene OpenGraph, comprime la miniatura a WebP y la guarda cifrada
junto con su operación de subida. Conserva notas, etiquetas y estado de revisión.
Compartir imágenes/texto y la actividad de nota rápida mantienen su interfaz.

La sincronización reintenta todos los assets elegibles que faltan en disco,
no sólo los del delta más reciente. Las imágenes de objetos `web` se consideran
miniaturas aunque su nombre no incluya `preview` ni `og-`. Las fichas abiertas
actualizan su asset cuando cambia la revisión del repositorio.

## Pruebas y evidencia

Entorno: emulador Android 16/API 36, 1080 × 2400, tema claro, dos núcleos y
1536 MB de RAM; disco de pruebas en `scratch/2026-11-01-mobile-navigation/`.
Sin emparejamiento ni acceso a la biblioteca personal. Los tests que escriben
fixtures comprueban que es un emulador sin servidor configurado.

- `:app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest`:
  **PASS**, 19 pruebas unitarias, incluidas dos de historial.
- `MobileNavigationTest`: **PASS**, `OK (1 test)`. Abre una imagen del diario,
  vuelve con el botón del sistema y verifica su posición vertical restaurada
  con tolerancia de 2 px. Desde Cronología abre una página, cambia a Gráfico,
  abre otra imagen, vuelve con un **gesto lateral real inyectado por Android**,
  verifica la pestaña Gráfico y vuelve con la flecha a Cronología seleccionada.
  [Diario restaurado](2026-11-01-mobile-navigation/journal-navigation-restored.png).
- `SharedLinkTest`: **PASS**. Lanza la actividad con `ACTION_SEND`, comprueba
  que la pantalla de origen sigue visible, observa el toast exacto mediante
  accesibilidad y verifica el objeto guardado y que el diario no cambió.
  Un HTTP local ficticio entrega HTML + PNG (dos peticiones); la miniatura
  queda comprimida a <=800 px y cifrada en disco. Tras apagar ese HTTP se
  descifra y renderiza de nuevo.
  [Miniatura sin servidor de origen](2026-11-01-mobile-navigation/shared-link-offline.png).
- `EditorRenderingTest`: **PASS** en emulador durante esta revisión; la prueba
  nativa previa en teléfono físico está en
  [QA del editor](2026-11-01-mobile-editor/README.md).
- `git diff --check`: **PASS**.

Se utilizó [Mobile MCP](https://github.com/mobile-next/mobile-mcp), versión
1.0.15, con `pnpm dlx`, para detectar el emulador e instalar el APK. Su servidor
de accesibilidad se detuvo antes de los tests instrumentados para evitar que
ambos compitieran por UiAutomation. No se modificó la configuración global de
Codex. El emulador se apaga al terminar para liberar recursos.

APK debug generado: `mobile/app/build/outputs/apk/debug/app-debug.apk`.
SHA-256: `059c83e1947e6a1d6b0623b79314080251fd6d4a8eef3752afb5438b1ac42620`.
Los cambios de navegación/Share Sheet no se han instalado en el teléfono físico.
No se afirma que todos los sitios permitan obtener una miniatura: se conserva
el enlace cuando OpenGraph no ofrece una imagen o bloquea su descarga.
