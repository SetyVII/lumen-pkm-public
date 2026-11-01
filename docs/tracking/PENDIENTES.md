# Pendientes

Actualizado el 30 de octubre de 2026. Distingue lo implementado de lo que falta;
no sustituye la verificación en dispositivo.

## OG / preview y Reddit

### Implementado

- **Android (`mobile/`)**:
  - `util/OgFetcher.kt` parsea `og:title`, `og:description`, `og:site_name`,
    `og:image`, `twitter:*` y `<title>`.
  - Descarga la imagen, la redimensiona a un máximo de 800 px y la guarda como
    WebP (~85 %) en el propio teléfono.
  - Reddit: OG directo, fallback `vxreddit` con User-Agent de embed y fallback
    Arctic Shift (`arctic-shift.photon-reddit.com`), con filtro de títulos
    bloqueados.
  - `UrlCleaner.kt` quita `utm_*`, `fbclid`, `gclid`, `si`, `igshid`, `ref`,
    `feature`, tokens; normaliza `youtu.be`, `/shorts/` y `redd.it`.
  - `ShareActivity` y `AddLinkDialog` usan ese pipeline.
  - `LumenRepository.saveWebLink` guarda `preview_status`, `og_title`,
    `og_description`, `og_site_name` e `image_asset_id`.
  - `ObjectCard` muestra la portada, descripción, estado y dominio.
- **Servidor Go**:
  - `preview.go` tiene el mismo pipeline Reddit (`PreviewReddit`, `vxreddit`,
    Arctic Shift) y parseo OG.
  - `/v1/capture` acepta una preview ya descargada por el cliente
    (`preview.image_base64` + `image_mime`) y no vuelve a pedirla.
  - `cleanURL` y deduplicación de enlaces implementados.

### Pendiente

1. **Extensión de navegador: OG local**. Hoy `extension/background.js` sólo
   envía la URL a `/v1/capture`; el servidor hace la preview. Si el enlace muere
   antes de sincronizar con la PC, la extensión no conserva portada. Falta
   descargar/parsear OG en el navegador y enviar `preview.image_base64`.
2. **Extensión: compresión WebP local**. Falta redimensionar a ~800 px y
   comprimir a WebP en el cliente, como ya hace Android.
3. **Extensión: paridad de `cleanWebUrl`**. `extension/core.js` aún no elimina
   `si`, `igshid`, `ref` ni `feature`, y no normaliza `youtu.be`, `/shorts/` ni
   `redd.it` como el servidor y Android.
4. **Servidor: WebP opcional**. El servidor guarda la imagen OG original; Android
   la comprime antes de subirla. Si se quiere que la extensión también suba WebP,
   el paso 2 lo cubre. Comprimir en servidor es opcional, no bloqueante.
5. **Prueba real de Reddit/OG en dispositivo**. El código existe, pero falta
   probar con enlaces reales de Reddit, YouTube, X/Instagram y un sitio con
   `og:image` detrás de redirects, verificando que la tarjeta conserve portada
   aun después de que el enlace original deje de responder.

## Otros pendientes de la app Android

- Cámara del sistema: la galería y el procesado WebP funcionan; falta validar
  `TakePicture` con la cámara de Motorola.
- Escáner QR: la pantalla abre y pide cámara; falta escanear un QR real.
- Notificación de tarea que vence hoy: canales y notificación de sync probados;
  falta disparar la notificación de tarea en dispositivo.
- Auto-sync Wi-Fi/mDNS desde el teléfono: el anuncio mDNS se ve desde la PC;
  falta probar el descubrimiento y sync automático desde el móvil sin USB.
- Conflicto de edición en dispositivo: el servidor conserva ambas versiones y
  los tests Go lo cubren; la app aún no tiene editor lado a lado.
- Re-pair a otro servidor: **resuelto en 0.6.1** con "Reenviar todo al
  servidor" (Ajustes → Sincronización); los objetos ya sincronizados se
  re-encolan sin duplicar y lo divergente se fusiona conservando ambas
  versiones. "Reenviar todo" también re-encola los medios sellados que
  tengan copia local.
