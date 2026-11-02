#include "lumen_document.hpp"
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace LumenDocument {
namespace {
using Transform = std::function<QJsonArray(const QJsonObject &)>;
QString id(const QJsonObject &o) {
    return o.value("id").toString();
}
void fail(const char *message) {
    throw std::runtime_error(message);
}
QString fresh() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QJsonArray transform(QJsonArray nodes, const QString &target, const Transform &fn, bool &found) {
    QJsonArray out;
    for (const auto &v : nodes) {
        auto node = v.toObject();
        if (id(node) == target) {
            found = true;
            for (const auto &replacement : fn(node))
                out.append(replacement);
        } else {
            if (node.contains("children"))
                node["children"] = transform(node["children"].toArray(), target, fn, found);
            out.append(node);
        }
    }
    return out;
}
QJsonObject lookup(const QJsonArray &nodes, const QString &target) {
    for (const auto &v : nodes) {
        const auto node = v.toObject();
        if (id(node) == target)
            return node;
        const auto nested = lookup(node["children"].toArray(), target);
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}
QJsonArray insertAt(QJsonArray nodes, int position, const QJsonArray &values) {
    if (position < 0 || position > nodes.size())
        fail("position outside child list");
    for (int i = 0; i < values.size(); i++)
        nodes.insert(position + i, values[i]);
    return nodes;
}
QJsonArray destination(QJsonArray nodes, const QString &parent, int position, const QJsonArray &values) {
    if (parent.isEmpty())
        return insertAt(nodes, position, values);
    bool found = false;
    auto out = transform(
        nodes, parent,
        [&](QJsonObject node) {
            node["children"] = insertAt(node["children"].toArray(), position, values);
            return QJsonArray{node};
        },
        found);
    if (!found)
        fail("parent not found");
    return out;
}
void check(const QJsonArray &nodes, QSet<QString> &seen, int depth = 0) {
    if (depth > 32)
        fail("document nesting limit");
    for (const auto &v : nodes) {
        auto node = v.toObject();
        auto name = id(node);
        if (name.isEmpty() || seen.contains(name))
            fail("invalid or duplicate node id");
        seen.insert(name);
        const auto type = node["type"].toString();
        auto children = node["children"].toArray();
        if (type == "table") {
            const auto defs = node["attrs"].toObject()["columns"].toArray();
            if (defs.isEmpty() || children.isEmpty())
                fail("table requires columns and rows");
            for (const auto &d : defs) {
                auto def = d.toObject();
                double w = def["width"].toDouble();
                if (id(def).isEmpty() || seen.contains(id(def)) || !std::isfinite(w) || w <= 0)
                    fail("invalid table width or column id");
                seen.insert(id(def));
            }
            for (const auto &r : children)
                if (r.toObject()["children"].toArray().size() != defs.size())
                    fail("table row width mismatch");
        }
        if (type == "columns" && children.isEmpty())
            fail("columns cannot be empty");
        check(children, seen, depth + 1);
    }
}
QJsonObject renamed(QJsonObject node, const QJsonObject &names) {
    auto old = id(node);
    node["id"] = names.value(old).toString(fresh());
    auto attrs = node.value("attrs").toObject();
    if (node["type"] == "table") {
        auto defs = attrs["columns"].toArray();
        for (int i = 0; i < defs.size(); i++) {
            auto d = defs[i].toObject();
            d["id"] = names.value(id(d)).toString(fresh());
            defs[i] = d;
        }
        attrs["columns"] = defs;
        node["attrs"] = attrs;
    }
    if (node.contains("children")) {
        QJsonArray children;
        for (const auto &v : node["children"].toArray())
            children.append(renamed(v.toObject(), names));
        node["children"] = children;
    }
    return node;
}
void gather(const QJsonArray &nodes, const QSet<QString> &selected, QJsonArray &roots, QSet<QString> &all) {
    std::function<void(const QJsonObject &)> descendants = [&](const QJsonObject &n) {
        all.insert(id(n));
        for (const auto &v : n["children"].toArray())
            descendants(v.toObject());
    };
    for (const auto &v : nodes) {
        auto n = v.toObject();
        if (selected.contains(id(n))) {
            roots.append(n);
            descendants(n);
        } else
            gather(n["children"].toArray(), selected, roots, all);
    }
}
QJsonArray drop(QJsonArray nodes, const QSet<QString> &roots) {
    QJsonArray out;
    for (const auto &v : nodes) {
        auto node = v.toObject();
        if (roots.contains(id(node)))
            continue;
        if (node.contains("children"))
            node["children"] = drop(node["children"].toArray(), roots);
        out.append(node);
    }
    return out;
}
QJsonObject paragraph(QString identity) {
    return {{"id", identity}, {"type", "paragraph"}, {"content", QJsonArray()}};
}
QJsonArray one(QJsonObject node) {
    return QJsonArray{node};
}
} // namespace
bool applyCommands(const QJsonObject &input, const QJsonArray &commands, QJsonObject *result,
                   QString *error) {
    if (result)
        *result = input;
    try {
        auto nodes = input["blocks"].toArray();
        for (const auto &cv : commands) {
            auto c = cv.toObject();
            auto type = c["type"].toString();
            auto target = c["node_id"].toString();
            int pos = c["position"].toInt();
            bool found = false;
            if (type == "append") {
                const auto parent = c["parent_id"].toString();
                const auto children =
                    parent.isEmpty() ? nodes : lookup(nodes, parent).value("children").toArray();
                if (!parent.isEmpty() && lookup(nodes, parent).isEmpty())
                    fail("parent not found");
                nodes = destination(nodes, parent, children.size(), one(c["node"].toObject()));
            } else if (type == "append_section") {
                auto heading = c["node"].toObject();
                if (heading.value("type") != "heading")
                    fail("append_section requires heading");
                auto entries = heading.value("children").toArray();
                heading.remove("children");
                const auto title = plainText({{"blocks", QJsonArray{heading}}});
                int position = nodes.size(), level = heading.value("attrs").toObject().value("level").toInt();
                bool matched = false;
                for (int i = 0; i < nodes.size(); ++i) {
                    const auto node = nodes[i].toObject();
                    if (node.value("type") != "heading")
                        continue;
                    const auto nodeLevel = node.value("attrs").toObject().value("level").toInt();
                    if (matched && nodeLevel <= level) {
                        position = i;
                        break;
                    }
                    auto comparison = node;
                    comparison.remove("children");
                    if (plainText({{"blocks", QJsonArray{comparison}}}) == title) {
                        matched = true;
                        level = nodeLevel;
                    }
                }
                if (!matched)
                    entries.prepend(heading);
                nodes = insertAt(nodes, position, entries);
            } else if (type == "insert")
                nodes = destination(nodes, c["parent_id"].toString(), pos, one(c["node"].toObject()));
            else if (type == "delete" || type == "move" || type == "copy") {
                QSet<QString> selected;
                for (const auto &v : c["node_ids"].toArray())
                    selected.insert(v.toString());
                if (!target.isEmpty())
                    selected.insert(target);
                if (selected.isEmpty())
                    fail("selection is empty");
                for (const auto &t : selected)
                    if (lookup(nodes, t).isEmpty())
                        fail("node not found");
                QJsonArray roots;
                QSet<QString> desc;
                gather(nodes, selected, roots, desc);
                if (type == "move" && desc.contains(c["parent_id"].toString()))
                    fail("cannot move node inside itself");
                QSet<QString> rootIDs;
                for (const auto &v : roots)
                    rootIDs.insert(id(v.toObject()));
                if (type != "copy")
                    nodes = drop(nodes, rootIDs);
                if (type == "copy") {
                    QJsonArray copies;
                    for (const auto &v : roots)
                        copies.append(renamed(v.toObject(), c["new_ids"].toObject()));
                    roots = copies;
                }
                if (type != "delete")
                    nodes = destination(nodes, c["parent_id"].toString(), pos, roots);
            } else {
                nodes = transform(
                    nodes, target,
                    [&](QJsonObject n) {
                        if (type == "replace") {
                            auto replacement = c["node"].toObject();
                            replacement["id"] = id(n);
                            return one(replacement);
                        }
                        if (type == "replace_inline") {
                            n["content"] = c["content"].toArray();
                            return one(n);
                        }
                        if (type == "set_attrs") {
                            auto a = n["attrs"].toObject();
                            const auto patch = c["attrs"].toObject();
                            for (auto it = patch.begin(); it != patch.end(); ++it) {
                                if (it.value().isNull())
                                    a.remove(it.key());
                                else
                                    a[it.key()] = it.value();
                            }
                            n["attrs"] = a;
                            return one(n);
                        }
                        if (type == "wrap") {
                            auto wrapper = c["node"].toObject();
                            wrapper["children"] = one(n);
                            return one(wrapper);
                        }
                        if (type == "unwrap")
                            return n["children"].toArray();
                        if (type == "flatten_columns") {
                            if (n["type"] != "columns")
                                fail("target is not columns");
                            QJsonArray flat;
                            for (const auto &col : n["children"].toArray())
                                for (const auto &ch : col.toObject()["children"].toArray())
                                    flat.append(ch);
                            return flat;
                        }
                        if (!type.startsWith("table_"))
                            fail("unsupported command");
                        if (n["type"] != "table")
                            fail("target is not table");
                        auto rows = n["children"].toArray();
                        auto attrs = n["attrs"].toObject();
                        auto defs = attrs["columns"].toArray();
                        int col = -1;
                        for (int i = 0; i < defs.size(); i++)
                            if (id(defs[i].toObject()) == c["column_id"].toString())
                                col = i;
                        if (type == "table_insert_row")
                            rows = insertAt(rows, pos, one(c["node"].toObject()));
                        else if (type == "table_delete_row" || type == "table_move_row") {
                            int row = -1;
                            for (int i = 0; i < rows.size(); i++)
                                if (id(rows[i].toObject()) == c["parent_id"].toString())
                                    row = i;
                            if (row < 0)
                                fail("row not found");
                            const QJsonValue value = rows[row];
                            rows.removeAt(row);
                            if (type == "table_move_row")
                                rows = insertAt(rows, pos, QJsonArray{value});
                        } else if (type == "table_insert_column") {
                            const QString columnID = c["column_id"].toString();
                            defs = insertAt(defs, pos,
                                            one(QJsonObject{{"id", columnID}, {"width", c["width"]}}));
                            for (int r = 0; r < rows.size(); r++) {
                                auto row = rows[r].toObject();
                                auto ids = c["new_ids"].toObject();
                                auto cell = QJsonObject{
                                    {"id", ids.value(id(row) + "/cell").toString(fresh())},
                                    {"type", "table_cell"},
                                    {"children",
                                     one(paragraph(ids.value(id(row) + "/paragraph").toString(fresh())))}};
                                row["children"] = insertAt(row["children"].toArray(), pos, one(cell));
                                rows[r] = row;
                            }
                        } else {
                            if (col < 0)
                                fail("column not found");
                            if (type == "table_set_width") {
                                auto d = defs[col].toObject();
                                d["width"] = c["width"];
                                defs[col] = d;
                            } else if (type == "table_delete_column" || type == "table_move_column") {
                                const QJsonValue def = defs[col];
                                defs.removeAt(col);
                                if (type == "table_move_column")
                                    defs = insertAt(defs, pos, QJsonArray{def});
                                for (int r = 0; r < rows.size(); r++) {
                                    auto row = rows[r].toObject();
                                    auto cells = row["children"].toArray();
                                    const QJsonValue cell = cells[col];
                                    cells.removeAt(col);
                                    if (type == "table_move_column")
                                        cells = insertAt(cells, pos, QJsonArray{cell});
                                    row["children"] = cells;
                                    rows[r] = row;
                                }
                            } else if (type == "table_sort") {
                                QList<QJsonObject> data;
                                QJsonArray header;
                                for (const auto &rv : rows) {
                                    auto r = rv.toObject();
                                    if (r["attrs"].toObject()["header"].toBool())
                                        header.append(r);
                                    else
                                        data.append(r);
                                }
                                auto text = [&](const QJsonObject &r) {
                                    auto cell = r["children"].toArray()[col].toObject();
                                    return plainText({{"blocks", cell["children"]}});
                                };
                                std::stable_sort(data.begin(), data.end(), [&](const auto &a, const auto &b) {
                                    const auto x = text(a), y = text(b);
                                    int cmp = QString::localeAwareCompare(x, y);
                                    if (c["numeric"].toBool()) {
                                        bool xok, yok;
                                        double xn = x.toDouble(&xok), yn = y.toDouble(&yok);
                                        if (xok && yok)
                                            cmp = xn < yn ? -1 : xn > yn ? 1 : 0;
                                    }
                                    return c["direction"] == "desc" ? cmp > 0 : cmp < 0;
                                });
                                rows = header;
                                for (const auto &r : data)
                                    rows.append(r);
                            } else
                                fail("unsupported table command");
                        }
                        n["children"] = rows;
                        attrs["columns"] = defs;
                        n["attrs"] = attrs;
                        return one(n);
                    },
                    found);
                if (!found)
                    fail("node not found");
            }
            QSet<QString> seen;
            check(nodes, seen);
        }
        if (result)
            *result = {{"schema_version", 2}, {"blocks", nodes}};
        if (error)
            error->clear();
        return true;
    } catch (const std::exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return false;
    }
}
} // namespace LumenDocument
