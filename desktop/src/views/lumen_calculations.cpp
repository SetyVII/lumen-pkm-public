#include "lumen_calculations.hpp"
#include "lumen_views.hpp"

#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <numeric>
#include <cmath>
#include <algorithm>

namespace {
// Kinds normalize to the three calculation-capable families; everything else
// keeps its name and falls through to the count-only ops.
QString normalizeKind(const QString &kind) {
    if (kind == "integer" || kind == "rating") return "number";
    if (kind == "checkbox") return "boolean";
    return kind;
}

// Up to two decimals with trailing zeros trimmed: 146.333 -> "146.33",
// 107.50 -> "107.5", 8.00 -> "8". Sums/min/max/range keep the app's general
// number format instead ('g', 12, like LumenQuery::displayValue).
QString decimal(double value) {
    QString text = QString::number(value, 'f', 2);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text == "-0" ? QStringLiteral("0") : text;
}

// The app stores dates as ISO yyyy-MM-dd strings and timestamps as ISO
// datetimes; both parse to the same calendar day.
QDate dateOf(const QJsonValue &value) {
    const QString text = value.toString();
    if (text.isEmpty()) return {};
    QDate date = QDate::fromString(text, Qt::ISODate);
    if (!date.isValid()) {
        const QDateTime stamp = QDateTime::fromString(text, Qt::ISODate);
        if (stamp.isValid()) date = stamp.date();
    }
    return date;
}

// The same truthy sets the view filter uses for boolean equality.
bool truthy(const QJsonValue &value) {
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble() != 0;
    if (value.isString()) {
        const QString text = value.toString().trimmed().toLower();
        return QStringList{"true", "1", QStringLiteral("sí"), "si", "yes"}.contains(text);
    }
    return false;
}

bool blank(const QJsonValue &v) {
    return v.isNull() || v.isUndefined() || (v.isString() && v.toString().trimmed().isEmpty()) ||
        (v.isArray() && v.toArray().isEmpty());
}

// Numeric cell: JSON doubles directly; strings that fully parse as C-locale
// numbers too (defensive — properties are typed, but hand-written values can
// arrive as strings).
bool numberCell(const QJsonValue &value, double &out) {
    if (value.isDouble()) { out = value.toDouble(); return true; }
    if (value.isString()) {
        const QString text = value.toString().trimmed();
        if (text.isEmpty()) return false;
        bool ok = false;
        const double parsed = QLocale::c().toDouble(text, &ok);
        if (ok && std::isfinite(parsed)) { out = parsed; return true; }
    }
    return false;
}
} // namespace

namespace LumenCalculations {

QString kindOf(const QJsonArray &types, const QJsonArray &objects, const QString &field) {
    if (field == "created_at" || field == "updated_at") return "date";
    if (field.startsWith("properties.")) {
        const QString key = field.mid(11);
        // The type definition wins when it exists (scope-agnostic: the first
        // definition of the key wins, matching how the app resolves
        // definitions elsewhere). No definition: infer from stored values.
        for (auto tv : types) {
            const auto type = tv.toObject();
            for (auto pv : type.value("properties").toArray()) {
                const auto definition = pv.toObject();
                if (pv.isString() && pv.toString() == key) return "text";
                if (definition.value("key").toString(definition.value("id").toString(definition.value("name").toString())) == key)
                    return normalizeKind(definition.value("type").toString(definition.value("kind").toString("text")));
            }
        }
        for (auto ov : objects) {
            const auto value = ov.toObject().value("properties").toObject().value(key);
            if (value.isDouble()) return "number";
            if (value.isBool()) return "boolean";
        }
    }
    return "text";
}

QList<QPair<QString, QString>> opsForKind(const QString &kind) {
    QList<QPair<QString, QString>> counts{{"count_filled", "Count filled"}, {"count_empty", "Count empty"}};
    if (kind == "number")
        return {{"sum", "Sum"},   {"avg", "Average"}, {"median", "Median"}, {"min", "Min"},
                {"max", "Max"},    {"range", "Range"}, {"count_filled", "Count filled"},
                {"count_empty", "Count empty"}};
    if (kind == "date")
        return {{"earliest", "Earliest"}, {"latest", "Latest"}, {"range", "Range (days)"},
                {"count_filled", "Count filled"}, {"count_empty", "Count empty"}};
    if (kind == "boolean")
        return {{"checked_count", "Checked count"}, {"checked_percent", "Checked percent"},
                {"count_filled", "Count filled"}, {"count_empty", "Count empty"}};
    return counts;
}

QString menuLabel(const QString &kind, const QString &op) {
    for (const auto &pair : opsForKind(kind))
        if (pair.first == op) return pair.second;
    return op;
}

QString opLabel(const QString &op) {
    static const QHash<QString, QString> labels{{"sum", "Sum"}, {"avg", "Average"}, {"median", "Median"},
        {"min", "Min"}, {"max", "Max"}, {"range", "Range"}, {"earliest", "Earliest"}, {"latest", "Latest"},
        {"checked_count", "Checked"}, {"checked_percent", "Checked %"},
        {"count_filled", "Filled"}, {"count_empty", "Empty"}};
    return labels.value(op, op);
}

Result compute(const QString &op, const QString &field, const QJsonArray &objects,
               const QJsonArray &universe, const QJsonArray &types) {
    Result result;
    const QString kind = kindOf(types, universe, field);
    const QString name = field.startsWith("properties.") ? field.mid(11) : field;
    const auto finish = [&result, &op, &name](const QString &text) {
        result.valid = true;
        result.text = text;
        result.summary = QString("%1 (%2): %3").arg(opLabel(op), name, text);
    };

    if (op == "count_filled" || op == "count_empty") {
        int filled = 0;
        for (auto ov : objects) {
            const auto object = ov.toObject();
            if (!blank(LumenQuery::fieldValue(object, field, universe))) ++filled;
        }
        finish(QString::number(op == "count_filled" ? filled : objects.size() - filled));
        return result;
    }

    if (kind == "number" && (op == "sum" || op == "avg" || op == "median" || op == "min" || op == "max" || op == "range")) {
        QList<double> values;
        for (auto ov : objects) {
            double number = 0;
            if (numberCell(LumenQuery::fieldValue(ov.toObject(), field, universe), number)) values << number;
        }
        if (values.isEmpty()) return result; // nothing to summarize: omit
        std::sort(values.begin(), values.end());
        const double sum = std::accumulate(values.begin(), values.end(), 0.0);
        if (op == "sum") finish(QString::number(sum, 'g', 12));
        else if (op == "avg") finish(decimal(sum / values.size()));
        else if (op == "median") {
            const double median = values.size() % 2 ? values.at(values.size() / 2)
                : (values.at(values.size() / 2 - 1) + values.at(values.size() / 2)) / 2;
            finish(decimal(median));
        } else if (op == "min") finish(QString::number(values.first(), 'g', 12));
        else if (op == "max") finish(QString::number(values.last(), 'g', 12));
        else finish(QString::number(values.last() - values.first(), 'g', 12));
        return result;
    }

    if (kind == "date" && (op == "earliest" || op == "latest" || op == "range")) {
        QDate earliest, latest;
        for (auto ov : objects) {
            const QDate date = dateOf(LumenQuery::fieldValue(ov.toObject(), field, universe));
            if (!date.isValid()) continue;
            if (!earliest.isValid() || date < earliest) earliest = date;
            if (!latest.isValid() || date > latest) latest = date;
        }
        if (!earliest.isValid()) return result; // no dates: omit
        if (op == "earliest") finish(earliest.toString(Qt::ISODate));
        else if (op == "latest") finish(latest.toString(Qt::ISODate));
        else {
            const qint64 days = earliest.daysTo(latest);
            finish(QString("%1 %2").arg(days).arg(days == 1 ? "day" : "days"));
        }
        return result;
    }

    if (kind == "boolean" && (op == "checked_count" || op == "checked_percent")) {
        int filled = 0, checked = 0;
        for (auto ov : objects) {
            const auto value = LumenQuery::fieldValue(ov.toObject(), field, universe);
            if (blank(value)) continue;
            ++filled;
            if (truthy(value)) ++checked;
        }
        if (!filled) return result; // no checkbox values: omit
        if (op == "checked_count") finish(QString("%1/%2").arg(checked).arg(filled));
        else finish(QString::number(qRound(100.0 * checked / filled)) + "%");
        return result;
    }

    // Invalid op/kind pairs (a stale saved spec, a property whose type
    // changed): invalid result — never offered by the picker, never shown.
    return result;
}

Result compute(const QJsonObject &calculation, const QJsonArray &objects,
               const QJsonArray &universe, const QJsonArray &types) {
    return compute(calculation.value("op").toString(), calculation.value("property").toString(),
                   objects, universe, types);
}

QString strip(const QJsonArray &calculations, const QJsonArray &objects,
              const QJsonArray &universe, const QJsonArray &types) {
    QStringList parts;
    for (auto cv : calculations) {
        const auto part = compute(cv.toObject(), objects, universe, types);
        if (part.valid) parts << part.summary;
    }
    return parts.join(" · ");
}

bool anyValid(const QJsonArray &calculations, const QJsonArray &objects,
              const QJsonArray &universe, const QJsonArray &types) {
    for (auto cv : calculations)
        if (compute(cv.toObject(), objects, universe, types).valid) return true;
    return false;
}

} // namespace LumenCalculations
