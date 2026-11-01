# Continuar Lumen

Punto de entrada para una persona o IA que retome el proyecto. Documentación
revisada contra el código de Lumen 0.6 el 30 de octubre de 2026.

## Qué construir

Un lugar personal para escribir, estudiar y relacionar conocimiento sin dedicar
la sesión a organizar carpetas. El recorrido principal es: abrir el diario,
escribir una frase, mencionar o crear un objeto con @ y continuar en esa misma
frase. La UI debe sentirse rápida, nativa, humana y operable con teclado.

La referencia es Capacities. El objetivo es reproducir sus buenas decisiones de
interacción dentro de Qt, conservando datos propios y un servidor
autoalojable. No hay equivalencia completa con Capacities ni una promesa de
funcionamiento offline.

## Índice

### Guías

| Documento | Para qué sirve |
|---|---|
| [RUNBOOK.md](guides/RUNBOOK.md) | Arrancar, compilar, probar, diagnosticar y detener servicios. |
| [QT-DESKTOP-WORKFLOW.md](guides/QT-DESKTOP-WORKFLOW.md) | Compilar Qt 6 Desktop con `lumen-qt-make`, revisar dependencias, pasar tests de editor y corregir UI sin sondas a ciegas. |
| [ANDROID-EMULATOR.md](guides/ANDROID-EMULATOR.md) | Emulador Android en Fedora 44: modos que fallan y modos que arrancan. |

### Diseño y arquitectura

| Documento | Para qué sirve |
|---|---|
| [DESIGN.md](design/DESIGN.md) | Lenguaje visual, estados, interacción, accesibilidad y revisión de UI. |
| [design-qa.md](design/design-qa.md) | Comparación visual de la entrega 0.3 y diferencias declaradas. |
| [ARCHITECTURE.md](architecture/ARCHITECTURE.md) | Módulos, flujo de datos e invariantes que no deben romperse. |
| [object-flow-v03.md](architecture/object-flow-v03.md) | Flujo de objetos, colecciones y vistas de la iteración v03. |
| [SYNC.md](../contracts/SYNC.md) | Contrato de objetos, formatos, relaciones, medios y sincronización. |

### Seguimiento

| Documento | Para qué sirve |
|---|---|
| [VERIFICATION.md](tracking/VERIFICATION.md) | Evidencia fechada, alcance y comprobaciones históricas. |
| [PENDIENTES.md](tracking/PENDIENTES.md) | Pendientes de OG/Reddit, extensión, cámara, QR y sync automático. |
| [INCIDENCIAS.md](tracking/INCIDENCIAS.md) | Problemas observados y cierres de versiones anteriores. |

### QA

- [Toggles editables y estado compartido](qa/2026-11-01-toggles/README.md).

| Documento | Para qué sirve |
|---|---|
| [Rendimiento y teclado móvil — 01-11-2026](qa/2026-11-01-mobile-performance.md) | Diagnóstico en Motorola, APK optimizado, caché de lecturas en RAM, ajuste IME y límites de la verificación. |
| [Vistas guardadas, tablas y columnas — 01-11-2026](qa/2026-11-01-mobile-views/README.md) | Consultas compartidas, edición estructural, compatibilidad Qt/Android y pruebas con datos ficticios. |
| [Objetos y esquemas móviles — 01-11-2026](qa/2026-11-01-mobile-objects/README.md) | Tipos reales, propiedades, revisión, filtros, referencias, imágenes mixtas y audio; comparación entre espacios. |
| [Navegación y enlaces móviles — 01-11-2026](qa/2026-11-01-mobile-navigation.md) | Historial de objetos, estado anterior, Share Sheet sin ventana y miniaturas locales; pruebas en emulador. |
| [Referencias, divisores e imágenes móviles — 01-11-2026](qa/2026-11-01-mobile-editor/README.md) | Editor con objetos pulsables, reglas horizontales, preview de imágenes y verificación en teléfono físico. |
| [Texto móvil oculto en escritorio — 01-11-2026](qa/2026-11-01-mobile-desktop-sync.md) | Diagnóstico real, invalidación de formatos, refresco del documento abierto y pruebas de protección del borrador. |
| [MOBILE-VISUAL-QA-2026-10-30.md](qa/2026-10-30-mobile-visual/MOBILE-VISUAL-QA-2026-10-30.md) | Revisión visual móvil contra Capacities: paleta, tipos, densidad y evidencia. |
| [capacities-audit-2026-10-29](design/DESIGN.md) | Auditoría de interacción, métricas y especificación nativa. |

### Producto y apps

| Documento | Para qué sirve |
|---|---|
| [README general](../README.md) | Funciones disponibles y uso del producto. |
| [Servidor](../server/README.md) | API, claves, despliegue y recuperación. |
| [Extensión](../extension/README.md) | Instalación, permisos y desarrollo del capturador. |
| [Lumen Mobile](../mobile/README.md) | App Android nativa, cifrado local, sync LAN/USB, Share Sheet y widgets. |
| [AGENTS.md](../AGENTS.md) | Reglas operativas para una IA o colaborador. |

### Archivo

| Documento | Para qué sirve |
|---|---|
| [LOGSEQ.md](archive/LOGSEQ.md) | Archivado: integración retirada; no describe la arquitectura actual. |
| [RESEARCH.md](archive/RESEARCH.md) | Investigación previa de referencias y tecnologías. |

Las capturas públicas están en [screenshots/](screenshots/) y usan datos
ficticios. Las referencias de terceros y el QA privado no se distribuyen: viven
en `${LUMEN_WORKSPACE}/research/`, fuera del repositorio.

## Estado que debe conservarse

Implementado: diario/calendario, editor enriquecido con bloques e IDs, @ y /
inline, objetos y propiedades tipadas, preview editable, pestañas, consultas y
seis representaciones, imágenes/PDF, grafo, captura OpenGraph, extensión y
cliente Android nativo (Kotlin/Compose) con SQLite cifrado, sync incremental
LAN/USB, diario visual, Share Sheet OG y widgets. La vista general usa recientes
de la sesión; Todo utiliza el evaluador compartido.

Límites: el cliente Android es local-first para texto, tareas y medios recientes,
pero no es E2EE: el servidor descifra el almacén en RAM y el índice FTS5 móvil es
derivado. No hay control completo de conflictos entre editores concurrentes; el
móvil conserva ambas versiones en el servidor, pero aún no ofrece un editor de
resolución lado a lado. En @, los enlaces web muestran el icono de tipo; el
capturador no obtiene automáticamente favicons individuales. Importar Markdown
conserva el texto, sin convertir su sintaxis a bloques enriquecidos.

La instalación personal y la API desechable son entornos distintos. Que Docker
esté healthy no demuestra que una escritura concreta haya llegado al almacén.
Consultar health, `pending` y estado del objeto; ver RUNBOOK.md.

## Cómo retomar una tarea

Leer la petición, localizar el módulo en ARCHITECTURE.md y revisar las guías que
le afectan. Describir el resultado esperado con un ejemplo verificable: “crear
Libro desde @ conserva la frase y enlaza su ID”, en vez de “mejorar UX”.

Si cambia un comportamiento conocido, actualizar su guía y verificación junto al
código. Si sólo cambia documentación, verificar rutas, comandos y configuración
sin volver a migrar datos o repetir toda la batería.

Las referencias privadas del usuario no se distribuyen. Los archivos de
`${LUMEN_WORKSPACE}/scratch/` de esta máquina son evidencia local, no una dependencia necesaria para
compilar o entender estas guías.
