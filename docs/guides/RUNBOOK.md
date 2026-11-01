# Arranque, desarrollo y pruebas

> Para el detalle completo sobre cómo compilar Qt 6 Desktop en esta máquina (`lumen-qt-make:latest`), comprobar bibliotecas dinámicas en el host (`ldd -r`), ejecutar tests individuales del editor en segundos y corregir la UI/editor sin crear sondas innecesarias, lee primero **[QT-DESKTOP-WORKFLOW.md](QT-DESKTOP-WORKFLOW.md)**.

Todos los comandos parten de la raíz del código fuente (`${LUMEN_WORKSPACE}/lumen-pkm`):

~~~bash
cd ${LUMEN_WORKSPACE}/lumen-pkm
~~~

Se necesita Docker Engine o Docker Desktop disponible para el servidor. La aplicación gráfica de Docker y la terminal no necesitan permanecer visibles; el motor y el contenedor sí deben funcionar. Lumen 0.3 no tiene cliente offline.

## Dos entornos separados

| | Uso diario | Pruebas |
|---|---|---|
| Compose | `${LUMEN_WORKSPACE}/ops/personal/compose.yaml` | `sandbox/compose.test.yaml` |
| Proyecto Docker | lumen-pkm | lumen-pkm-test |
| API | http://127.0.0.1:8765 | http://127.0.0.1:8879 |
| Imagen | lumen-pkm:local | lumen-pkm:test |
| Volumen | lumen-pkm_lumen_data | lumen-pkm-test_lumen_test_data |
| Claves | `${LUMEN_WORKSPACE}/ops/personal/secrets` | Directorio externo LUMEN_TEST_SECRETS_DIR |
| Grafo | lumen | lumen-test |

No basta cambiar el nombre del proyecto con -p: el Compose de uso diario seguiría publicando 8765 y usando sus archivos de claves. Utilizar el archivo separado de pruebas.

## Abrir la instalación existente

~~~bash
docker info
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml up -d
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml ps
curl --fail --silent --show-error http://127.0.0.1:8765/health
tools/scripts/run-desktop.sh
~~~

El wrapper lee `${LUMEN_WORKSPACE}/ops/personal/secrets/client.token` y ejecuta `.build/desktop-release/lumen` (o `${LUMEN_WORKSPACE}/releases/0.6/lumen`). No escribir el valor del token en el comando. En la máquina donde se instaló el acceso de usuario también se puede abrir Lumen desde el menú o con el comando lumen; ese acceso apunta al wrapper de esta carpeta. Ese comando de usuario no es el lanzador de pruebas.

La primera inicialización descifra los objetos para construir el índice en RAM. Esperar a `status=ready`, `source_of_truth=lumen-store` y `pending=0`. Un error de descifrado o de integridad requiere diagnóstico con `-fsck`; no regenerar claves.

restart: unless-stopped permite recuperar el contenedor cuando vuelve el motor Docker, salvo que se haya detenido expresamente. Activar Docker al iniciar sesión depende de la instalación del sistema; no ejecutar sudo o cambiar servicios de la máquina como parte de una prueba de UI.

## Instalación nueva

Este bloque es sólo para una instalación sin datos ni claves previas:

~~~bash
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml build
bash ../../tools/scripts/bootstrap-secrets.sh
docker compose -f ${LUMEN_WORKSPACE}/ops/personal/compose.yaml up -d
~~~

El bootstrap se niega a reemplazar claves existentes. Si falla por permisos, consultar ../../server/README.md; no borrar secrets para intentar arreglarlo. El puerto de uso diario permanece ligado a loopback. Un acceso remoto necesita transporte seguro y configuración propia, no publicar 8765 en todas las interfaces como atajo.

## Preparar un servidor desechable

Elegir una carpeta de trabajo externa al repo y exportar su ruta absoluta. Por convención, `scratch/<AAAA-MM-DD>-<tarea>/` del workspace:

~~~bash
mkdir -p ../../../scratch/2026-10-30-dev/lumen-dev-test
export LUMEN_TEST_WORK="$(realpath ../../../scratch/2026-10-30-dev/lumen-dev-test)"
export LUMEN_TEST_SECRETS_DIR="$LUMEN_TEST_WORK/secrets"
~~~

Para probar exactamente el backend ya instalado, crear una etiqueta de prueba sin modificar la etiqueta de uso diario:

~~~bash
docker image tag lumen-pkm:local lumen-pkm:test
~~~

Si se cambió código del servidor, en su lugar compilar la imagen de pruebas:

~~~bash
docker compose -p lumen-pkm-test -f ../../sandbox/compose.test.yaml build
~~~

Provisionar las claves una sola vez para este entorno:

~~~bash
LUMEN_IMAGE=lumen-pkm:test bash ../../tools/scripts/bootstrap-secrets.sh "$LUMEN_TEST_SECRETS_DIR"
docker compose -p lumen-pkm-test -f ../../sandbox/compose.test.yaml config --quiet
docker compose -p lumen-pkm-test -f ../../sandbox/compose.test.yaml up -d
docker compose -p lumen-pkm-test -f ../../sandbox/compose.test.yaml ps
curl --fail --silent --show-error http://127.0.0.1:8879/health
~~~

Si las claves de prueba ya existen, omitir el bootstrap y reutilizarlas con su volumen. Si el puerto está ocupado, identificar el servicio; no detener un contenedor desconocido. Las pruebas pueden crear objetos, fechas futuras, tipos y adjuntos. Los tipos de fixture pueden permanecer en este entorno porque la API no ofrece borrado de tipos.

## Compilar el escritorio sin reemplazar el instalado

En esta máquina el host tiene las bibliotecas dinámicas de ejecución (`/lib64/libQt6*.so.6`), pero el compilador C++20, CMake, Ninja y las cabeceras de Qt 6 viven en la imagen local `lumen-qt-make:latest`. Nunca crear carpetas `desktop/build-*`; compilar siempre en `.build/desktop-test` o `.build/desktop-release` con `--parallel 2`:

~~~bash
docker run --rm -u "$(id -u):$(id -g)" \
  -v "${LUMEN_WORKSPACE}:/work" -w /work/lumen-pkm \
  lumen-qt-make:latest bash -lc '
    cmake --preset test -S desktop &&
    cmake --build .build/desktop-test --parallel 2 &&
    ctest --preset test --test-dir .build/desktop-test
  '
~~~

Para compilar y probar rápidamente solo los tests del editor (sin recompilar toda la batería):

~~~bash
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
~~~

Para compilar `.build/desktop-release/lumen`:

~~~bash
tools/scripts/build-desktop.sh
~~~

Ver [QT-DESKTOP-WORKFLOW.md](QT-DESKTOP-WORKFLOW.md) para el mapa completo de targets, invariantes del editor y verificación de bibliotecas con `ldd -r`.

## Android sin Android Studio

El SDK de línea de comandos y el wrapper de Gradle bastan. En esta máquina:

```bash
export ANDROID_HOME="$HOME/.local/share/android-sdk"
export JAVA_HOME=/usr/lib/jvm/java-25-openjdk
cd mobile
./gradlew :app:testDebugUnitTest
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Para USB: `../../tools/scripts/usb-sync.sh` y empareja con `http://127.0.0.1:8765`.
Para Wi-Fi: arranca el servidor con `LUMEN_LISTEN=0.0.0.0:8765 LUMEN_MDNS=1` y
usa `../../tools/scripts/pairing-qr.sh` (con `LUMEN_TLS_CERT` si hay TLS). No instalar el
APK de pruebas en una instalación personal sin confirmarlo; usa un servidor de
pruebas en otro puerto y `adb reverse tcp:8879 tcp:8879`.

## Aceptación integrada

Requiere el servidor desechable ready y el binario de tests recién compilado. Este subshell evita dejar token, API y preferencias de prueba en la sesión normal:

~~~bash
(
  set +x
  export LUMEN_API_URL=http://127.0.0.1:8879
  export LUMEN_ACCEPTANCE_DISPOSABLE=1
  export LUMEN_TOKEN
  LUMEN_TOKEN=$(cat "$LUMEN_TEST_SECRETS_DIR/client.token")
  export XDG_CONFIG_HOME="$LUMEN_TEST_WORK/config"
  export XDG_CACHE_HOME="$LUMEN_TEST_WORK/cache"
  export LUMEN_ACCEPTANCE_SCREENSHOTS="$LUMEN_TEST_WORK/captures"
  export QT_QPA_PLATFORM=offscreen
  ./desktop/build-dev/lumen-acceptance
)
~~~

El test rechaza 8765 y exige el flag de entorno desechable. La guardia es adicional: comprobar igualmente URL, volumen y claves. El resultado esperado es LUMEN_ACCEPTANCE_OK y salida0. lumen-integration es un alias del mismo recorrido, no una segunda evidencia independiente.

Se prueban navegación/guardado, @ real, creación y undo, preview/foco, Ctrl+clic, tipo vacío, importación, consultas, propiedades, Kanban y medios autenticados. El test restaura diarios y elimina objetos de fixture, pero no deja necesariamente el volumen completamente vacío.

Para abrir el binario de desarrollo de forma visible, repetir el subshell anterior sin QT_QPA_PLATFORM=offscreen y ejecutar ../../.build/desktop-dev/lumen. No usar run-desktop.sh esperando que ejecute el build de desarrollo: ese wrapper abre `.build/desktop-release/lumen` o `releases/0.6/lumen`.

## Capturas y smoke

Siempre contra el servidor desechable y con preferencias aisladas del subshell anterior:

~~~bash
(
  set +x
  export LUMEN_API_URL=http://127.0.0.1:8879
  export LUMEN_TOKEN
  LUMEN_TOKEN=$(cat "$LUMEN_TEST_SECRETS_DIR/client.token")
  export XDG_CONFIG_HOME="$LUMEN_TEST_WORK/config"
  export XDG_CACHE_HOME="$LUMEN_TEST_WORK/cache"
  export QT_QPA_PLATFORM=offscreen
  ./desktop/build-dev/lumen --smoke --size=1448x650
  ./desktop/build-dev/lumen --size=1448x650 --view=gallery --screenshot "$LUMEN_TEST_WORK/gallery.png" --exit-after-screenshot
)
~~~

--seed-demo crea datos; sólo se permite en un entorno desechable. --smoke carga el cliente y sale: no sustituye aceptación, validación del almacén ni prueba visual de Wayland. Esperar a que el proceso termine y comprobar su código de salida.

Para comparar UI, ver DESIGN.md. Capturas de la biblioteca personal, referencias del navegador y tokens quedan fuera de screenshots distribuibles.

## Backend y motor real

Con Go compatible con server/go.mod y CGO disponibles:

~~~bash
(
  cd server
  go test -race ./...
  go vet ./...
)
~~~

La construcción de la imagen también ejecuta tests del backend. Las pruebas normales usan sus propios datos temporales. Para verificar una copia del almacén sin arrancar el servidor:

~~~bash
(
  cd server
  LUMEN_KEY_FILE=/ruta/encryption.key LUMEN_DATA_DIR=/ruta/datos \
    go run . -fsck
)
~~~

`-fsck` descifra cada registro, comprueba IDs, enlaces, assets/blobs y que el índice en memoria coincide con el disco. No escribe datos.

## Extensión

~~~bash
(
  cd extension
  pnpm install
  pnpm test
  pnpm build
  LUMEN_TEST_API=http://127.0.0.1:8879 LUMEN_TEST_TOKEN_FILE="$LUMEN_TEST_SECRETS_DIR/client.token" pnpm test:server
)
~~~

El harness usa HTTP real y APIs de extensión simuladas; crea y elimina datos. Su API predeterminada es la de uso diario, así que pasar siempre LUMEN_TEST_API explícitamente. No prueba los permisos, el ID, la instalación o el CORS de un navegador real. Esos pasos se hacen por separado siguiendo extension/README.md y permitiendo el origen exacto, nunca *.

## Diagnóstico

| Síntoma | Comprobación y siguiente paso |
|---|---|
| No conecta | Docker info, compose ps, /health, URL y proceso. No culpar al editor antes de comprobar el servicio. |
| 401 | Archivo de client.token correcto para ese servidor. No mostrarlo en logs o pegarlo en una URL. |
| Guardado | `/health` con `status=ready`, `source_of_truth=lumen-store` y `pending=0`; si hay error, ejecutar `-fsck` sobre una copia. |
| Se ve texto anterior | ID de origen y representaciones content/blocks/rich_text; conservar la evidencia antes de modificar datos. |
| Falla el arranque gráfico | Dependencias Qt y plugin de plataforma; probar componentes offscreen por separado. Offscreen funcionando no acredita Wayland. |
| Preview OpenGraph incompleto | Respuesta y políticas del sitio; puede requerir autenticación o impedir extracción. No prometer soporte universal. |
| Tipo vacío inesperado | Revisar ámbito, filtros, búsqueda y definición de vista antes de concluir que faltan objetos. |

~~~bash
docker compose -p lumen-pkm-test -f ../../sandbox/compose.test.yaml logs --tail=100 lumen
curl --fail --silent --show-error http://127.0.0.1:8879/health
~~~

Los logs pueden contener rutas, URLs o errores privados. Revisarlos localmente y redactarlos antes de compartirlos; no volcar entornos completos o archivos de secretos.

## Cerrar, actualizar y recuperar

Para detener pruebas conservando su volumen:

~~~bash
docker compose -p lumen-pkm-test -f ../../sandbox/compose.test.yaml down
~~~

No añadir -v por rutina. Si se decide borrar un entorno desechable, verificar primero sus nombres de proyecto/volumen y que sus claves no pertenezcan a la instalación personal. No mezclar una clave nueva con datos cifrados antiguos.

Para instalar un binario de desarrollo ya probado sin interrumpir el proceso abierto:

~~~bash
install -m 755 .build/desktop-release/lumen ${LUMEN_WORKSPACE}/releases/0.6/lumen.new
mv ${LUMEN_WORKSPACE}/releases/0.6/lumen.new ${LUMEN_WORKSPACE}/releases/0.6/lumen
~~~

La ventana existente sigue usando el ejecutable anterior hasta que el usuario la cierre. tools/scripts/build-desktop.sh compila en `.build/desktop-release/`; con `--install` publica en `${LUMEN_WORKSPACE}/releases/0.6/lumen` usando un archivo temporal y `mv`, nunca sobre el ELF abierto.

Antes de actualizar el backend de uso diario, hacer una copia consistente del volumen completo /data y de las claves, con el servicio parado o mediante un procedimiento de snapshot consistente. Incluye /data/lumen; copiar sólo el SQLite abierto o sólo las notas no basta. Coordinar la interrupción con el usuario, registrar imagen/configuración y conservar la copia fuera del proyecto y del paquete.

Restaurar primero en un entorno aislado, con la misma clave y versiones compatibles; verificar objetos, adjuntos, motor y cola antes de sustituir la instalación personal. No improvisar una restauración sobre su volumen vivo.

## Qué registrar al entregar

Fecha, versión, toolchain y comandos realmente ejecutados; resultados y alcance por capa; API/volumen de pruebas; defectos encontrados y corregidos; capturas comparables y diferencias restantes. Si cambia el binario, registrar su SHA256 en BUILD-INFO.json.

Ver ../tracking/VERIFICATION.md para la evidencia existente. No convertir una lectura de documentación o un config --quiet en una prueba de extremo a extremo.
