# Emulador Android en Fedora 44

Actualizado el 30 de octubre de 2026. Entorno comprobado: Fedora 44, kernel
7.2.11-200.fc44.x86_64, SELinux Enforcing (`selinux-policy-targeted` 44.11-1),
Mesa 26.2.5, Android Emulator 37.2.13.0 (build 16740729), AVD `lumen-test` con
`system-images;android-36;google_apis;x86_64`.

Este documento registra qué modos de renderizado del emulador fallan en esta
máquina y cuáles arrancan. No describe un fallo de la app Android: la app se
instaló y ejecutó en el dispositivo físico mientras el emulador estaba caído.

Los comandos se ejecutan desde la raíz del proyecto (`outputs/lumen-pkm`),
salvo que se indique otra cosa.

## Síntoma

Al lanzar el emulador en headless, `qemu-system-x86_64-headless` termina con
`SIGSEGV` y systemd/DrKonqi muestra un coredump. El hilo que falla se llama
`RenderThread` y el stack superior está en
`lib64/gles_swiftshader/libGLESv2.so`.

El proceso aparece bajo el scope de OpenCode Desktop porque el emulador es un
proceso hijo. No es un fallo de OpenCode.

## Causa

SELinux deniega `execheap` al hilo `RenderThread`:

```text
avc: denied { execheap } for pid=1866509 comm="RenderThread"
     scontext=unconfined_u:unconfined_r:unconfined_t:s0
     tclass=process permissive=0
```

SwiftShader usa un JIT y necesita marcar memoria del heap como ejecutable. Al
denegarse esa operación, acaba llamando a un puntero inválido y el proceso hace
segfault. Las denegaciones coinciden con los crashes: 01:52:02 y 01:54:02 del
30-10-2026.

El flag que selecciona SwiftShader es `-gpu swiftshader_indirect`. El log del
emulador lo confirma:

```text
emuglConfig_init: vulkan_mode_selected:swiftshader gles_mode_selected:swiftshader
Graphics Adapter ... Google SwiftShader
```

`setroubleshoot` sugiere `setsebool -P selinuxuser_execheap 1`. Es la vía rápida
si hace falta SwiftShader, pero no la recomendada para este proyecto.

## Modos de renderizado

| Modo | Estado en esta máquina | Detalle |
|---|---|---|
| `-gpu swiftshader_indirect` | **Falla** | SIGSEGV en `libGLESv2.so`; SELinux deniega `execheap`. Es el flag que usó el agente. |
| `-gpu swiftshader` | **No usar** | Mismo backend SwiftShader. No se probó por separado. |
| `-gpu swangle` | **No usar** | ANGLE con SwiftShader Vulkan; mismo JIT. No se probó por separado. |
| `-gpu software` | **No usar** | Renderizador software por defecto; en esta versión acaba en SwiftShader. No se probó por separado. |
| `-gpu off` | **No fiable** | En 37.1.11 se reportó el mismo SIGSEGV; no se probó en 37.2.13. No usarlo como sustituto sin comprobarlo. |
| `auto` (sin flag) | **No fiable en headless** | El emulador puede elegir SwiftShader. Pasar el modo explícitamente. |
| `-gpu lavapipe` | **Funciona** | Mesa llvmpipe + ANGLE/SwANGLE. Arrancó en 22 s. Recomendado para CI y pruebas reproducibles. |
| `-gpu host` | **Funciona** | Intel Iris Xe del host. Arrancó en 18,5 s. Recomendado para trabajo interactivo. |

Los modos marcados «No usar» no se probaron uno a uno; se evitan porque
seleccionan SwiftShader o su JIT. La tabla distingue lo confirmado de lo no
probado.

El flag de línea de comandos tiene prioridad sobre `hw.gpu.mode` del AVD. No
depender de la configuración guardada del AVD para cambiar el renderizador.

## Comandos que funcionan

```bash
# Software: Mesa llvmpipe + ANGLE/SwANGLE
emulator -avd lumen-test -no-window -no-audio -no-boot-anim \
  -gpu lavapipe -no-snapshot

# Hardware: Intel Iris Xe
emulator -avd lumen-test -no-window -no-audio -no-boot-anim \
  -gpu host -no-snapshot
```

Evidencia de los logs:

```text
# lavapipe
emuglConfig_init: vulkan_mode_selected:lavapipe gles_mode_selected:swangle
Selecting Vulkan device: llvmpipe (LLVM 21.0.0, 256 bits), Version: 1.4.318
Graphics Adapter ... ANGLE (Mesa, Vulkan 1.4.318 (llvmpipe ...))
Boot completed in 18990 ms

# host
emuglConfig_init: vulkan_mode_selected:host gles_mode_selected:host
Selecting Vulkan device: Intel(R) Iris(R) Xe Graphics (TGL GT2), Version: 1.4.354
Graphics Adapter ... Mesa Intel(R) Iris(R) Xe Graphics (TGL GT2)
Boot completed in 18492 ms
```

`-gpu host` requiere acceso al GPU del host; en un servidor sin GPU o en CI,
usar `-gpu lavapipe`. `lavapipe` requiere el driver Vulkan de Mesa
(`mesa-vulkan-drivers` en Fedora).

## Si hace falta SwiftShader

La solución rápida que sugiere SELinux es:

```bash
sudo setsebool -P selinuxuser_execheap 1
```

Advertencia: permite que procesos de usuario no confinados marquen heap
ejecutable. Es un cambio global y debilita el endurecimiento de SELinux.
Preferir `-gpu lavapipe` o `-gpu host`. Para revertir:

```bash
sudo setsebool -P selinuxuser_execheap 0
```

Un módulo de política específico para el emulador es posible, pero no se
documenta aquí; el booleano es la vía que propone `setroubleshoot`.

## adb y dispositivo físico

Si el emulador muere y hay un teléfono conectado, `adb` puede seguir usando el
teléfono sin avisar. En esta máquina, tras el crash, la instalación y las
capturas se hicieron en el Motorola Edge 60 Fusion (`ZY22M4JJDL`), no en el
emulador.

Para evitar confundir destinos, elegir el serial explícitamente:

```bash
adb devices -l
adb -s emulator-5554 install -r mobile/app/build/outputs/apk/debug/app-debug.apk
adb -s ZY22M4JJDL install -r mobile/app/build/outputs/apk/debug/app-debug.apk
```

Para el emulador, el serial es `emulator-5554` (o el que asigne `adb devices`).
No usar `adb install` sin `-s` cuando hay más de un dispositivo.

## OpenCode

El agente lanzó el emulador con `-gpu swiftshader_indirect` y el crash quedó
registrado bajo el scope de OpenCode Desktop. Para que no vuelva a ocurrir,
indicar al agente que use `-gpu lavapipe` o `-gpu host`.

Resumen operativo:

```text
En Fedora 44, usar -gpu lavapipe (o -gpu host).
No usar -gpu swiftshader_indirect: SELinux bloquea execheap
y el emulador hace SIGSEGV en SwiftShader.
```

## Coredumps

El coredump de 163 MB queda en `/var/lib/systemd/coredump`. Para verlo:

```bash
coredumpctl list
coredumpctl info 1866509
```

Para borrarlo:

```bash
sudo coredumpctl rm 1866509
```

No copiar el core sin comprimir a `/tmp` si no se va a analizar: este caso
generó un archivo de 3,9 GB.

## Referencia externa

- Google Issue Tracker 566807521: emulador 37.x en Fedora con SELinux; Google
  recomienda `-gpu lavapipe`:
  <https://issuetracker.google.com/issues/566807521>
