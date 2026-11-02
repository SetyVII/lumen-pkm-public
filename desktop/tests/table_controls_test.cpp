#include "lumen_editor.hpp"
#include "lumen_table_controls.hpp"
#include <QtTest>
#include <stdexcept>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
QJsonObject sourceTable() {
    QJsonArray definitions, rows;
    const QList<int> widths{160, 130, 190};
    for (int c = 0; c < 3; ++c)
        definitions.append(QJsonObject{{"id", QString("column-%1").arg(c)}, {"width", widths[c]}});
    for (int r = 0; r < 2; ++r) {
        QJsonArray cells;
        for (int c = 0; c < 3; ++c) {
            const auto suffix = QString("%1-%2").arg(r).arg(c);
            QJsonObject p{{"id", "paragraph-" + suffix},
                          {"type", "paragraph"},
                          {"attrs", QJsonObject{{"unknown_paragraph_state", 17}}},
                          {"content", QJsonArray{QJsonObject{{"type", "text"},
                                                             {"text", QString("r%1c%2").arg(r).arg(c)}}}}};
            cells.append(QJsonObject{{"id", "cell-" + suffix},
                                     {"type", "table_cell"},
                                     {"attrs", QJsonObject{{"unknown_cell_state", true}}},
                                     {"children", QJsonArray{p}}});
        }
        rows.append(
            QJsonObject{{"id", QString("row-%1").arg(r)}, {"type", "table_row"}, {"children", cells}});
    }
    return {
        {"id", "controlled-table"},
        {"type", "table"},
        {"attrs", QJsonObject{{"columns", definitions}, {"future_table_state", QJsonObject{{"keep", true}}}}},
        {"children", rows}};
}
QJsonObject sourceDocument() {
    return {{"schema_version", 2}, {"blocks", QJsonArray{sourceTable()}}};
}
QTextTable *nativeTable(RichEditor &editor) {
    QTextTable *result = nullptr;
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        if (auto *table = qobject_cast<QTextTable *>(frame))
            result = table;
        for (auto *child : frame->childFrames())
            visit(child);
    };
    visit(editor.document()->rootFrame());
    return result;
}
QJsonObject table(RichEditor &editor) {
    return editor.canonicalDocument().value("blocks").toArray()[0].toObject();
}
QJsonObject paragraph(const QJsonObject &node, int row, int column) {
    return node.value("children")
        .toArray()[row]
        .toObject()
        .value("children")
        .toArray()[column]
        .toObject()
        .value("children")
        .toArray()[0]
        .toObject();
}
void hover(RichEditor &editor, int row = 0, int column = 0) {
    auto *t = nativeTable(editor);
    require(t, "native table exists");
    auto point = editor.cursorRect(t->cellAt(row, column).firstCursorPosition()).center() + QPoint(5, 0);
    QTest::mouseMove(editor.viewport(), point);
    QApplication::processEvents();
}
QToolButton *grip(RichEditor &editor, const char *name) {
    auto *button = editor.findChild<QToolButton *>(name);
    require(button && button->isVisible(), "visible table grip");
    return button;
}
void clickAction(RichEditor &editor, const char *name) {
    auto *bar = editor.findChild<QToolBar *>("lumenTableToolbar");
    auto *action = editor.findChild<QAction *>(name);
    require(bar && bar->isVisible() && action, "table format action exists");
    auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(action));
    require(button, "format action has native button");
    QTest::mouseClick(button, Qt::LeftButton);
    QApplication::processEvents();
}
void drag(RichEditor &editor, QToolButton *handle, QPoint destination) {
    QTest::mousePress(handle, Qt::LeftButton);
    QTest::mouseMove(editor.viewport(), destination, 20);
    QApplication::processEvents();
    auto *indicator = editor.findChild<QFrame *>("lumenTableDropIndicator");
    require(indicator && indicator->isVisible(), "drag shows insertion indicator");
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, destination);
    QApplication::processEvents();
}
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        RichEditor editor;
        editor.resize(860, 520);
        LumenTableControls::install(&editor);
        const auto original = sourceDocument();
        editor.setCanonicalDocument(original);
        editor.show();
        editor.setFocus();
        QApplication::processEvents();
        hover(editor);
        QTest::mouseClick(grip(editor, "lumenTableColumnGrip"), Qt::LeftButton);
        QApplication::processEvents();
        clickAction(editor, "lumenTableMark-bold");
        auto formatted = table(editor);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c) {
                const auto marks = paragraph(formatted, r, c)
                                       .value("content")
                                       .toArray()[0]
                                       .toObject()
                                       .value("marks")
                                       .toObject();
                require(marks.value("bold").toBool() == (c == 0), "column format respects exact scope");
            }
        require(formatted.value("attrs") == sourceTable().value("attrs"),
                "format preserves widths and unknown table attrs");
        editor.undo();
        QApplication::processEvents();
        require(editor.canonicalDocument() == original, "format is one undoable canonical edit");
        hover(editor);
        auto *native = nativeTable(editor);
        auto rect = editor.document()->documentLayout()->frameBoundingRect(native).translated(
            -editor.horizontalScrollBar()->value(), -editor.verticalScrollBar()->value());
        drag(editor, grip(editor, "lumenTableRowGrip"),
             QPoint(qRound(rect.center().x()), qRound(rect.bottom() + 8)));
        auto moved = table(editor);
        require(moved.value("children").toArray()[0].toObject().value("id") == "row-1",
                "drag moves row preserving identity");
        require(moved.value("children").toArray()[1].toObject().value("id") == "row-0",
                "drag retains every row");
        hover(editor);
        native = nativeTable(editor);
        rect = editor.document()->documentLayout()->frameBoundingRect(native).translated(
            -editor.horizontalScrollBar()->value(), -editor.verticalScrollBar()->value());
        drag(editor, grip(editor, "lumenTableColumnGrip"),
             QPoint(qRound(rect.right() + 8), qRound(rect.center().y())));
        moved = table(editor);
        auto columns = moved.value("attrs").toObject().value("columns").toArray();
        require(columns[0].toObject().value("id") == "column-1" &&
                    columns[2].toObject().value("id") == "column-0",
                "drag moves column definition");
        require(columns[2].toObject().value("width").toInt() == 160,
                "drag preserves individual column width");
        require(moved.value("children")
                        .toArray()[0]
                        .toObject()
                        .value("children")
                        .toArray()[2]
                        .toObject()
                        .value("id") == "cell-1-0",
                "column drag moves cells in every row");
        editor.undo();
        editor.undo();
        QApplication::processEvents();
        require(editor.canonicalDocument() == original, "row and column drag undo preserve complete table");
        hover(editor);
        QTest::mouseClick(grip(editor, "lumenTableAllGrip"), Qt::LeftButton);
        QApplication::processEvents();
        clickAction(editor, "lumenTableAlign-center");
        formatted = table(editor);
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c) {
                const auto attrs = paragraph(formatted, r, c).value("attrs").toObject();
                require(attrs.value("alignment") == "center" &&
                            attrs.value("unknown_paragraph_state").toInt() == 17,
                        "whole-table alignment retains unknown attrs");
            }
        editor.undo();
        QApplication::processEvents();
        require(editor.canonicalDocument() == original, "whole-table format undo is lossless");
        if (!qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR").isEmpty()) {
            hover(editor); QTest::mouseClick(grip(editor, "lumenTableAllGrip"), Qt::LeftButton);
            QApplication::processEvents();
            const auto directory = qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR"); QDir().mkpath(directory);
            require(editor.grab().save(directory + "/table-controls.png"), "save actual table controls capture");
        }
        editor.setReadOnly(true);
        hover(editor);
        require(!editor.findChild<QToolButton *>("lumenTableRowGrip")->isVisible(),
                "read-only mode hides mutation handles");
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
    qInfo() << "TABLE_CONTROLS_TEST_OK";
    return 0;
}
