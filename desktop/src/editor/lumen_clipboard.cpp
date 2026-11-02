#include "lumen_clipboard.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace LumenClipboard {
namespace {
void fail(const char *reason) {
    throw std::runtime_error(reason);
}
void fields(const QJsonObject &object, const QSet<QString> &allowed) {
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!allowed.contains(it.key()))
            fail("Unknown structural clipboard field");
}
bool identityValid(const QString &value) {
    if (value.isEmpty() || value.size() > 128)
        return false;
    for (const auto ch : value)
        if (ch.isSpace() || ch.unicode() < 32 || ch.unicode() == 127)
            return false;
    return true;
}
QString fresh() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace
QJsonObject frame(const QJsonObject &document, const QString &sourceObjectID) {
    return {{"clipboard_version", 1},
            {"source_object_id", sourceObjectID},
            {"operation", "copy"},
            {"document", document}};
}
bool validate(const QJsonObject &document, QString *error) {
    try {
        if (document.value("schema_version") != 2 || !document.value("blocks").isArray())
            fail("Unsupported document format");
        fields(document, {"schema_version", "blocks"});
        QSet<QString> ids;
        int count = 0;
        std::function<void(const QJsonArray &, const QString &, int)> visit = [&](const QJsonArray &nodes,
                                                                                  const QString &parent,
                                                                                  int depth) {
            if (depth > 32)
                fail("Document nesting limit exceeded");
            for (const auto &value : nodes) {
                if (++count > 10000 || !value.isObject())
                    fail("Invalid node count or type");
                const auto node = value.toObject();
                fields(node, {"id", "type", "attrs", "content", "children"});
                if ((node.contains("attrs") && !node.value("attrs").isObject()) ||
                    (node.contains("content") && !node.value("content").isArray()) ||
                    (node.contains("children") && !node.value("children").isArray()))
                    fail("Invalid block field type");
                const auto type = node.value("type").toString();
                static const QSet<QString> types{"paragraph", "heading", "list_item",    "quote",
                                                 "group",     "toggle",  "code",         "math",
                                                 "divider",   "image",   "object_embed", "columns",
                                                 "column",    "table",   "table_row",    "table_cell"};
                if (!types.contains(type))
                    fail("Unsupported block type");
                const auto identity = node.value("id").toString();
                if (!identityValid(identity) || ids.contains(identity))
                    fail("Missing or duplicate block identity");
                ids.insert(identity);
                if ((type == "column" && parent != "columns") || (type == "table_row" && parent != "table") ||
                    (type == "table_cell" && parent != "table_row"))
                    fail("Invalid container parent");
                if (parent == "table_cell" && type != "paragraph")
                    fail("Table cells only accept paragraphs");
                const auto attrs = node.value("attrs").toObject();
                const auto children = node.value("children").toArray();
                if ((type == "code" || type == "math") && !attrs.value("source").isString())
                    fail("Code and math require source text");
                if (type == "heading" &&
                    (attrs.value("level").toInt() < 1 || attrs.value("level").toInt() > 6))
                    fail("Invalid heading level");
                if (type == "column" && attrs.contains("width") && attrs.value("width").toDouble() <= 0)
                    fail("Invalid column width");
                if (type == "image" && !identityValid(attrs.value("asset_id").toString()))
                    fail("An image requires an asset");
                if (type == "object_embed" && attrs.value("object_id").toString().isEmpty())
                    fail("An object embed requires an owner");
                if (type == "table") {
                    const auto columns = attrs.value("columns").toArray();
                    if (columns.isEmpty() || children.isEmpty())
                        fail("An empty table is invalid");
                    for (const auto &v : columns) {
                        const auto def = v.toObject();
                        const auto name = def.value("id").toString();
                        const auto width = def.value("width").toDouble();
                        if (name.isEmpty() || ids.contains(name) || !std::isfinite(width) || width <= 0)
                            fail("Invalid column definition");
                        ids.insert(name);
                    }
                    for (const auto &row : children)
                        if (row.toObject().value("type") != "table_row" ||
                            row.toObject().value("children").toArray().size() != columns.size())
                            fail("Table cells do not match its columns");
                }
                if (type == "columns") {
                    if (children.isEmpty())
                        fail("Empty columns container");
                    for (const auto &child : children)
                        if (child.toObject().value("type") != "column")
                            fail("Invalid columns child");
                }
                if (type == "table_row")
                    for (const auto &child : children)
                        if (child.toObject().value("type") != "table_cell")
                            fail("Invalid row child");
                for (const auto &part : node.value("content").toArray()) {
                    if (!part.isObject())
                        fail("Invalid inline field type");
                    const auto in = part.toObject();
                    fields(in, {"type", "text", "marks", "href", "object_id", "block_id", "label", "latex",
                                "attrs"});
                    if ((in.contains("attrs") && !in.value("attrs").isObject()) ||
                        (in.contains("marks") && !in.value("marks").isObject()))
                        fail("Invalid inline attributes");
                    const auto marks = in.value("marks").toObject();
                    fields(marks, {"bold", "italic", "underline", "strike", "code", "color", "highlight",
                                   "point_size"});
                    for (const auto *mark : {"bold", "italic", "underline", "strike", "code"})
                        if (marks.contains(mark) && !marks.value(mark).isBool())
                            fail("Invalid boolean mark");
                    for (const auto *mark : {"color", "highlight"})
                        if (marks.contains(mark) && !QRegularExpression("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$")
                                                         .match(marks.value(mark).toString())
                                                         .hasMatch())
                            fail("Invalid color mark");
                    if (marks.contains("point_size") &&
                        (!marks.value("point_size").isDouble() || marks.value("point_size").toDouble() <= 0 ||
                         marks.value("point_size").toDouble() > 256))
                        fail("Invalid point size");
                    const auto kind = in.value("type").toString();
                    static const QSet<QString> inlines{"text",      "break", "link",    "object_ref",
                                                       "block_ref", "math",  "temporal"};
                    if (!inlines.contains(kind))
                        fail("Unsupported inline type");
                    if ((kind == "object_ref" || kind == "block_ref") &&
                        in.value("object_id").toString().isEmpty())
                        fail("Reference owner is missing");
                    if (kind == "block_ref" && in.value("block_id").toString().isEmpty())
                        fail("Block reference identity is missing");
                }
                visit(children, type, depth + 1);
            }
        };
        visit(document.value("blocks").toArray(), {}, 0);
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
bool prepare(const QByteArray &bytes, const QString &targetObjectID, QJsonObject *document, QString *error,
             const QJsonObject &newIDs) {
    try {
        if (bytes.size() > 8 * 1024 * 1024)
            fail("Clipboard is too large");
        QJsonParseError parseError;
        const auto value = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !value.isObject())
            fail("Clipboard JSON is invalid");
        const auto envelope = value.object();
        fields(envelope, {"clipboard_version", "source_object_id", "operation", "document"});
        if (envelope.value("clipboard_version") != 1)
            fail("Unsupported clipboard version");
        if (!envelope.value("source_object_id").isString() || !envelope.value("operation").isString())
            fail("Clipboard origin or operation is missing");
        const auto sourceOwner = envelope.value("source_object_id").toString();
        if (!sourceOwner.isEmpty() && !identityValid(sourceOwner))
            fail("Invalid clipboard owner");
        const auto operation = envelope.value("operation").toString("copy");
        if (operation != "copy" && operation != "move")
            fail("Unsupported clipboard operation");
        auto source = envelope.value("document").toObject();
        QString reason;
        if (!validate(source, &reason))
            throw std::runtime_error(reason.toStdString());
        QHash<QString, QString> mapping;
        QSet<QString> nodeIDs;
        std::function<void(const QJsonArray &)> gather = [&](const QJsonArray &nodes) {
            for (const auto &v : nodes) {
                const auto node = v.toObject();
                const auto id = node.value("id").toString();
                mapping[id] = newIDs.value(id).toString(fresh());
                nodeIDs.insert(id);
                if (node.value("type") == "table")
                    for (const auto &d : node.value("attrs").toObject().value("columns").toArray()) {
                        const auto key = d.toObject().value("id").toString();
                        mapping[key] = newIDs.value(key).toString(fresh());
                    }
                gather(node.value("children").toArray());
            }
        };
        gather(source.value("blocks").toArray());
        const auto sourceID = envelope.value("source_object_id").toString();
        auto retarget = [&](QJsonObject attrs) {
            const auto block = attrs.value("block_id").toString();
            if (!sourceID.isEmpty() && !targetObjectID.isEmpty() && attrs.value("object_id") == sourceID &&
                nodeIDs.contains(block)) {
                attrs["object_id"] = targetObjectID;
                attrs["block_id"] = mapping.value(block);
            }
            return attrs;
        };
        std::function<QJsonArray(const QJsonArray &)> rename = [&](const QJsonArray &nodes) {
            QJsonArray out;
            for (const auto &v : nodes) {
                auto node = v.toObject();
                node["id"] = mapping.value(node.value("id").toString());
                if (node.contains("attrs")) {
                    auto attrs = node.value("attrs").toObject();
                    if (node.value("type") == "object_embed")
                        attrs = retarget(attrs);
                    if (node.value("type") == "table") {
                        auto defs = attrs.value("columns").toArray();
                        for (int i = 0; i < defs.size(); ++i) {
                            auto d = defs[i].toObject();
                            d["id"] = mapping.value(d.value("id").toString());
                            defs[i] = d;
                        }
                        attrs["columns"] = defs;
                    }
                    node["attrs"] = attrs;
                }
                if (node.contains("content")) {
                    auto parts = node.value("content").toArray();
                    for (int i = 0; i < parts.size(); ++i)
                        if (parts[i].toObject().value("type") == "block_ref")
                            parts[i] = retarget(parts[i].toObject());
                    node["content"] = parts;
                }
                if (node.contains("children"))
                    node["children"] = rename(node.value("children").toArray());
                out.append(node);
            }
            return out;
        };
        source["blocks"] = rename(source.value("blocks").toArray());
        if (!validate(source, &reason))
            throw std::runtime_error(reason.toStdString());
        if (document)
            *document = source;
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
} // namespace LumenClipboard
