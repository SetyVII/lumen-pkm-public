#pragma once

#include <QColor>
#include <QImage>
#include <QString>

// A compact TeX-subset layout engine drawn with QPainter. Everything happens in
// memory: no TeX installation, no helper process, no temporary files.
//
// Supported: fractions (\frac \dfrac \tfrac \binom), roots (\sqrt[n]{}), super/
// subscripts, Greek letters, relations/operators, \sum \prod \int \lim with
// limits, \left \right delimiters, matrices (matrix/pmatrix/bmatrix/vmatrix/
// cases), accents (\hat \bar \vec \tilde \dot \overline \underline), \text,
// \mathrm \mathbf \mathbb, spacing commands and common function names.
namespace LumenMath {
struct Result {
    QImage image;               // devicePixelRatio == scale
    qreal width = 0, height = 0; // logical size in pixels
    bool ok = false;            // false when the source had unknown commands/unbalanced braces
    QString error;
};
Result render(const QString &latex, qreal pixelSize, const QColor &color, bool display, qreal scale = 2.0);
}
