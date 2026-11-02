#include "lumen_document.hpp"
#include <QJsonDocument>
#include <QDateTime>
#include <QLocale>
#include <QUrl>
#include <QUrlQuery>
#include <QSet>
#include <QUuid>
#include <functional>

namespace LumenDocument {
QString temporalText(const QJsonObject &a) {
    const auto start = a.value("start").toString(), end = a.value("end").toString();
    const bool time = a.value("kind") == "datetime";
    const auto begin = QDateTime::fromString(start, Qt::ISODate);
    const auto finish = QDateTime::fromString(end, Qt::ISODate);
    QLocale locale = QLocale().language() == QLocale::C ? QLocale(QLocale::Spanish) : QLocale();
    const auto day = time ? begin.date() : QDate::fromString(start, Qt::ISODate);
    QString text = a.value("display_hint") == "time" && time ? begin.time().toString("HH:mm") : locale.toString(day, QLocale::LongFormat);
    if (time && a.value("display_hint") != "time") text += ", " + begin.time().toString("HH:mm");
    if (!end.isEmpty()) {
        if (time && finish.date() == begin.date()) text += " - " + finish.time().toString("HH:mm");
        else text += " - " + locale.toString(time ? finish.date() : QDate::fromString(end, Qt::ISODate), QLocale::LongFormat)
            + (time ? ", " + finish.time().toString("HH:mm") : QString());
    }
    return text.isEmpty() ? start : text;
}
namespace {
QString fresh() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString string(const QJsonObject &o, const char *key) {
    return o.value(key).toString();
}
QJsonObject makeNode(const QString &type, const QJsonArray &content = {}) {
    return {{"id", fresh()}, {"type", type}, {"content", content}};
}
QString inlineText(const QJsonObject &in) {
    const auto type = string(in, "type");
    if (type == "break")
        return QString(QChar::LineSeparator);
    if (type == "object_ref" || type == "block_ref")
        return in.value("label").toString(in.value("object_id").toString());
    if (type == "math")
        return string(in, "latex");
    if (type == "temporal") {
        const auto a = in.value("attrs").toObject();
        return temporalText(a);
    }
    return string(in, "text");
}
QJsonArray presentationRuns(const QJsonArray &content) {
    QJsonArray out; int sequence = 0;
    for (const auto &value : content) {
        const auto in = value.toObject();
        const auto type = string(in, "type");
        auto run = in.value("marks").toObject();
        run["text"] = inlineText(in);
        run["canonical_inline"] = in;
        if (type != "text" && type != "link") { run["canonical_sequence"] = ++sequence; run["canonical_instance"] = fresh(); }
        if (type == "link")
            run["href"] = in.value("href");
        if (type == "object_ref" || type == "block_ref") {
            run["object_id"] = in.value("object_id");
            run["href"] = type == "block_ref" ? "lumen://block/" + string(in, "block_id") +
                                                    "?object_id=" + string(in, "object_id")
                                              : "lumen://object/" + string(in, "object_id");
        }
        if (type == "math") {
            run["type"] = "math";
            run["latex"] = in.value("latex");
            run["display"] = false;
        }
        out.append(run);
    }
    return out;
}
QString runsText(const QJsonArray &runs) {
    QString s;
    for (const auto &r : runs)
        s += r.toObject().value("text").toString();
    return s;
}
QJsonArray canonicalRuns(const QJsonArray &runs) {
    QJsonArray out;
    for (const auto &v : runs) {
        const auto run = v.toObject();
        auto in = run.value("canonical_inline").toObject();
        if (run.value("canonical_marker").toBool() || run.value("reference_icon").toBool())
            continue;
        auto type = string(in, "type");
        if (type.isEmpty()) {
            const auto href = string(run, "href");
            if (string(run, "type") == "math")
                type = "math";
            else if (href.startsWith("lumen://object/"))
                type = "object_ref";
            else if (href.startsWith("lumen://block/"))
                type = "block_ref";
            else
                type = href.isEmpty() ? "text" : "link";
            in["type"] = type;
        }
        if (type == "text" || type == "link") {
            auto text = string(run, "text");
            if (text.isEmpty())
                continue;
            auto marks = in.value("marks").toObject();
            for (const auto *key : {"bold", "italic", "underline", "strike", "code"}) {
                if (run.value(key).toBool())
                    marks[key] = true;
                else
                    marks.remove(key);
            }
            for (const auto *key : {"color", "highlight"})
                if (run.contains(key))
                    marks[key] = run.value(key);
            if (run.contains("point_size") &&
                (in.value("marks").toObject().contains("point_size") || !run.contains("canonical_inline")))
                marks["point_size"] = run.value("point_size");
            if (marks.isEmpty())
                in.remove("marks");
            else
                in["marks"] = marks;
            in["text"] = text;
            if (type == "link")
                in["href"] = run.value("href");
        } else if (type == "math")
            in["latex"] = run.value("latex");
        else if (type == "object_ref" || type == "block_ref") {
            if (in.value("object_id").toString().isEmpty()) {
                in["object_id"] = run.value("object_id");
                if (type == "block_ref" && in.value("object_id").toString().isEmpty()) in["object_id"] = QUrlQuery(QUrl(string(run, "href"))).queryItemValue("object_id");
            }
            if (type == "block_ref" && in.value("block_id").toString().isEmpty())
                in["block_id"] = string(run, "href").mid(14).section('?', 0, 0);
            in["label"] = string(run, "text");
        }
        // Split a normal line separator into semantic breaks, retaining marks.
        if ((type == "text" || type == "link") && string(in, "text").contains(QChar::LineSeparator)) {
            const auto parts = string(in, "text").split(QChar::LineSeparator);
            for (int i = 0; i < parts.size(); ++i) {
                if (i)
                    out.append(QJsonObject{{"type", "break"}});
                if (!parts[i].isEmpty()) {
                    auto part = in;
                    part["text"] = parts[i];
                    out.append(part);
                }
            }
        } else if (type == "break")
            out.append(QJsonObject{{"type", "break"}});
        else
            out.append(in);
    }
    // Qt may split one run into several format fragments. Merge equal semantic
    // styles again instead of exposing those rendering boundaries on the wire.
    QJsonArray merged;
    for (const auto &v : out) {
        auto next = v.toObject();
        if (!merged.isEmpty() && (next.value("type") == "text" || next.value("type") == "link")) {
            auto previous = merged.last().toObject(), a = previous, b = next;
            a.remove("text");
            b.remove("text");
            if (a == b) {
                previous["text"] = string(previous, "text") + string(next, "text");
                merged[merged.size() - 1] = previous;
                continue;
            }
        }
        merged.append(next);
    }
    return merged;
}
QJsonObject toBlock(const QJsonObject &node);
QJsonArray toBlocks(const QJsonArray &nodes) {
    QJsonArray out;
    for (const auto &n : nodes)
        out.append(toBlock(n.toObject()));
    return out;
}
QJsonObject toBlock(const QJsonObject &node) {
    const auto type = string(node, "type");
    const auto attrs = node.value("attrs").toObject();
    auto content = node.value("content").toArray();
    QJsonObject out{{"id", node.value("id")},
                    {"canonical_node", node},
                    {"type", type},
                    {"runs", presentationRuns(content)}};
    auto runs = out.value("runs").toArray();
    if (type == "heading")
        out["level"] = attrs.value("level");
    const auto alignment = attrs.value("alignment").toString("left");
    out["alignment"] = alignment == "center" ? 4 : alignment == "right" ? 2 : 1;
    if (attrs.contains("color"))
        out["text_color"] = attrs.value("color");
    if (attrs.contains("background"))
        out["background_color"] = attrs.value("background");
    if (type == "list_item") {
        const auto kind = attrs.value("kind").toString("bullet");
        out["list_case"] = attrs.value("case").toString("lower");
        out["type"] = kind == "task"      ? "todo"
                      : kind == "decimal" ? "numbered_list"
                      : kind == "alpha"   ? "alphabetic_list"
                      : kind == "roman"   ? "roman_list"
                                          : "bullet_list";
        if (kind == "task") {
            QJsonArray marked;
            marked.append(QJsonObject{
                {"text", attrs.value("checked").toBool() ? QString::fromUtf8("✔ ") : QString::fromUtf8("○ ")},
                {"canonical_marker", true}});
            for (const auto &r : runs)
                marked.append(r);
            runs = marked;
        }
    }
    if (type == "toggle") {
        QJsonArray marked;
        marked.append(QJsonObject{{"text", attrs.value("expanded").toBool(true) ? QString::fromUtf8("▼ ")
                                                                                : QString::fromUtf8("▶ ")},
                                  {"canonical_marker", true}});
        for (const auto &r : runs)
            marked.append(r);
        runs = marked;
    }
    if (type == "code") {
        out["language"] = attrs.value("language");
        runs = QJsonArray{
            QJsonObject{{"text", attrs.value("source").toString().replace('\n', QChar::LineSeparator)}}};
    }
    if (type == "math")
        runs = QJsonArray{QJsonObject{{"text", QString(QChar::ObjectReplacementCharacter)},
                                      {"type", "math"},
                                      {"latex", attrs.value("source")},
                                      {"display", true}}};
    if (type == "image") {
        const auto objectId = string(attrs, "object_id");
        out["type"] = "image";
        runs = QJsonArray{
            QJsonObject{{"text", QString(QChar::ObjectReplacementCharacter)},
                        {"type", "image"},
                        {"object_id", objectId},
                        {"image_name", objectId.isEmpty() ? "lumen://asset/" + string(attrs, "asset_id")
                                                          : "lumen://image/" + objectId},
                        {"width", attrs.value("width")},
                        {"height", attrs.value("height")}}};
        if (!attrs.value("caption").toString().isEmpty())
            runs.append(QJsonObject{{"text", QString(QChar::LineSeparator) + attrs.value("caption").toString()},
                {"color", "#96969e"}, {"point_size", 10}});
    }
    if (type == "divider")
        runs = QJsonArray{QJsonObject{{"text", QString(QChar::ObjectReplacementCharacter)},
                                      {"type", "divider"},
                                      {"image_name", "lumen-rule://divider/light"}}};
    if (type == "object_embed") {
        out["type"] = "embed";
        runs = QJsonArray{
            QJsonObject{{"text", attrs.value("label").toString(attrs.value("object_id").toString())},
                        {"object_id", attrs.value("object_id")},
                        {"href", attrs.value("block_id").toString().isEmpty() ? "lumen://object/" + string(attrs, "object_id") : "lumen://block/" + string(attrs, "block_id") + "?object_id=" + string(attrs, "object_id")}}};
    }
    out["runs"] = runs;
    out["text"] = runsText(runs);
    const auto children = node.value("children").toArray();
    if (type == "columns") {
        QJsonArray cols;
        for (const auto &v : children) {
            auto col = v.toObject();
            cols.append(toBlocks(col.value("children").toArray()));
        }
        out["columns"] = cols;
    } else if (type == "table") {
        QJsonArray rows, align;
        const auto defs = attrs.value("columns").toArray();
        for (const auto &rv : children) {
            QJsonArray row;
            for (const auto &cv : rv.toObject().value("children").toArray()) {
                auto cell = cv.toObject();
                row.append(QJsonObject{{"canonical_cell", cell},
                                       {"blocks", toBlocks(cell.value("children").toArray())}});
            }
            rows.append(row);
        }
        for (const auto &d : defs)
            align.append(d.toObject().value("alignment").toString());
        out["rows"] = rows;
        out["align"] = align;
    } else if (!children.isEmpty()) {
        if (type == "group" || type == "toggle")
            out["children"] = toBlocks(children);
        else {
            auto header = out;
            header.remove("children");
            header["canonical_header"] = true;
            QJsonArray body{header};
            for (const auto &ch : toBlocks(children))
                body.append(ch);
            out["type"] = "group";
            out["canonical_wrapper"] = true;
            out["children"] = body;
        }
    }
    return out;
}
QJsonObject fromBlock(const QJsonObject &block);
QJsonArray fromBlocks(const QJsonArray &blocks) {
    QJsonArray out;
    for (const auto &v : blocks)
        out.append(fromBlock(v.toObject()));
    return out;
}
QJsonObject fromBlock(const QJsonObject &block) {
    auto node = block.value("canonical_node").toObject();
    auto attrs = node.value("attrs").toObject();
    const auto presented = string(block, "type");
    if (node.isEmpty())
        node = makeNode("paragraph");
    node["id"] = block.value("id").toString(node.value("id").toString());
    QString type = presented;
    if (presented == "bullet_list" || presented == "numbered_list" || presented == "alphabetic_list" ||
        presented == "roman_list" || presented == "todo") {
        type = "list_item";
        attrs["kind"] = presented == "todo"              ? "task"
                        : presented == "numbered_list"   ? "decimal"
                        : presented == "alphabetic_list" ? "alpha"
                        : presented == "roman_list"      ? "roman"
                                                         : "bullet";
        if (presented == "todo")
            attrs["checked"] = string(block, "text").startsWith(QString::fromUtf8("✔"));
    }
    if (presented == "small") {
        type = "paragraph";
        attrs["size"] = "small";
    }
    if (presented == "embed")
        type = "object_embed";
    if (presented == "paragraph" && node.value("type") == "image") {
        bool hasImage = false;
        for (const auto &r : block.value("runs").toArray())
            if (r.toObject().value("type") == "image")
                hasImage = true;
        if (hasImage)
            type = "image";
        else {
            type = "paragraph";
            attrs = QJsonObject();
        }
    }
    const auto imageRuns = block.value("runs").toArray();
    if (presented == "paragraph" && imageRuns.size() == 1 &&
        imageRuns.first().toObject().value("type") == "image") {
        type = "image";
        const auto run = imageRuns.first().toObject();
        if (!run.value("object_id").toString().isEmpty())
            attrs["object_id"] = run.value("object_id");
        if (run.value("image_name").toString().startsWith("lumen://asset/"))
            attrs["asset_id"] = run.value("image_name").toString().mid(14);
    }
    if (block.value("canonical_wrapper").toBool()) {
        const auto ch = block.value("children").toArray();
        if (!ch.isEmpty() && ch.first().toObject().value("canonical_header").toBool()) {
            auto parent = fromBlock(ch.first().toObject());
            QJsonArray rest;
            for (int i = 1; i < ch.size(); i++)
                rest.append(ch[i]);
            parent["children"] = fromBlocks(rest);
            return parent;
        }
        // If the header was deleted, unwrap instead of silently using a child
        // as the parent's content. The controller owns explicit parent deletion.
        node["children"] = fromBlocks(ch);
        return node;
    }
    node["type"] = type;
    if (type == "heading")
        attrs["level"] = block.value("level");
    if (block.contains("alignment")) {
        int a = block.value("alignment").toInt();
        const QString align = a & 4 ? "center" : a & 2 ? "right" : "left";
        if (attrs.contains("alignment") || (align != "left" && type != "math"))
            attrs["alignment"] = align;
    }
    if (block.contains("text_color"))
        attrs["color"] = block.value("text_color");
    if (block.contains("background_color"))
        attrs["background"] = block.value("background_color");
    if (type == "code") {
        attrs["source"] = string(block, "text").replace(QChar::LineSeparator, '\n');
        attrs["language"] = block.value("language");
        node.remove("content");
    } else if (type == "math") {
        auto runs = block.value("runs").toArray();
        if (!runs.isEmpty())
            attrs["source"] = runs.first().toObject().value("latex");
        node.remove("content");
    } else if (type == "image") {
        for (const auto &r : block.value("runs").toArray()) {
            const auto run = r.toObject();
            if (run.value("type") == "image") {
                for (const auto *k : {"width", "height"})
                    if (run.value(k).toDouble() > 0)
                        attrs[k] = run.value(k);
                if (attrs.value("object_id").toString().isEmpty() &&
                    !run.value("object_id").toString().isEmpty())
                    attrs["object_id"] = run.value("object_id");
            }
        }
        node.remove("content");
    } else if (type == "object_embed") {
        const auto runs = block.value("runs").toArray();
        if (!runs.isEmpty()) {
            auto run = runs.first().toObject();
            attrs["object_id"] = run.value("object_id");
        }
        if (!attrs.contains("view"))
            attrs["view"] = "embed";
        node.remove("content");
    } else if (type != "columns" && type != "table" && type != "divider" && type != "group")
        node["content"] = canonicalRuns(block.value("runs").toArray());
    if (type == "toggle")
        attrs["expanded"] = !string(block, "text").startsWith(QString::fromUtf8("▶"));
    if (type == "columns") {
        QJsonArray children;
        const auto old = node.value("children").toArray();
        const auto cols = block.value("columns").toArray();
        for (int i = 0; i < cols.size(); i++) {
            auto col = i < old.size() ? old[i].toObject() : makeNode("column");
            col["children"] = fromBlocks(cols[i].toArray());
            children.append(col);
        }
        node["children"] = children;
        node.remove("content");
    } else if (type == "table") {
        QJsonArray rows;
        const auto old = node.value("children").toArray();
        const auto data = block.value("rows").toArray();
        for (int r = 0; r < data.size(); r++) {
            auto row = r < old.size() ? old[r].toObject() : makeNode("table_row");
            QJsonArray cells;
            for (const auto &cv : data[r].toArray()) {
                auto cell = cv.toObject().value("canonical_cell").toObject();
                if (cell.isEmpty())
                    cell = makeNode("table_cell");
                auto bs = cv.toObject().value("blocks").toArray();
                if (bs.isEmpty() && !cv.toObject().contains("blocks"))
                    bs = QJsonArray{QJsonObject{
                        {"id", fresh()}, {"type", "paragraph"}, {"runs", cv.toObject().value("runs")}}};
                cell["children"] = fromBlocks(bs);
                cell.remove("content");
                cells.append(cell);
            }
            row["children"] = cells;
            row.remove("content");
            rows.append(row);
        }
        node["children"] = rows;
        node.remove("content");
        auto defs = attrs.value("columns").toArray();
        const int count = data.isEmpty() ? 0 : data.first().toArray().size();
        while (defs.size() > count)
            defs.removeLast();
        while (defs.size() < count)
            defs.append(QJsonObject{{"id", fresh()}, {"width", 160}});
        attrs["columns"] = defs;
    } else if (block.contains("children"))
        node["children"] = fromBlocks(block.value("children").toArray());
    if (attrs.isEmpty())
        node.remove("attrs");
    else
        node["attrs"] = attrs;
    return node;
}
} // namespace
QJsonObject empty() {
    return {{"schema_version", 2}, {"blocks", QJsonArray()}};
}
QJsonArray toPresentation(const QJsonObject &document) {
    return toBlocks(document.value("blocks").toArray());
}
QJsonObject fromPresentation(const QJsonArray &blocks) {
    return {{"schema_version", 2}, {"blocks", fromBlocks(blocks)}};
}
QJsonObject fromText(const QString &text) {
    QJsonArray nodes;
    for (const auto &line : text.split('\n'))
        nodes.append(makeNode("paragraph", QJsonArray{QJsonObject{{"type", "text"}, {"text", line}}}));
    return {{"schema_version", 2}, {"blocks", nodes}};
}
QString plainText(const QJsonObject &document) {
    QStringList parts;
    std::function<void(const QJsonArray &)> walk = [&](const QJsonArray &nodes) {
        for (const auto &v : nodes) {
            auto n = v.toObject();
            auto type = string(n, "type");
            QString s;
            for (const auto &i : n.value("content").toArray())
                s += inlineText(i.toObject());
            if (type == "code" || type == "math")
                s = string(n.value("attrs").toObject(), "source");
            if (!s.isEmpty())
                parts.append(s);
            walk(n.value("children").toArray());
        }
    };
    walk(document.value("blocks").toArray());
    return parts.join('\n');
}
QJsonArray objectReferences(const QJsonObject &document) {
    QJsonArray ids;
    std::function<void(const QJsonArray &)> walk = [&](const QJsonArray &nodes) {
        for (const auto &v : nodes) {
            auto n = v.toObject();
            auto a = n.value("attrs").toObject();
            auto add = [&](const QString &id) {
                if (!id.isEmpty() && !ids.contains(id))
                    ids.append(id);
            };
            if (n.value("type") == "image" || n.value("type") == "object_embed")
                add(string(a, "object_id"));
            for (const auto &i : n.value("content").toArray())
                add(string(i.toObject(), "object_id"));
            walk(n.value("children").toArray());
        }
    };
    walk(document.value("blocks").toArray());
    return ids;
}
} // namespace LumenDocument
