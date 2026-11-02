# Lumen · Captura desde el navegador

Extensión Manifest V3 en JavaScript sencillo, sin frameworks, dependencias, analítica ni proveedores externos. Captura la URL, el título y la selección de la pestaña activa; permite editarlos, crear un objeto **Enlace** o **Libro**, y enlazarlo al diario del día. El servidor obtiene OpenGraph, descarga su imagen y la sirve como adjunto privado: la extensión nunca carga imágenes remotas. La fuente Inter se incluye localmente bajo SIL OFL, con su licencia en `assets/fonts/LICENSE.txt`.

## Chrome / Chromium

1. Abre `chrome://extensions` y activa **Modo desarrollador**.
2. Pulsa **Cargar descomprimida** y elige esta carpeta `extension/` o el zip `a locally built package`.
3. Abre Lumen desde la barra del navegador o con **Alt + Shift + L**.
4. Introduce la URL de la API y el token del servidor. Por defecto usa `http://127.0.0.1:8765`.
5. Autoriza únicamente el servidor elegido. **Ctrl + Enter** guarda; **Esc** cierra.

Requiere Chrome 120 o posterior. Los atajos pueden cambiarse en `chrome://extensions/shortcuts` si hay conflicto.

## Firefox

Se entrega `a locally built package` con un manifest específico: Firefox usa scripts de background, mientras Chromium usa un service worker. Requiere Firefox 140 o posterior. Su declaración de transmisión informa de contenido web y autenticación porque se envían la captura y el bearer token a tu propio servidor; no implica telemetría de terceros.

1. Abre `about:debugging#/runtime/this-firefox`.
2. Pulsa **Cargar complemento temporal** y elige `manifest.json` dentro del zip de Firefox.
3. Sigue los mismos pasos de conexión. La instalación temporal desaparece al cerrar Firefox; una distribución permanente requeriría empaquetado y firma de Mozilla.

## API y orígenes

El servidor exige bearer token y admite orígenes exactos mediante `LUMEN_ALLOWED_ORIGINS`. En ajustes de la extensión, abre **Configurar el origen permitido** y añade ese valor al entorno del servidor. Chromium tiene un ID específico para tu instalación; Firefox usa su UUID interno para el origen HTTP. No uses `*` como atajo de configuración.

Contrato:

```http
POST /v1/capture
Authorization: Bearer <token de sesión>
Content-Type: application/json
```

```json
{
  "url": "https://example.com/article",
  "title": "Una lectura interesante",
  "selection": "Lo que quiero recordar",
  "type_id": "web",
  "date": "2026-10-09",
  "source": "browser-extension"
}
```

`type_id` admite `web` o `book`; `date` solo se envía si se marca el diario. La respuesta debe contener un objeto con `id`. OpenGraph se obtiene automáticamente si el sitio lo permite; Instagram, TikTok y sitios con autenticación pueden impedirlo. No se envían cookies, credenciales de la página ni su HTML completo.

## Privacidad y límites reales

- El token se guarda en `storage.session` (memoria del navegador), nunca en `storage.sync` o almacenamiento persistente. Se pierde al cerrar el navegador, recargar o desactivar la extensión. **Desconectar** elimina el token y revoca el permiso del servidor.
- Solo se conserva la URL de la API como preferencia local. Títulos, selección y URL capturada viven en el popup; no hay historial ni cola persistente de capturas.
- El permiso `activeTab` permite leer únicamente la pestaña que invocas. No hay scripts permanentes, historial, acceso a cookies ni acceso general a las páginas visitadas. Los permisos de servidor son opcionales; la declaración HTTPS amplia permite solicitar en ejecución únicamente el host elegido.
- HTTP solo se admite en `127.0.0.1` o `localhost`. Para un servidor remoto debes configurar HTTPS con certificado válido, red privada/autenticación y respaldo de los volúmenes según la documentación del servidor.
- La extensión omite cookies y referrer; rechaza redirecciones de la API para no redirigir el bearer token. El título y la selección se tratan siempre como texto, nunca como HTML.
- Se eliminan fragmentos, parámetros de seguimiento conocidos y ciertos parámetros de secretos de la URL. **Revisa siempre el enlace antes de guardar**: no es posible detectar automáticamente todos los secretos que una web incluya en una URL.
- HTTPS protege el transporte. El backend cifra su caché y adjuntos con AES-256-GCM, pero **el grafo de Logseq sigue siendo SQLite sin cifrado propio**: protege también el volumen del servidor con cifrado del disco y los respaldos. **Este MVP no implementa cifrado de extremo a extremo**: el servidor procesa las notas en claro. No promete ausencia de rastros en un ordenador ajeno o comprometido.
- Si se pierde la conexión después de enviar, el servidor podría haber guardado la captura. Comprueba la aplicación antes de repetir para evitar duplicados.

## Desarrollo y comprobación

```bash
pnpm test
pnpm build
```

No hace falta instalar paquetes: las nueve pruebas usan Node y el empaquetado copia archivos locales. Prueban validación de API, restricciones de transporte, limpieza de enlaces, límites de captura, permisos, emisor del mensaje y conservación del estado de sincronización pendiente de Logseq. La validación de navegador real requiere instalarla y guardar contra el backend autenticado; estos tests no sustituyen esa comprobación.

Prueba optativa contra tu servidor en marcha, con el token leído de un archivo sin imprimirlo:

```bash
LUMEN_TEST_TOKEN_FILE=/ruta/segura/api.token pnpm test:server
```

Se ejecuta el worker real en una VM con APIs de extensión simuladas y `fetch` real. Guarda una captura pública de `example.com`, comprueba título, selección, fuente, enlace del diario y rechazo del acceso anónimo; elige una fecha de diario libre y al terminar elimina solo sus objetos temporales. No verifica instalación, permisos ni CORS del navegador. En la comprobación del 29 de octubre de 2026, captura, lectura autenticada y diario pasaron; OpenGraph quedó explícitamente `unavailable`, y el origen simulado no autorizado recibió `403`.

Para inspeccionar el popup sin instalar la extensión:

```bash
pnpm preview
```

Abre `http://127.0.0.1:8789/preview`: es una **vista previa con datos ficticios**, sin token real, sin llamadas a la API y sin acceso a tu navegador. Usa el HTML y CSS de producción. El 29 de octubre de 2026 se revisó en Chrome headless con un perfil temporal independiente: sin errores JavaScript, captura y conexión con **Ctrl + Enter**, rechazo visible del servidor HTTP remoto y origen de extensión legible. El botón de guardar queda visible dentro de los 600 px de altura del popup de Chromium; los avisos se desplazan a la vista cuando aparecen. Esta revisión no instaló la extensión ni comprobó permisos o CORS de un navegador real.

## Trabajo previo revisado

Ya existen [j3sch/logseq-clipper](https://github.com/j3sch/logseq-clipper) y [clombion/logseq-clipper](https://github.com/clombion/logseq-clipper), forks del clipper de Obsidian que capturan hacia la API HTTP de Logseq. Sirven como referencia de integración; esta extensión pequeña usa código propio para mantener el alcance, la API de Lumen y permisos acotados, sin incluir funciones de IA.

Referencias de plataforma: [permisos opcionales de Chrome](https://developer.chrome.com/docs/extensions/develop/concepts/declare-permissions), [storage.session en Chrome](https://developer.chrome.com/docs/extensions/reference/api/storage), [storage.session en Firefox](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/API/storage/session), [background de Firefox](https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/background).
