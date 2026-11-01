# sync/

- `lumen_api.hpp` — cliente HTTP autenticado (`Api`), caché de assets y subida
  multipart.
- `main_window_sync.cpp` — carga inicial, guardado, estado `pending`, refresco
  periódico y reconciliación de referencias.

No crear aquí un segundo cliente: la app Android (`mobile/`) y la extensión
comparten el contrato de `../../contracts/SYNC.md`.
