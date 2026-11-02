#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>
namespace LumenClipboard {
inline constexpr auto Mime = "application/vnd.lumen.document+json";
QJsonObject frame(const QJsonObject &document, const QString &sourceObjectID);
// Copy always allocates fresh IDs, including table definitions. Internal
// compound references are retargeted only when the source owner is explicit.
bool prepare(const QByteArray &bytes, const QString &targetObjectID, QJsonObject *document,
             QString *error = nullptr, const QJsonObject &newIDs = {});
bool validate(const QJsonObject &document, QString *error = nullptr);
} // namespace LumenClipboard
