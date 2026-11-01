# Fixtures compartidos

Datos ficticios que usan los tests de las cuatro apps. No contienen datos
personales ni secretos.

| Archivo | Contenido |
|---|---|
| `ids.json` | Identificadores estables (diario, libro, persona, proyecto, idea, enlace) para el servidor desechable del puerto 8879. |
| `mobile-object-schemas.json` | Libro heredado, libro con autores relacionados, género, intervalo y definición; API y compatibilidad móvil. |
| `native-editor.json` | Viñeta con negrita, dos imágenes en su posición, encabezado, cursiva y código; roundtrip de API y editor Qt. |
| `mobile-editor.json` | Referencia por ID y divisor nativo escritos por el editor móvil; lectura por el editor Qt sin mostrar sintaxis ni U+FFFC. |

Reglas:

- Cualquier cambio en el formato de objeto, propiedad o relación debe
  actualizar `../SYNC.md` y estos fixtures a la vez.
- Los fixtures se cargan solo contra el Compose de `../../sandbox/`, nunca
  contra la instalación personal de `ops/`.

- `backend-power.json`: block references, media kinds, extended recurrence and habit tracking (2026-11-01).

- `saved-views.json`: shared desktop/Android LumenQuery cases: nested AND/OR,
  aliases, links/backlinks, numeric and boolean coercion, manual order, sort and
  limit. Consumed by `desktop/tests/views.cpp` and `LumenQueryTest`.
- `native-structures.json`: tables, styled reference cells, columns and a nested
  image with stable IDs. Consumed by Qt editor and Android structured-block tests.

- `toggles.json`: shared expanded/collapsed toggle titles, nested editable bodies and a following sibling. Used by Qt and backend sync tests.
