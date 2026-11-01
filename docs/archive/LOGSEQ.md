# [Archivado] Motor Logseq: investigación, contrato y validación

> **Archivado el 30 de octubre de 2026.** Lumen ya no usa Logseq como motor ni
> como exportador. Este documento se conserva como historial de la integración
> retirada; no describe la arquitectura actual. Consulta
> [ARCHITECTURE.md](../architecture/ARCHITECTURE.md) y [SYNC.md](../../contracts/SYNC.md).

Motor fijado e investigación del 9 de octubre de 2026; recuperación y regresiones revalidadas el 29 de octubre de 2026. Lumen usa un cliente Qt/C++ propio y un servidor Go. El motor reutilizado es **Logseq DB oficial, headless**, ejecutado con Node. No arranca Electron. No escribe directamente tablas de SQLite ni intenta reproducir el esquema interno de Logseq.

## Qué existe antes de construir

La búsqueda de clientes GTK/Qt que reproduzcan el flujo de objetos de Capacities sobre Logseq DB no encontró un proyecto mantenido que cumpla esos requisitos. Esta búsqueda no prueba que no exista ningún experimento privado o poco indexado.

Sí existen piezas útiles ya construidas:

- [Logseq CLI oficial](https://github.com/logseq/logseq/blob/master/docs/cli/logseq-cli.md): crea grafos y entidades, consulta, modifica nodos, importa adjuntos y exporta/respaldos sin abrir la interfaz de escritorio. Es la pieza reutilizada.
- [yshalsager/logseq-selfhost](https://github.com/yshalsager/logseq-selfhost): imágenes de sincronización, publicación y cliente web. No proporciona un frontend nativo Qt/GTK con la experiencia de Capacities.
- [brynmrgn/logseq-selfhost-mcp](https://github.com/brynmrgn/logseq-selfhost-mcp): demuestra el patrón puente hacia clientes headless y grafos cifrados en sincronización. Requiere infraestructura, autenticación y versiones compatibles; no es el cliente humano nativo buscado y no se incluye en Lumen.
- [CLI distribuida en el registro](https://www.npmjs.com/package/@logseq/cli): `@logseq/cli@0.4.3` tiene el conjunto anterior de comandos, no los comandos `upsert` del motor actual. Se verificó instalándola en un directorio temporal; no se usa en la aplicación.

Fuentes de la búsqueda comunitaria: [alternativas a Capacities en r/selfhosted](https://www.reddit.com/r/selfhosted/comments/129wmux/selfhosted_alternative_to_capacitiesio/) y [experiencias recientes con Logseq DB en r/logseq](https://www.reddit.com/r/logseq/comments/1w0nm8f/i_just_really_love_the_db_version/). Las capacidades técnicas del MVP se comprobaron contra el código y el motor oficial, no se dedujeron de comentarios.

## Runtime fijado y reproducible

Se extraen los dos runtimes headless y sus dependencias de la [release oficial 2.0.2](https://github.com/logseq/logseq/releases/tag/2.0.2), revisón de CLI `533ac43`, build `2026-10-07T14:07:14.501Z`. Node probado: `v24.20.0`, Linux x86_64.

El instalador `server/install-logseq-runtime.sh` descarga el ZIP, verifica el SHA256 oficial y utiliza pnpm para las dependencias. El empaquetado ASAR se abre como formato de archivo durante la instalación; **no se ejecuta ni incluye la interfaz Electron**. El runtime resultante ocupa aproximadamente 58 MiB en este entorno.

SHA256 fijado de `Logseq-linux-x86_64-2.0.2.zip`:

```text
289eadd4dfcbdd2ab71aeb6cf02a53d0b5d7340865c528b1434bef8001900a9a
```

Requisitos de instalación: Node 24, pnpm, bash, curl, unzip, sha256sum. `keytar` es un módulo N-API requerido por upstream, enlazado con `libsecret-1.so.0`; en Debian se instala `libsecret-1-0`. Las operaciones sobre grafos locales no requieren abrir Electron ni configurar un servicio de sincronización.

```bash
bash server/install-logseq-runtime.sh /opt/logseq
```

Dependencias pnpm fijadas: `@emoji-mart/data@1.2.1`, `chrono-node@2.9.0`, `jszip@3.10.1`, `mldoc@1.5.9`, `picocolors@1.1.1`, `remove-accents@0.5.0`, `sanitize-filename@1.6.4`, `tiny-pinyin@1.3.2`, `ws@8.20.0`. `@logseq/graph-lifecycle` y `keytar` se conservan desde la release verificada. El módulo opcional de embeddings `@zvec/zvec` no se instala; Lumen no usa funciones de IA.

El instalador está fijado a x86_64. No se ha validado ARM64 ni el antiguo paquete publicado como equivalente a este runtime.

## Configuración del adaptador Go

```text
LOGSEQ_NODE=node
LOGSEQ_CLI=/opt/logseq/logseq-cli.js
LOGSEQ_ROOT=/data/logseq
LOGSEQ_GRAPH=lumen
```

`LOGSEQ_ROOT` debe ser un volumen persistente. Si el grafo no existe, el adaptador crea uno mediante el comando oficial. Nunca sustituye un grafo existente. `Health()` distingue un adaptador no configurado, un error del motor y un grafo realmente disponible; no convierte un cache pendiente en éxito de Logseq.

Contrato de `server/logseq_adapter.go`, paquete `main`:

```go
NewLogseqAdapter() *LogseqAdapter
Health(context.Context) map[string]any
List(context.Context) ([]Object, error)
Put(context.Context, Object) error
Delete(context.Context, string) error
PutAsset(context.Context, string, string, string) (int64, error)
```

`PutAsset` está disponible para importación explícita en Logseq, pero los adjuntos de la aplicación se almacenan en su depósito cifrado, no se duplican automáticamente como archivos planos en el grafo. Esto implica que un respaldo completo incluye ambos depósitos.

## Cómo se representan los objetos

Un objeto de Lumen es una página Logseq con su título mostrado y una clase de tipo: Libro, Persona, Proyecto, Nota, Enlace web o Diario. Las páginas nuevas usan `block/name = lumen-<ID del objeto>` como clave interna estable, independiente del título humano. Así puede borrarse un objeto y crearse otro con el mismo título y un ID nuevo, aunque Logseq conserve la primera página en su papelera. La clase interna `Lumen Object` permite localizar sólo entidades administradas por esta aplicación. La propiedad oculta `Lumen Metadata` contiene ID estable, tipo, propiedades, relaciones y la referencia al bloque de cuerpo.

El contenido es un bloque Logseq ordinario. Las conexiones son otro bloque con referencias Logseq reales `[[lumen-ID]]`. La interfaz nativa resuelve el ID y dibuja el título humano como chip: el usuario no escribe claves UUID, `#Libro` ni necesita editar la sintaxis interna.

Un objeto o diario vacío es una página sin bloque de cuerpo (`_logseq_body_id: 0`). El CLI oficial rechaza crear bloques con contenido vacío. Al vaciar una nota, el adaptador recicla su bloque anterior y conserva su página, propiedades y conexiones; no escribe texto de relleno. Sólo la metadata final marca esta página vacía como sincronizada. Un registro intermedio pendiente sin cuerpo continúa devolviendo un error para que su outbox se reintente.

Un renombrado utiliza `upsert page --id … --update-properties '{:block/title "Título nuevo"}'`. Se verificó que conserva ID y el nombre normalizado original del nodo, mientras cambia el título mostrado. Por tanto los nodos existentes conservan una identidad estable, incluso cuando sus títulos se modifican.

Los journals nativos antiguos tienen `block/journal-day` y un título que Logseq protege. Se recuperó un diario pendiente real de la primera ejecución: su actualización fallaba al intentar cambiar ese título. El adaptador conserva el título nativo y guarda el título mostrado por Lumen en su metadata; continúa actualizando cuerpo, tipo y relaciones mediante el CLI oficial. Esta excepción permite reintentar su outbox sin borrar ni sustituir el diario existente.

Las propiedades personalizadas y el HTML del editor se conservan dentro de la propiedad de integración. No todas aparecen como propiedades independientes de Logseq; convertir cada propiedad a un esquema bidireccional nativo más amplio queda fuera del MVP. El diario es un tipo de objeto propio; todavía no se configura toda la semántica de calendarios/journals interna de Logseq.

Las consultas recuperan títulos y cuerpos actuales desde Logseq. Una modificación al texto o al título realizada directamente en el motor puede reflejarse al recargar. Los objetos guardados por el adaptador incluyen un checksum del cuerpo: si otro cliente cambia el texto, se descarta el HTML formateado que haya quedado desactualizado y se muestra el nuevo texto. Los registros antiguos sin ese checksum necesitan una guardada con la versión actual para adquirirlo. Editar arbitrariamente la propiedad interna de integración puede invalidar el objeto y devolver un error explícito.

Las referencias administradas usan el `block/name` estable, porque crear una referencia `[[Título nuevo]]` después de cambiar solamente el display title genera otra página en Logseq. Este caso se comprobó y corrigió en el adaptador. Los objetos guardados por las primeras pruebas conservan su nombre original; los nuevos usan la clave interna `lumen-ID`. Los chips de Lumen siguen mostrando el título actual por ID. No se renombran ni sobrescriben páginas preexistentes de otros clientes.

## Comandos ejecutados y resultados

Los siguientes ejemplos usan datos ficticios, ningún grafo personal:

```bash
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json graph create
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json upsert tag --name Libro
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json upsert page --page Ficciones --update-tags '["Libro"]'
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json upsert block --target-page Ficciones --content 'Una nota de lectura.'
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json show --page Ficciones
```

Resultado comprobado: envoltorios `{"status":"ok","data":…}`, página tipada, cuerpo persistido y lectura del mismo grafo. Además se ejecutó una prueba Go contra el runtime mínimo: creación de Libro y Diario, lectura de dos objetos, relaciones, cambio de título, actualización de cuerpo y borrado mediante papelera. La prueba pasó con el motor real, sin mock ni manipulación directa de SQLite. Una prueba posterior verificó también borrar y recrear el mismo título con otro ID, reutilizar el título antiguo después de un renombrado y conectar ambos objetos a los IDs correctos.

La prueba de contenido vacío verificó crear un Libro vacío y un Diario vacío con conexión, leer ambos sincronizados, escribir texto, vaciarlo, repetir la guardada vacía y recargar sin perder conexiones. Se volvió a validar el grafo real; no se dedujo este comportamiento de un mock.

La regresión reproducible `server/logseq_integration_test.go`, ejecutada el 29 de octubre, comprobó también recuperar un journal nativo con título protegido y quitar la última relación: al quedar vacía se recicla el bloque de referencias, porque el CLI también rechaza actualizar ese bloque con texto vacío. Pasó contra Node24 y el runtime 2.0.2 fijado. El grafo existente de la demo volvió a `pending: 0`; `graph validate` devolvió `errors:null` e `invalid-entity-ids:[]`.

También se importó un SVG ficticio mediante `upsert asset`; `list asset --fields id,title,asset-type,size` devolvió un nodo Asset, tipo `svg`, tamaño 214 bytes. No se deduce de ello una prueba completa de anotaciones PDF.

```bash
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json graph validate
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json graph backup create --name smoke
node /opt/logseq/logseq-cli.js --root-dir /data/logseq --graph demo --output json graph backup list
```

La validación del grafo ficticio devolvió `errors:null` e `invalid-entity-ids:[]`. Se restauró un backup SQLite en otro grafo con `graph backup restore --src … --dst …` y se recuperó el título modificado. Esta restauración valida notas/entidades, no restaura archivos de adjuntos que el backup de Logseq no incluye.

## Persistencia, privacidad y límites

Docker preserva datos sólo si sus volúmenes sobreviven. El backup incorporado de Logseq copia **sólo `db.sqlite`**, no todos los assets, cache de búsqueda ni el depósito cifrado propio de Lumen. Para migración completa se respaldan el volumen Logseq, el volumen de aplicación/adjuntos y las claves de cifrado por separado; para copiar archivos SQLite y su WAL de forma consistente se detienen el servidor y sus workers antes de la copia, o se utilizan exports/snapshots oficiales.

**El grafo local de Logseq de este MVP contiene texto y metadatos legibles.** El cifrado AES-GCM del cache y depósito de adjuntos de Lumen no cifra la base SQLite de Logseq. TLS protege el transporte cuando se configura en el proxy, y un volumen cifrado protege el almacenamiento del servidor apagado. No se presenta este MVP como cifrado de extremo a extremo; las claves y datos están disponibles para el servidor que los procesa. [Documentación de cifrado/sync del CLI](https://github.com/logseq/logseq/blob/master/docs/cli/logseq-cli.md).

Otros límites deliberados:

- El CLI requiere Node aunque el código de la aplicación sea Go/C++ y la UI sea Qt nativa. Si se prohíbe cualquier runtime Node incluso en el motor, reutilizar Logseq DB oficial de esta forma deja de ser posible.
- Las operaciones de una guardada usan varios comandos, no una transacción única que abarque toda la API. El servidor mantiene estado pendiente y errores; una interrupción puede dejar una guardada parcialmente aplicada que debe reintentarse.
- El adaptador limita cuerpo y argumento de metadatos a 96 KiB por los límites de argumentos del proceso. El servidor aplica un límite menor a documentos formateados. No es un editor para documentos enormes en esta versión.
- La interfaz mantiene únicos los títulos humanos de sus objetos activos en este MVP. La clave interna UUID evita colisiones con páginas borradas, renombradas o externas. Reutilizar el ID de un objeto borrado no equivale a crear uno nuevo; se requiere un ID nuevo. Las páginas ajenas no se importan ni adoptan automáticamente.
- Borrar usa la papelera de Logseq. La consulta excluye `:logseq.property/deleted-at`; la retención y eliminación definitiva no se prometen desde este adaptador.
- La sincronización remota de Logseq no se probó. No se presume que selfhosting elimine todas las dependencias de autenticación de los servicios Logseq.
- Esta integración administra sus objetos; no importa automáticamente cada página, block o propiedad de un grafo Logseq arbitrario.

Fijar el runtime y el protocolo evita cambios silenciosos del motor durante la vida del MVP. Una actualización debe realizarse sobre una copia del grafo y repetir lectura, escritura, adjuntos y restauración antes de cambiar el servidor personal.
