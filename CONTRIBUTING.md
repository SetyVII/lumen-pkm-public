# Contribuir a Lumen

Esta guía se limita al desarrollo del código y del diseño. Las instalaciones personales, los despliegues, las copias de seguridad y las credenciales quedan fuera de este flujo.

## Antes de empezar

1. Lee [docs/BUILDING.md](docs/BUILDING.md) y [docs/PROTOCOL.md](docs/PROTOCOL.md).
2. Revisa [contracts/SYNC.md](contracts/SYNC.md) si el cambio toca objetos, API o sincronización.
3. Lee el README y la guía de la app afectada: `desktop/`, `server/`, `mobile/` o `extension/`.
4. Para interfaz móvil, consulta [la referencia de diseño](docs/figma/README.md).

## Rama y espacio de trabajo

Parte de `main` y crea una rama corta para una tarea concreta. No desarrolles directamente sobre `main` ni compartas una misma carpeta de trabajo entre cambios paralelos.

```bash
git switch main
git pull --ff-only
git switch -c feat/descripcion-breve
```

Para trabajar en paralelo desde un mismo clon, cada persona puede usar un worktree y una rama propios:

```bash
git worktree add ../lumen-descripcion -b feat/descripcion origin/main
```

Coordina antes los cambios que cruzan varias apps o modifican contratos. Mantén cada PR acotado y evita mezclar refactors no relacionados con una función.

## Contratos, código y diseño

- `contracts/` define lo que comparten escritorio, servidor, Android y extensión. Si cambia el formato o el comportamiento compartido, actualiza `contracts/SYNC.md` y los fixtures afectados en el mismo PR.
- No elimines campos, estados ni operaciones persistidas sin explicar compatibilidad, migración y comportamiento ante clientes antiguos.
- El archivo móvil de `docs/figma/` es un artefacto binario: GitHub no puede mostrar un diff útil ni combinar ediciones simultáneas. Si lo actualizas, hazlo en una sola rama y adjunta capturas exportadas al PR. No reemplaces ese archivo para un cambio de código que no altere el diseño.
- Una captura ayuda a revisar la interfaz, pero no sustituye la descripción del recorrido, el estado y el tamaño de pantalla afectados.

## Comprobaciones y pull request

Ejecuta las comprobaciones existentes que correspondan al área modificada; los comandos están en [docs/BUILDING.md](docs/BUILDING.md) y en el README de cada app. En el PR, anota los comandos ejecutados y su resultado real. Si no pudiste ejecutarlos, indícalo sin presentar el cambio como verificado.

Abre un pull request hacia `main` y completa la plantilla. Incluye:

- objetivo y alcance, con el issue o tarea relacionada;
- apps y contratos afectados;
- comandos ejecutados y resultado;
- capturas antes/después para cambios de interfaz;
- notas de compatibilidad o migración cuando apliquen.

Pide revisión a otra persona antes de integrar. Al habilitar colaboradores, configura protección de `main` para exigir una aprobación y las comprobaciones de CI que existan; esta guía por sí sola no impone esa regla en GitHub.

## Datos y límites del repositorio

Usa datos ficticios y el servidor desechable de `sandbox/compose.test.yaml` para desarrollo que escriba datos. No añadas tokens, claves, bibliotecas personales, archivos de `ops/`, backups ni datos reales a un commit o a una captura. Mantén `pnpm` para los comandos JavaScript de la extensión.
