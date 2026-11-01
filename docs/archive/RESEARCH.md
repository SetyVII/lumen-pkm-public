# Qué reutilizar antes de construir

Investigación del 9 de octubre de 2026. No encontrar un proyecto no demuestra que no exista; este documento registra los candidatos examinados.

| Proyecto | Reutilización posible | Distancia respecto al objetivo |
|---|---|---|
| [Logseq DB](https://github.com/logseq/logseq) | Grafo, nodos, clases, propiedades, referencias y CLI oficial headless | Cliente desktop Electron; el MVP necesita otro cliente |
| [logseq-selfhost](https://github.com/yshalsager/logseq-selfhost) | Despliegue de sincronización y servicios con Docker | No aporta una UI nativa tipo Capacities |
| [logseq-selfhost-mcp](https://github.com/brynmrgn/logseq-selfhost-mcp) | Referencia para bridge de servicios | No aporta un cliente Qt/GTK; el MVP no incorpora IA |
| [logseq-clipper](https://github.com/j3sch/logseq-clipper) | Precedente de captura por HTTP | Requiere adaptar autenticación, permisos y modelo DB |
| [logseq-clipper, clombion](https://github.com/clombion/logseq-clipper) | Otro precedente de clipper | No resuelve cliente nativo ni cifrado completo |
| [Anytype desktop](https://github.com/anyproto/anytype-ts) | Modelo de objetos e integración API | Electron y licencia source available |
| [Trilium](https://github.com/TriliumNext/Trilium) | ETAPI y selfhosting | Modelo árbol/atributos, no el flujo de objetos solicitado |

No se encontró entre estos candidatos un cliente C++/Qt o Rust/GTK para Logseq DB que ya reproduzca el flujo solicitado y reúna todas las restricciones. Se reutiliza el motor Logseq; no se implementa su esquema SQLite a mano.

## Referencia visual y funcional

Las capturas oficiales de [Capacities Product](https://capacities.io/product) y su [documentación de diario](https://docs.capacities.io/reference/dates-and-daily-notes) sirven para observar sidebar de tipos, nota central, panel contextual, calendario, creación rápida y objetos vinculados. Las imágenes de referencia se conservan en `${LUMEN_WORKSPACE}/research/capacities/desktop/` y no se distribuyen como recursos de la aplicación.

El MVP usa nombre e identidad propios: Lumen. La tipografía Inter es libre (OFL, licencia adjunta). No se descargan fuentes comerciales sin licencia.

## Decisión técnica

- Cliente: C++20 + Qt 6 Widgets, sin Electron ni WebView. Qt PDF para visualizar archivos en memoria. Pintado propio de tarjetas y grafo, tema y transiciones pequeñas. Qt Quick/QML es una opción futura para skins más elaborados; esta versión no afirma usar QML.
- Servidor: Go, HTTP API, SQLite y AES-GCM para el almacenamiento propio.
- Motor: CLI oficial de Logseq, runtime Node separado. Esa dependencia es necesaria al reutilizar el motor actual; no convierte el cliente en Electron.
- Extensión: JavaScript vanilla MV3, sin TypeScript ni framework. Los navegadores requieren código JavaScript para la captura.

No se puede inferir que C++ o Go garanticen fluidez: se verifica el comportamiento del MVP y se documenta el entorno concreto.

## Comprobación adicional en Reddit

Consultada el **9 de octubre de 2026** mediante búsquedas específicas en r/PKMS, r/logseq y referencias cruzadas. No apareció un cliente Qt/GTK orientado a objetos que ya implementara el conjunto solicitado sobre Logseq DB. Esto describe el alcance de la búsqueda, no una prueba de exclusividad.

- [r/logseq: Alternative, 14 de septiembre de 2026](https://www.reddit.com/r/logseq/comments/1wgd8vb/alternative/) y [Tine, 3 meses antes de la consulta](https://www.reddit.com/r/logseq/comments/1uetnyn/tine_dropin_faster_logseq_og_replacement/) sí identifican un precedente importante sin Electron: **Tine**. La [fuente primaria](https://github.com/martinkoutecky/tine/blob/master/README.md) y su [Cargo.toml](https://github.com/martinkoutecky/tine/blob/master/src-tauri/Cargo.toml) confirman núcleo Rust, shell Tauri/WebKitGTK e interfaz SolidJS. Su licencia es AGPL-3.0-only. Opera sobre los grafos Markdown clásicos de Logseq; no proporciona este frontend sobre Logseq DB ni componentes Qt/GTK de escritorio. Android usa Tauri, no Jetpack Compose. No cumple las restricciones de interfaz sin WebView y código sin TypeScript.
- [r/PKMS: Your PKM top choice, 7 de enero de 2025](https://www.reddit.com/r/PKMS/comments/1hw5dr5/your_pkm_top_choice_why/) y [Looking for a new PKMS, enero de 2026](https://www.reddit.com/r/PKMS/comments/1pzjsbe/looking_for_a_new_pkms/) ayudan a localizar herramientas y preferencias, pero sus recomendaciones no verifican arquitectura, licencia ni compatibilidad DB. No se toman sus etiquetas «native» u «open source» como prueba técnica.
- La búsqueda relacionada encontró [Agaric](https://github.com/jfolcini/agaric), GPL-3.0: diario y bloques, Rust con una interfaz React/Tauri y almacenamiento SQLite propio. Puede importar contenido Logseq, pero no es un cliente de su motor DB ni usa widgets Qt/GTK. No se reutiliza su esquema como si fuera Logseq.

El hallazgo de Tine corrige cualquier afirmación demasiado amplia de que no hay alternativas sin Electron. Lo que no se encontró fue una aplicación existente que cumpla **a la vez** el modelo Capacities, Logseq DB como motor, interfaz nativa sin WebView y el despliegue privado solicitado.
