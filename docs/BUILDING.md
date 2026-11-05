# Building Lumen

For branch, review and cross-app contribution workflow, see
[CONTRIBUTING.md](../CONTRIBUTING.md).

Lumen uses a native C++20/Qt Widgets desktop, a Go server and an Android
Kotlin/Compose client. No embedded browser is required by the editor.

## Server

From the repository root:

```sh
(cd server && go test ./... && go vet ./...)
docker build -f server/Dockerfile -t lumen-pkm:local .
```

The Docker build includes the shared contract fixtures and race-detector tests.
See [server setup](../server/README.md) for local credentials and startup.

## Linux desktop

Install a C++20 toolchain, CMake, Ninja and Qt Widgets, Network, SVG, PDF,
PDFWidgets and Test development libraries. The current build is tested with
Qt 6.4.2. From the repository root:

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2 --target lumen lumen-document lumen-table-controls lumen-editor-blocks lumen-editor-media lumen-code-math lumen-api-protocol
QT_QPA_PLATFORM=offscreen ctest --test-dir .build/desktop-test --output-on-failure -R '^(document|table-controls|editor-blocks|editor-media|code-math|api-protocol)$'
```

Use the `release` preset for a release build. Set `LUMEN_API_URL` and
`LUMEN_TOKEN` when launching; obtain the token from your own server. Do not
commit local configuration, credentials, libraries or build output.

## Android and extension

See [Android](../mobile/README.md). For the browser extension use `pnpm install`
and `pnpm test` in `extension/`; load it as an unpacked extension.

## Current limits

The metadata catalog is available, but desktop list caching still needs migration
to it. Long-session undo memory, complete keyboard hierarchy and reactive block
transclusion need further work. Component checks do not certify every Linux
compositor, screen scale or memory-constrained machine.
