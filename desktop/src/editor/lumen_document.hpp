#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

// Canonical document wire format. The presentation array is private to Qt and
// must never be persisted; metadata travels with Qt formats through undo/redo.
namespace LumenDocument {
QString temporalText(const QJsonObject &attributes);
QJsonObject empty();
QJsonArray toPresentation(const QJsonObject &document);
QJsonObject fromPresentation(const QJsonArray &blocks);
QJsonObject fromText(const QString &text);
QString plainText(const QJsonObject &document);
QJsonArray objectReferences(const QJsonObject &document);
} // namespace LumenDocument
namespace LumenDocument {
// Applies a batch to an independent value, returns input unchanged on failure.
bool applyCommands(const QJsonObject &input, const QJsonArray &commands, QJsonObject *result,
                   QString *error = nullptr);
} // namespace LumenDocument
