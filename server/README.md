# Servidor Lumen

API Go con almacén SQLite propio y cifrado AES-256-GCM. El servidor es la fuente
de verdad; no usa Logseq, Node ni un segundo motor de persistencia. El cliente de
escritorio es C++/Qt y no ejecuta Electron.

## Instalación con Docker

Este directorio solo contiene el código y el `Dockerfile`. El Compose de uso
diario y los secretos viven en `${LUMEN_WORKSPACE}/ops/personal/`, fuera del repositorio:

```bash
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml build
bash ${LUMEN_WORKSPACE}/lumen-pkm/tools/scripts/bootstrap-secrets.sh
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml up -d
curl --fail http://127.0.0.1:8765/health
```

Para pruebas desechables (8879) usa `../sandbox/compose.test.yaml`.

`/health` debe devolver `status: ready`, `source_of_truth: lumen-store` y
`pending: 0`. `docker compose down` conserva el volumen; **`down -v` lo borra**.
Guarda la llave por separado: sin ella no se puede descifrar el almacén ni los
adjuntos.

Para el cliente Qt, `LUMEN_API_URL=http://127.0.0.1:8765` y
`LUMEN_TOKEN_FILE=/ruta/ops/personal/secrets/client.token`. El puerto queda publicado
solamente en loopback. En servidor remoto utiliza un proxy HTTPS, VPN o túnel; no
publiques el puerto HTTP directamente. Añade únicamente los orígenes exactos de
tu extensión en `LUMEN_ALLOWED_ORIGINS`, separados por comas, y recrea el
servicio. No hay CORS con comodín.

El bootstrap (`tools/scripts/bootstrap-secrets.sh`) crea los secretos sin mostrarlos y se niega a reemplazarlos. No
regeneres claves para reparar permisos. En Docker nativo, si restauras archivos
con otro propietario, corrige únicamente el volumen de este proyecto con el
servicio detenido.

## Modelo y API

Todo `/v1/*` requiere `Authorization: Bearer <token>`. Las respuestas de tipos y
objetos son arrays directos; detalle, diario y captura devuelven un objeto
directo. Los errores son `{ "error": "..." }`. No se registran notas, URLs ni
tokens en los logs HTTP.

Objeto: `id`, `type_id`, `title`, `content` (texto), `properties` (propiedades
dinámicas, incluido `rich_text` HTML), `links` (IDs de otros objetos), fechas ISO
UTC, `revision` y `sync_status` (siempre `synced` por compatibilidad).

`properties.aliases` es una propiedad por defecto de todos los objetos: lista de
hasta 50 cadenas de 200 caracteres, sin duplicados ignorando mayúsculas. Los
objetos sin alias no tienen la clave.

| Ruta | Uso |
|---|---|
| `GET /health` | Estado sin contenido privado |
| `GET/POST /v1/types` | Tipos y tipos personalizados |
| `GET/POST /v1/objects` | Listar o crear; filtros `type_id`, `q` |
| `GET/PATCH/DELETE /v1/objects/{id}` | Ficha, edición parcial y envío a la papelera |
| `GET /v1/objects/{id}/backlinks` | Objetos que enlazan la ficha |
| `GET /v1/trash` | Papelera: `id`, tipo, título y `deleted_at`; filtros `q`, `type_id` |
| `POST /v1/objects/{id}/restore` | Restaurar un objeto de la papelera |
| `DELETE /v1/trash/{id}` | Borrado definitivo de un objeto de la papelera |
| `POST /v1/trash/empty` | Vaciar la papelera; `?type_id=` limita a un tipo |
| `GET /v1/journal/YYYY-MM-DD` | Obtener o crear el diario |
| `GET /v1/search?q=...` | Búsqueda en títulos, texto y alias; `matched_alias` indica el alias que coincidió |

| `GET/POST /v1/objects` | Listar o crear; filtros `type_id`, `q`, `include_templates` |
| `POST /v1/objects/from-template` | Crear un objeto desde una plantilla con variables `{date:...}` |
| `GET/PATCH/DELETE /v1/objects/{id}` | Ficha, edición parcial y eliminación |
| `GET /v1/objects/{id}/backlinks` | Objetos que enlazan la ficha; `include_templates` |
| `GET /v1/journal/YYYY-MM-DD` | Obtener o crear el diario; `apply_default_template` |
| `GET /v1/search?q=...` | Búsqueda en títulos y texto desde el índice en RAM |
| `POST /v1/capture` | `{url?,title?,selection?,type_id?,date?,source?,fetch_preview?}` |
| `POST /v1/assets` | Multipart `object_id` y `file`, hasta 50 MB |
| `GET /v1/assets?object_id=...` | Metadatos de adjuntos |
| `GET /v1/assets/{id}/content` | Bytes autenticados con `Cache-Control: no-store` |
| `POST /v1/sync` | No-op de compatibilidad; devuelve `/health` |

`DELETE /v1/objects/{id}` mueve el objeto a la papelera y conserva su contenido
cifrado hasta restaurarlo o vaciarla. Los objetos en la papelera desaparecen de
listados, búsqueda y backlinks, pero los enlaces entrantes se conservan para que
la restauración los recupere; el borrado definitivo los limpia. La papelera no
se vacía sola en esta versión.

`PATCH /v1/objects/{id}` acepta `base_revision` opcional. Si no coincide con la
revisión actual, responde 409 con el objeto vigente en `current`. Sin ese campo
se conserva el comportamiento anterior. También acepta `add_links: ["ID", ...]`:
añade relaciones a la versión actual bajo el mismo bloqueo de escritura que el
guardado, valida los destinos y elimina duplicados. `links` reemplaza el conjunto
completo; si se envían ambos, se aplica primero el reemplazo y después la unión.

La captura obtiene OpenGraph HTML público, bloquea redes privadas/reservadas y
URLs con credenciales, valida DNS al conectar y fija la conexión a una IP
comprobada. Cada redirección se valida. No envía tu selección, cookies ni token a
la web consultada. Hay límites de tiempo, tamaño, MIME y dimensiones de imagen.
Las portadas públicas se copian como adjuntos cifrados; los clientes no consultan
al proveedor de imágenes. No ejecuta JavaScript ni elude cuentas privadas.

## Persistencia y límites

El volumen `/data` contiene `lumen-cache.sqlite`, que guarda objetos, tipos,
tombstones, papelera (`trash`), assets, blobs, `sync_changes`, `sync_ops` y `meta`. Los payloads y
blobs se cifran con AES-256-GCM, nonce aleatorio y datos asociados al ID. Los IDs
y la clase de registro no se ocultan. Los adjuntos también se cifran y sólo se
descargan por la API autenticada; el upload no deja temporales en texto claro.

El servidor descifra los objetos al arrancar para construir un índice en RAM con
**metadatos**: id, tipo, título, fechas, revisión, enlaces y backlinks inversos.
El contenido, las propiedades y los blobs siguen cifrados en SQLite y se
descifran solo al responder una petición que los necesita. Un PDF no se mantiene
en RAM entre peticiones. El índice se actualiza sólo después de un `COMMIT`; si
el proceso cae, se reconstruye desde disco.

**No hay E2EE.** El servidor puede leer las notas mientras está en ejecución.
HTTPS protege el transporte, no cambia esa limitación. No se debe anunciar este
MVP como un sistema E2EE.

No hay worker de exportación ni estado “pendiente de Logseq”. Una respuesta HTTP
de guardado significa que el objeto está confirmado en disco. Los títulos de los
objetos normales siguen siendo únicos en el espacio de trabajo; las plantillas
(`properties.is_template: true`) viven fuera de ese espacio y los títulos vacíos
se permiten (los clientes muestran un marcador). El límite de documento lo impone
el tamaño máximo de la petición, no una CLI externa.

## Copia y recuperación

Detén el servicio para una copia consistente del SQLite, sus archivos WAL/SHM y
los blobs. Respalda el volumen completo y conserva la llave fuera del backup y
una copia protegida del token. Antes de moverlo a un servidor comprueba una
restauración local. Sin la llave, el backup es inútil.

## Desarrollo

```bash
go test -race ./...
go vet ./...
go build -o lumen-server .
./lumen-server -init-secrets ./secrets
```

La guía de pruebas del backend
([`docs/guides/BACKEND-TESTING.md`](../docs/guides/BACKEND-TESTING.md)) explica
cómo ejecutar la suite completa, cómo lanzar un escenario por nombre, qué cubre
cada recorrido de usuario y cómo verificar manualmente contra el servidor
desechable del puerto 8879. Los escenarios de extremo a extremo viven en
`server/scenarios_*.go` y combinan varias etapas (diario, captura, tareas,
colecciones, plantillas, papelera, conversión, fusión, historial y export).

Para arrancar fuera de Docker configura `LUMEN_KEY_FILE`, `LUMEN_TOKEN_FILE`,
`LUMEN_DATA_DIR` y opcionalmente `LUMEN_LISTEN` (por defecto
`127.0.0.1:8765`). El binario incluye `-healthcheck` para el `HEALTHCHECK` de
Docker y `-fsck` para verificar invariantes del almacén.

Las pruebas verifican autenticación, origen CORS exacto, relaciones, diario,
concurrencia, restauración de datos cifrados, rechazo de llave equivocada,
adjuntos autenticados, CAS opcional, tombstones, enlaces muertos, merge de listas
y guardas SSRF/parseo OpenGraph. Esas pruebas no equivalen a una auditoría de
seguridad ni a comprobar todas las webs remotas.
