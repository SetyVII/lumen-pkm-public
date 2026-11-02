#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

namespace LumenCalculations {

// Kind of a field for calculation purposes: "number", "date", "boolean" or
// "text" (the fallback for select/tags/relation/url/everything else).
// `types` holds the property definitions; `objects` is only consulted to
// infer the kind when no type defines the property.
QString kindOf(const QJsonArray &types, const QJsonArray &objects, const QString &field);

QList<QPair<QString, QString>> opsForKind(const QString &kind);

// Verbose menu label of an op for a kind ("Range (days)" for dates); falls
// back to the op id when the pair is not valid for the kind.
QString menuLabel(const QString &kind, const QString &op);

// Compact summary label used in strips and tooltips ("Sum", "Checked"...).
QString opLabel(const QString &op);

struct Result {
    // false when there is nothing to summarize (e.g. Sum over a property no
    // shown object has): the segment is omitted instead of showing noise.
    bool valid = false;
    QString text;    // rendered value: "878", "146.33", "4/6", "67%", "2026-01-05"
    QString summary; // "Sum (pages): 878" — strips, menu entries, tooltips
};

// Evaluates one {property, op} calculation over `objects` (the subset being
// summarized — the whole result or one group), resolving relation/backlink
// fields against `universe` (the full data set, like the rest of the view).
Result compute(const QJsonObject &calculation, const QJsonArray &objects,
               const QJsonArray &universe, const QJsonArray &types);
Result compute(const QString &op, const QString &field, const QJsonArray &objects,
               const QJsonArray &universe, const QJsonArray &types);

// All valid results joined for a strip / group line:
// "Sum (pages): 878 · Checked (read): 4/6". Empty when nothing renders.
QString strip(const QJsonArray &calculations, const QJsonArray &objects,
              const QJsonArray &universe, const QJsonArray &types);

// True when at least one calculation of the list renders for `objects`.
bool anyValid(const QJsonArray &calculations, const QJsonArray &objects,
              const QJsonArray &universe, const QJsonArray &types);

} // namespace LumenCalculations
