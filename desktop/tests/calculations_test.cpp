// D7 (view calculations): Capacities-style calculations in the desktop data
// views (https://docs.capacities.io/reference/views#calculations). Covers
// the pure op-per-type engine (lumen_calculations.{hpp,cpp}) and the
// LumenViews surfaces: the table column footer, the summary strip, the
// per-group lines, the Customize "Calculations" add/edit/remove flow, the
// persisted spec, live recalculation and the empty-view rule. Offscreen,
// no server — the data is the view's own objects.
#include "../src/views/lumen_calculations.hpp"
#include "../src/views/lumen_views.hpp"
#include <QtWidgets>
#include <QtTest>
#include <algorithm>
#include <cstdio>

static bool check(bool value, const char *message)
{
    if (!value) fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

static QStringList opIds(const QString &kind)
{
    QStringList ids;
    for (const auto &pair : LumenCalculations::opsForKind(kind)) ids << pair.first;
    return ids;
}

static QJsonObject calc(const QString &property, const QString &op)
{
    return QJsonObject{{"property", property}, {"op", op}};
}

static QString value(const LumenCalculations::Result &r)
{
    return r.valid ? r.text : QString();
}

static QStringList calculationTexts(QWidget *host)
{
    QStringList out;
    for (auto *label : host->findChildren<QLabel *>("groupCalculations")) out << label->text();
    std::sort(out.begin(), out.end());
    return out;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    bool pass = true;

    // Fixture: one type with every calculation-capable property kind.
    const QJsonArray types{
        QJsonObject{{"id", "book"}, {"name", "Libro"}, {"properties", QJsonArray{
            QJsonObject{{"key", "pages"}, {"type", "integer"}, {"min", 0}},
            QJsonObject{{"key", "published"}, {"type", "date"}},
            QJsonObject{{"key", "read"}, {"type", "boolean"}},
            QJsonObject{{"key", "status"}, {"type", "select"}, {"options", QJsonArray{"Pendiente", "Leyendo", "Leído"}}},
            QJsonObject{{"key", "author"}, {"type", "text"}},
            QJsonObject{{"key", "related"}, {"type", "relation"}, {"target_type", "book"}}}}}};
    auto object = [](const char *id, const char *title, QJsonObject properties) {
        return QJsonObject{{"id", id}, {"type_id", "book"}, {"title", title},
            {"content", ""}, {"created_at", "2026-10-01T10:00:00Z"}, {"updated_at", "2026-10-01T10:00:00Z"},
            {"links", QJsonArray{}}, {"properties", properties}};
    };
    QJsonArray objects;
    objects.append(object("b1", "Bosque", QJsonObject{{"pages", 120}, {"published", "2026-01-05"}, {"read", true}, {"status", "Leído"}, {"author", "Iris"}}));
    objects.append(object("b2", "Colina", QJsonObject{{"pages", 340}, {"published", "2026-03-11"}, {"read", true}, {"author", "Iris"}}));
    objects.append(object("b3", "Duna", QJsonObject{{"pages", 95}, {"published", "2025-12-02"}, {"read", false}}));
    objects.append(object("b4", "Este", QJsonObject{{"pages", 0}, {"read", false}, {"status", "Pendiente"}}));
    objects.append(object("b5", "Faro", QJsonObject{{"pages", 265}, {"published", "2026-08-30"}, {"author", "Ada"}}));
    objects.append(object("b6", "Glaciar", QJsonObject{{"read", true}}));
    objects.append(object("b7", "Huerto", QJsonObject{{"pages", 58}, {"published", "2026-10-31"}, {"read", true}, {"author", "Ada"}}));

    // ---------- Engine: kinds and the op-per-type picker ----------
    pass &= check(LumenCalculations::kindOf(types, objects, "properties.pages") == "number", "integer definition normalizes to the number kind");
    pass &= check(LumenCalculations::kindOf(types, objects, "properties.published") == "date", "date definition maps to the date kind");
    pass &= check(LumenCalculations::kindOf(types, objects, "properties.read") == "boolean", "boolean definition maps to the boolean kind");
    pass &= check(LumenCalculations::kindOf(types, objects, "properties.status") == "select", "select keeps its kind (count ops only)");
    pass &= check(LumenCalculations::kindOf(types, objects, "properties.related") == "relation", "relation keeps its kind (count ops only)");
    pass &= check(LumenCalculations::kindOf(types, objects, "created_at") == "date", "system timestamps count as dates");
    pass &= check(LumenCalculations::kindOf(types, objects, "title") == "text", "title is a plain text field");
    pass &= check(opIds("number") == QStringList{"sum", "avg", "median", "min", "max", "range", "count_filled", "count_empty"}, "number properties offer sum/average/median/min/max/range plus counts");
    pass &= check(opIds("date") == QStringList{"earliest", "latest", "range", "count_filled", "count_empty"}, "date properties offer earliest/latest/range plus counts");
    pass &= check(opIds("boolean") == QStringList{"checked_count", "checked_percent", "count_filled", "count_empty"}, "checkbox properties offer checked count/percent plus counts");
    pass &= check(opIds("text") == QStringList{"count_filled", "count_empty"}, "text properties only offer the count ops");
    pass &= check(opIds("select") == QStringList{"count_filled", "count_empty"}, "select properties only offer the count ops");
    pass &= check(!opIds("text").contains("sum") && !opIds("date").contains("sum") && !opIds("boolean").contains("sum"), "sum is never offered for non-number properties");
    pass &= check(LumenCalculations::menuLabel("date", "range") == "Range (days)" && LumenCalculations::menuLabel("number", "range") == "Range", "the range menu label spells out the day unit for dates");
    pass &= check(LumenCalculations::opLabel("checked_count") == "Checked" && LumenCalculations::opLabel("count_empty") == "Empty", "compact summary labels are available for strips");

    // ---------- Engine: number ops (0 is a value, missing pages is not) ----------
    pass &= check(value(LumenCalculations::compute("sum", "properties.pages", objects, objects, types)) == "878", "sum covers filled numeric values including zero");
    pass &= check(value(LumenCalculations::compute("avg", "properties.pages", objects, objects, types)) == "146.33", "average keeps two decimals");
    pass &= check(value(LumenCalculations::compute("median", "properties.pages", objects, objects, types)) == "107.5", "median averages the two middle values");
    pass &= check(value(LumenCalculations::compute("min", "properties.pages", objects, objects, types)) == "0", "minimum is zero, not the missing value");
    pass &= check(value(LumenCalculations::compute("max", "properties.pages", objects, objects, types)) == "340", "maximum over filled values");
    pass &= check(value(LumenCalculations::compute("range", "properties.pages", objects, objects, types)) == "340", "numeric range is max minus min");
    pass &= check(!LumenCalculations::compute("sum", "properties.related", objects, objects, types).valid, "sum over a relation has nothing to summarize");

    // ---------- Engine: date ops (ISO dates and timestamp fields) ----------
    pass &= check(value(LumenCalculations::compute("earliest", "properties.published", objects, objects, types)) == "2025-12-02", "earliest published date");
    pass &= check(value(LumenCalculations::compute("latest", "properties.published", objects, objects, types)) == "2026-10-31", "latest published date");
    pass &= check(value(LumenCalculations::compute("range", "properties.published", objects, objects, types)) == QString("%1 days").arg(QDate(2025, 12, 2).daysTo(QDate(2026, 10, 31))), "date range counts days between earliest and latest");
    pass &= check(value(LumenCalculations::compute("earliest", "created_at", objects, objects, types)) == "2026-10-01", "timestamps resolve to their calendar day");

    // ---------- Engine: checkbox ops and counts ----------
    pass &= check(value(LumenCalculations::compute("checked_count", "properties.read", objects, objects, types)) == "4/6", "checked count is checked over filled");
    pass &= check(value(LumenCalculations::compute("checked_percent", "properties.read", objects, objects, types)) == "67%", "checked percent rounds to a whole percent");
    pass &= check(value(LumenCalculations::compute("count_filled", "properties.status", objects, objects, types)) == "2", "count filled for a select property");
    pass &= check(value(LumenCalculations::compute("count_empty", "properties.status", objects, objects, types)) == "5", "count empty for a select property");
    pass &= check(value(LumenCalculations::compute("count_filled", "properties.related", objects, objects, types)) == "0" &&
                  value(LumenCalculations::compute("count_empty", "properties.related", objects, objects, types)) == "7", "counts work for relation properties");
    pass &= check(value(LumenCalculations::compute("count_filled", "properties.author", objects, objects, types)) == "4" &&
                  value(LumenCalculations::compute("count_empty", "properties.author", objects, objects, types)) == "3", "counts work for text properties");
    pass &= check(!LumenCalculations::compute("checked_count", "properties.pages", objects, objects, types).valid &&
                  !LumenCalculations::compute("earliest", "properties.pages", objects, objects, types).valid, "ops that do not fit the property never render");
    const auto stripText = LumenCalculations::strip(QJsonArray{calc("properties.pages", "sum"), calc("properties.read", "checked_count")}, objects, objects, types);
    pass &= check(stripText == "Sum (pages): 878 · Checked (read): 4/6", "the strip joins every valid calculation");

    // ---------- View: table column footer ----------
    LumenViews view;
    view.setTheme(true, false);
    view.resize(980, 700);
    view.setData(objects, types);
    QJsonObject spec{{"type_id", "book"}, {"presentation", "table"},
        {"visible_properties", QJsonArray{"title", "properties.pages", "properties.read", "properties.status"}},
        {"sort", QJsonArray{QJsonObject{{"field", "title"}, {"direction", "asc"}}}},
        {"calculations", QJsonArray{calc("properties.pages", "sum"), calc("properties.pages", "avg")}}};
    view.setScope("book", spec);
    view.show();
    QTest::qWait(30);
    auto *table = view.findChild<QTableView *>("objectsTable");
    pass &= check(table && table->model()->rowCount() == 9, "one footer row per calculation extends the table");
    if (table) {
        const auto footerSum = table->model()->index(7, 1);
        const auto footerAvg = table->model()->index(8, 1);
        pass &= check(footerSum.data(Qt::DisplayRole).toString() == "878" && footerAvg.data(Qt::DisplayRole).toString() == "146.33", "footer cells sit under the calculated column");
        pass &= check(!footerSum.sibling(7, 0).data(Qt::DisplayRole).isValid(), "columns without a calculation stay footer-empty");
        pass &= check(footerSum.data(Qt::ToolTipRole).toString() == "Sum (pages): 878", "footer cells carry the op and property as tooltip");
        pass &= check(footerSum.flags() == Qt::NoItemFlags, "the footer row is a summary, not a selectable object");
        pass &= check(!table->model()->setData(footerSum, "5", Qt::EditRole), "the footer row rejects edits");
    }
    // A calculation whose column is hidden renders nothing.
    QJsonObject hidden = spec;
    hidden["visible_properties"] = QJsonArray{"title", "properties.read"};
    view.setScope("book", hidden);
    QTest::qWait(30);
    table = view.findChild<QTableView *>("objectsTable");
    pass &= check(table && table->model()->rowCount() == 7, "no visible column means no footer row");
    // A calculation with no data renders nothing either (no "Sum: 0" noise).
    QJsonObject noise = spec;
    noise["calculations"] = QJsonArray{calc("properties.related", "sum")};
    view.setScope("book", noise);
    QTest::qWait(30);
    table = view.findChild<QTableView *>("objectsTable");
    pass &= check(table && table->model()->rowCount() == 7, "an empty summary adds no footer row");

    // ---------- View: summary strip on list + live recalculation ----------
    QJsonObject stripSpec = spec;
    stripSpec["presentation"] = "list";
    stripSpec["calculations"] = QJsonArray{calc("properties.pages", "sum")};
    view.setScope("book", stripSpec);
    QTest::qWait(30);
    auto *strip = view.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 878", "list view shows the summary strip at the top");
    auto *search = view.findChild<QLineEdit *>("viewSearch");
    if (search) search->setText("Bosque");
    QTest::qWait(30);
    strip = view.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 120", "the strip recalculates when the filter narrows the view");
    QJsonArray changed = objects;
    auto first = changed.at(0).toObject();
    auto firstProperties = first.value("properties").toObject();
    firstProperties["pages"] = 200;
    first["properties"] = firstProperties;
    changed.replace(0, first);
    view.setData(changed, types);
    QTest::qWait(40);
    strip = view.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 200", "the strip recalculates on the polling refresh path");
    if (search) search->clear();
    QTest::qWait(30);
    strip = view.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 958", "clearing the search restores the full set result");
    view.setData(objects, types);
    QTest::qWait(40);

    // ---------- View: grouped presentations show per-group results ----------
    QJsonObject grouped = stripSpec;
    grouped["group_by"] = "properties.status";
    view.setScope("book", grouped);
    QTest::qWait(30);
    pass &= check(!view.findChild<QLabel *>("viewCalculations"), "a grouped view has no top strip");
    pass &= check(calculationTexts(&view) == QStringList{"Sum (pages): 0", "Sum (pages): 120", "Sum (pages): 758"}, "each group shows its own result under its header");
    QJsonObject board = stripSpec;
    board["group_by"] = "properties.read";
    board["presentation"] = "kanban";
    board["calculations"] = QJsonArray{calc("properties.read", "checked_count")};
    view.setScope("book", board);
    QTest::qWait(30);
    pass &= check(calculationTexts(&view) == QStringList{"Checked (read): 0/2", "Checked (read): 4/4"}, "kanban columns carry their own result; a column with no checkbox values stays silent");

    // ---------- View: the Customize "Calculations" add flow ----------
    QJsonObject plain = stripSpec;
    plain.remove("calculations");
    view.setScope("book", plain);
    QTest::qWait(30);
    pass &= check(!view.findChild<QLabel *>("viewCalculations"), "no configured calculations, no strip");
    QAction *addAction = nullptr, *listedAction = nullptr;
    auto findMenuActions = [&view, &addAction, &listedAction] {
        addAction = listedAction = nullptr;
        auto *sortTool = view.findChild<QToolButton *>("viewSortButton");
        if (!sortTool || !sortTool->menu()) return;
        for (auto *action : sortTool->menu()->actions()) {
            if (action->menu() && action->text() == "Calculations") {
                for (auto *sub : action->menu()->actions()) {
                    if (sub->text() == "Add calculation…") addAction = sub;
                    if (sub->text().startsWith("Sum · ")) listedAction = sub;
                }
            }
        }
    };
    findMenuActions();
    pass &= check(addAction, "the customize menu carries the Calculations submenu");
    QTimer::singleShot(30, &view, [&] {
        auto *dialog = view.findChild<QDialog *>("calculationDialog");
        if (!dialog) return;
        auto *property = dialog->findChild<QComboBox *>("calculationProperty");
        auto *op = dialog->findChild<QComboBox *>("calculationOp");
        if (!property || !op) return;
        property->setCurrentIndex(qMax(0, property->findData("properties.pages")));
        op->setCurrentIndex(qMax(0, op->findData("sum")));
        if (auto *buttons = dialog->findChild<QDialogButtonBox *>())
            buttons->button(QDialogButtonBox::Ok)->click();
    });
    if (addAction) addAction->trigger();
    QTest::qWait(30);
    pass &= check(view.viewSpec().value("calculations").toArray() == QJsonArray{calc("properties.pages", "sum")}, "the add flow persists the calculation in the view spec");
    strip = view.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 878", "the added calculation renders immediately");

    // The picker only offers ops that fit the property type.
    QTimer::singleShot(30, &view, [&] {
        auto *dialog = view.findChild<QDialog *>("calculationDialog");
        if (!dialog) return;
        auto *property = dialog->findChild<QComboBox *>("calculationProperty");
        auto *op = dialog->findChild<QComboBox *>("calculationOp");
        if (!property || !op) return;
        QStringList offered;
        auto collect = [&] {
            offered.clear();
            for (int i = 0; i < op->count(); ++i) offered << op->itemData(i).toString();
        };
        property->setCurrentIndex(qMax(0, property->findData("properties.author")));
        collect();
        pass &= check(offered == QStringList{"count_filled", "count_empty"}, "a text property only offers the count ops in the dialog");
        property->setCurrentIndex(qMax(0, property->findData("properties.published")));
        collect();
        pass &= check(offered == QStringList{"earliest", "latest", "range", "count_filled", "count_empty"}, "a date property offers the date ops, never sum");
        if (auto *buttons = dialog->findChild<QDialogButtonBox *>())
            buttons->button(QDialogButtonBox::Cancel)->click();
    });
    findMenuActions();
    if (addAction) addAction->trigger();
    QTest::qWait(30);
    pass &= check(view.viewSpec().value("calculations").toArray() == QJsonArray{calc("properties.pages", "sum")}, "cancelling the picker leaves the spec untouched");

    // ---------- View: persistence round trip ----------
    const QJsonObject persisted = view.viewSpec();
    view.setScope("book", persisted);
    QTest::qWait(30);
    strip = view.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 878", "the spec round trip keeps the calculation rendering");
    LumenViews restored;
    restored.setTheme(true, false);
    restored.resize(980, 700);
    restored.setData(objects, types);
    restored.setScope("book", persisted);
    restored.show();
    QTest::qWait(30);
    strip = restored.findChild<QLabel *>("viewCalculations");
    pass &= check(strip && strip->text() == "Sum (pages): 878", "a fresh view restores calculations from the persisted spec");
    restored.setPresentation("table");
    QTest::qWait(30);
    table = restored.findChild<QTableView *>("objectsTable");
    pass &= check(table && table->model()->rowCount() == 8 && table->model()->index(7, 1).data(Qt::DisplayRole).toString() == "878", "the restored footer lands under the right column");

    // ---------- View: empty view shows nothing ----------
    if (search) search->setText("no-such-result");
    QTest::qWait(30);
    pass &= check(!view.findChild<QLabel *>("viewCalculations") && view.findChild<QLabel *>("viewEmpty"), "an empty view shows the empty state, not a zero summary");
    pass &= check(view.viewSpec().value("calculations").toArray() == QJsonArray{calc("properties.pages", "sum")}, "the calculation config survives an empty result");
    if (search) search->clear();
    QTest::qWait(30);
    pass &= check(view.findChild<QLabel *>("viewCalculations"), "the strip returns with the objects");

    // ---------- View: edit and remove an existing calculation ----------
    QTimer::singleShot(30, &view, [&] {
        auto *dialog = view.findChild<QDialog *>("calculationDialog");
        if (!dialog) return;
        if (auto *remove = dialog->findChild<QPushButton *>("calculationRemove")) remove->click();
    });
    findMenuActions();
    pass &= check(listedAction && listedAction->text() == "Sum · pages", "existing calculations are listed in the submenu");
    if (listedAction) listedAction->trigger();
    QTest::qWait(30);
    pass &= check(!view.viewSpec().contains("calculations") && !view.findChild<QLabel *>("viewCalculations"), "removing the calculation clears the spec and the rendering");
    findMenuActions();
    pass &= check(addAction && !listedAction, "the submenu lists nothing once every calculation is removed");

    if (pass)
        fprintf(stdout, "LUMEN_CALCULATIONS_OK: op-per-type engine, number/date/checkbox/count ops, table footer, summary strip, per-group results, customize add/edit/remove, persisted spec, live recalculation, empty view\n");
    return pass ? 0 : 1;
}
