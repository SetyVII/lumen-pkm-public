# Editor móvil nativo — 01-11-2026

Esta revisión sustituye el primer arreglo parcial que convertía una imagen en
un enlace y la mostraba debajo del cuerpo. Las capturas pertenecen a datos
ficticios salvo las comprobaciones privadas conservadas en `scratch/`.

## Comportamiento

- Lee `properties.blocks`; dibuja las imágenes en el bloque correspondiente,
  incluso si falta su ID en `links`. No duplica esas imágenes en «Momentos».
- Viñetas/sangría, encabezados y estilos de runs se representan en el campo
  común. El divisor se dibuja como una línea; `---` dentro de código es texto.
- Editar una viñeta, partir un párrafo o continuar una lista conserva los
  bloques vecinos, las dos imágenes, IDs, dimensiones y formato de runs.
  Se invalida el HTML anterior y se conserva el lenguaje del código.
- Las referencias usan IDs y la selección ya no provoca escrituras.

## Evidencia

- Pruebas unitarias Android: reconciliación de bloques, referencias/divisores,
  offsets y conservación de formato. `NativeDocumentTest` cubre cinco casos.
- `EditorRenderingTest` ejecutado por ADB en Motorola Edge 60 Fusion físico:
  **PASS**, `OK (1 test)`. Usa bloques e imágenes ficticios en RAM/cache;
  comprueba que una edición conserva negrita, imágenes y bloques vecinos.
  [Captura del editor nativo](native-editor-rendering-phone.png).
- Diario real del 1 de noviembre, tras desbloqueo del usuario: ambas viñetas
  visibles; primera captura entre las viñetas y segunda captura inmediatamente
  después de «Imagen de pruba». Sin escrituras de fixtures en la biblioteca.
- `lumen-editor-blocks`: **PASS**, `EDITOR_BLOCKS_TEST_OK`. Lee los dos fixtures
  compartidos y conserva referencias, divisor, viñeta, negrita, encabezado,
  lenguaje Kotlin y ambas imágenes al pasar por Qt.
- API Go, `TestMobileNativeEditorBlocksRoundTripWithoutLosingFormatting` y
  pruebas `TestMobilePlainContent*`: **PASS** en almacenes temporales. El cuerpo
  y el JSON completo de bloques sobreviven a push/pull sin pérdida.

## Límites

No se afirma paridad completa con Qt: tablas/grupos/columnas tienen una
representación básica y la matemática conserva el código fuente. Los metadatos
de bloques no editados se conservan. La revisión posterior verifica la edición de
texto antes/después de una imagen en el mismo bloque; véase
[QA de objetos](../2026-11-01-mobile-objects/README.md).

La navegación y el Share Sheet posteriores se verifican por separado en
[QA de navegación y enlaces](../2026-11-01-mobile-navigation.md).
