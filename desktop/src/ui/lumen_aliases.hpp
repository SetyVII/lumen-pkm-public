#pragma once

// Client mirror of the server's aliases matcher (contracts/SYNC.md
// "Aliases", docs/implementation/backend/aliases.md). The desktop keeps the
// whole workspace in RAM and filters locally in three places (command
// palette fallback, the editor @-linking dropdown, LumenQuery view search),
// so this header is the ONE implementation of the rule; it must stay
// byte-for-byte compatible with the server's matcher:
//   - matching is a case-insensitive substring;
//   - the first alias containing the query wins (stored spelling — the
//     server returns the same value as `matched_alias`);
//   - the CALLER applies the "title wins" rule: when the title (or the
//     searched content) already matched, the alias is better link text's
//     opposite — the canonical title is, so no alias is reported.
// GET /v1/search remains authoritative: the command palette uses the server
// results (with their matched_alias) when a query is typed.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace LumenAliases {

// The stored aliases of an object: trimmed, empty entries dropped, exactly
// like the server's read path.
inline QStringList of(const QJsonObject &object) {
    QStringList result;
    for (const auto &value : object.value("properties").toObject().value("aliases").toArray()) {
        const QString alias = value.toString().trimmed();
        if (!alias.isEmpty())
            result << alias;
    }
    return result;
}

// The first alias containing `query` (case-insensitive substring, stored
// spelling), or an empty string when none matches. Mirrors the server's
// matchAlias, including the empty-query case (an empty query is handled by
// the caller: it matches everything and reports no alias).
inline QString matched(const QJsonObject &object, const QString &query) {
    const QString needle = query.trimmed();
    if (needle.isEmpty())
        return {};
    for (const auto &alias : of(object)) {
        if (alias.contains(needle, Qt::CaseInsensitive))
            return alias;
    }
    return {};
}

inline QJsonArray toJson(const QStringList &aliases) {
    QJsonArray array;
    for (const auto &alias : aliases)
        array.append(alias);
    return array;
}

}  // namespace LumenAliases
