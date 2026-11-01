# Texto móvil oculto en el diario de escritorio — 01-11-2026

## Diagnóstico en la instalación existente

ADB confirmó que el texto visible en el diario móvil también estaba en
`content` del mismo objeto en la API. Ambos clientes usaban el mismo
`server_id` y el móvil había alcanzado el `head_seq` del servidor. La entrega
al servidor estaba completada.

Sin embargo, `properties.blocks` y `properties.rich_text` aún representaban el
cuerpo anterior. El escritorio prioriza esos formatos al cargar. Además,
`pollWorkspace()` actualizaba la caché y la revisión del objeto abierto sin
recargar su editor. Avanzar esa revisión sin incorporar el cuerpo remoto podía
permitir que un guardado posterior sobrescribiera cambios no vistos.

## Correcciones

- El editor de texto móvil envía `blocks: null` y `rich_text: null` cuando
  cambia el cuerpo. No basta omitir esas claves: sync fusiona propiedades.
  Se conservan las propiedades restantes y los enlaces; el cuerpo editado en
  móvil pasa a texto plano. Editar con el mismo texto no genera otra operación.
- El sondeo de escritorio incorpora el objeto remoto al editor cuando no hay
  cambios locales, guardado o navegación en curso. Conserva selección, foco
  del editor y posición de desplazamiento. Si hay un borrador local, conserva
  también su revisión base para que siga funcionando la detección de conflictos.
- La asignación de IDs de bloque al cargar texto se completa antes de conectar
  el guardado automático. La normalización diferida no debe convertir una
  lectura remota en una escritura local.

La línea que faltaba en el diario existente se incorporó a sus bloques con un
PATCH condicionado por `base_revision`, sin cambiar el cuerpo ni los bloques
previos. Se comprobó antes que el editor estaba guardado y coincidía con esos
bloques. Tras refrescar el diario, AT-SPI confirmó la línea en el editor de la
ventana ya abierta. No se reinició el servidor ni se accedió a `ops/`.

## Verificación

- Escritorio, contenedor `lumen-qt-make:latest`, `--parallel 2`:
  `lumen-sync` **PASS** (`SYNC_TEST_OK`): nota y diario abiertos actualizados
  por sondeo, selección/foco conservados, ninguna escritura al cargar texto
  remoto y borrador local con su revisión base intacta.
  `lumen-find-replace` **PASS** (`LUMEN_FIND_REPLACE_OK`): regresión del editor,
  undo y guardado tras la modificación de la carga del documento.
  Ejecutable de uso local `.build/desktop-release/lumen` recompilado con el
  preset `release`: **PASS**. La sesión anterior se mantuvo abierta.
- Móvil: `:app:testDebugUnitTest :app:assembleDebug --max-workers=2`: **PASS**,
  7 pruebas (incluidas invalidación explícita, texto vacío e inmutabilidad).
- API: `go test ./... -run 'TestMobilePlainContent|TestPlainContentPatch|TestPropertiesReplacement' -count=1`:
  **PASS** contra almacenes temporales. Los nulls enviados por móvil eliminan
  la precedencia de los formatos antiguos sin perder las otras propiedades.
- APK actualizado mediante `adb install -r`: **Success**. Apertura con
  `am start -W`: **Status: ok**. Se conservan los datos de la app.

La planificación móvil conserva su intervalo periódico de 15 minutos y la
acción de sincronización manual. Esta corrección no añade sincronización en
tiempo real al escribir. La actualización del ejecutable de escritorio se
activa al volver a abrir la aplicación; la sesión abierta no se fuerza a cerrar.

La edición móvil posterior usa reconciliación de bloques nativos; sustituye la
invalidación completa del primer parche. Evidencia y límites en
[Editor móvil nativo](2026-11-01-mobile-editor/README.md).
