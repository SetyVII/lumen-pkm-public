#include "lumen_table_controls.hpp"
#include "lumen_editor.hpp"
#include <algorithm>
#include <functional>
#include <limits>

namespace {
constexpr int NativeFrameProperty = QTextFormat::UserProperty + 712;
QString freshID() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QJsonObject findNode(const QJsonArray &nodes, const QString &id) {
    for (const auto &value : nodes) {
        const auto node = value.toObject();
        if (node.value("id").toString() == id)
            return node;
        const auto child = findNode(node.value("children").toArray(), id);
        if (!child.isEmpty())
            return child;
    }
    return {};
}
QString tableID(QTextTable *table) {
    if (!table)
        return {};
    const auto node = QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray())
                          .object()
                          .value("canonical_node")
                          .toObject();
    return node.value("type") == "table" ? node.value("id").toString() : QString{};
}

class TableControls final : public QObject {
    RichEditor *editor_;
    QToolButton *rowGrip_, *columnGrip_, *tableGrip_;
    QToolBar *toolbar_;
    QComboBox *scope_;
    QFrame *indicator_;
    QPointer<QTextTable> table_;
    QString identity_;
    QPoint hover_, pressed_;
    int row_ = 0, column_ = 0, firstRow_ = 0, rowCount_ = 1, firstColumn_ = 0, columnCount_ = 1;
    enum Axis { None, Row, Column } axis_ = None;
    int dragSource_ = 0, dropPosition_ = 0;
    bool dragged_ = false, selectionSet_ = false, busy_ = false;
    QTimer refresh_;
    QHash<QString, QAction *> marks_;
    mutable QJsonObject tableSnapshot_;
    mutable int snapshotRevision_ = -1;
    mutable QString snapshotIdentity_;

    QJsonObject canonicalTable() const {
        // Cache one active table only, invalidated on every document edit.
        // Pointer hover/scroll must not serialize a large note on every frame.
        const auto revision = editor_->document()->revision();
        if (snapshotRevision_ != revision || snapshotIdentity_ != identity_) {
            tableSnapshot_ = findNode(editor_->canonicalDocument().value("blocks").toArray(), identity_);
            snapshotRevision_ = revision;
            snapshotIdentity_ = identity_;
        }
        return tableSnapshot_;
    }
    QTextTable *findTable(const QString &id) const {
        QTextTable *result = nullptr;
        std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
            if (auto *table = qobject_cast<QTextTable *>(frame); table && tableID(table) == id)
                result = table;
            for (auto *child : frame->childFrames())
                visit(child);
        };
        visit(editor_->document()->rootFrame());
        return result;
    }
    QRectF tableRect() const {
        if (!table_)
            return {};
        return editor_->document()->documentLayout()->frameBoundingRect(table_).translated(
            -editor_->horizontalScrollBar()->value(), -editor_->verticalScrollBar()->value());
    }
    QVector<qreal> rowEdges() const {
        QVector<qreal> edges;
        if (!table_)
            return edges;
        const auto rect = tableRect();
        edges.append(rect.top());
        for (int row = 1; row < table_->rows(); ++row) {
            int top = std::numeric_limits<int>::max();
            for (int col = 0; col < table_->columns(); ++col)
                top = qMin(top, editor_->cursorRect(table_->cellAt(row, col).firstCursorPosition()).top());
            edges.append(top - table_->format().cellPadding());
        }
        edges.append(rect.bottom());
        return edges;
    }
    QVector<qreal> columnEdges() const {
        QVector<qreal> edges;
        if (!table_)
            return edges;
        const auto rect = tableRect();
        const auto widths = table_->format().columnWidthConstraints();
        qreal total = 0;
        for (const auto &width : widths)
            total += width.rawValue();
        if (widths.size() != table_->columns() || total <= 0) {
            for (int col = 0; col <= table_->columns(); ++col)
                edges.append(rect.left() + rect.width() * col / table_->columns());
        } else {
            qreal before = 0;
            edges.append(rect.left());
            for (const auto &width : widths) {
                before += width.rawValue();
                edges.append(rect.left() + rect.width() * before / total);
            }
        }
        return edges;
    }
    void hideGrips() {
        rowGrip_->hide();
        columnGrip_->hide();
        tableGrip_->hide();
    }
    void setActive(bool active) {
        editor_->setProperty("lumenTableSelectionActive", active);
        if (!active)
            toolbar_->hide();
    }
    void refresh() {
        if (busy_ || axis_ != None)
            return;
        if (editor_->isReadOnly()) {
            hideGrips();
            setActive(false);
            return;
        }
        if (selectionSet_)
            table_ = findTable(identity_);
        else {
            auto cursor = editor_->cursorForPosition(hover_);
            auto *candidate = cursor.currentTable();
            if (!tableID(candidate).isEmpty() &&
                tableRectFor(candidate).adjusted(-26, -24, 4, 4).contains(hover_)) {
                table_ = candidate;
                identity_ = tableID(candidate);
                const auto cell = table_->cellAt(cursor);
                row_ = cell.row();
                column_ = cell.column();
            } else {
                table_.clear();
                hideGrips();
                setActive(false);
                return;
            }
        }
        if (!table_ || table_->rows() == 0 || table_->columns() == 0) {
            hideGrips();
            setActive(false);
            return;
        }
        row_ = qBound(0, row_, table_->rows() - 1);
        column_ = qBound(0, column_, table_->columns() - 1);
        const auto rect = tableRect();
        if (!rect.intersects(editor_->viewport()->rect())) {
            hideGrips();
            toolbar_->hide();
            return;
        }
        const auto rows = rowEdges(), columns = columnEdges();
        rowGrip_->move(qRound(rect.left()) - 24, qRound((rows[row_] + rows[row_ + 1]) / 2) - 10);
        columnGrip_->move(qRound((columns[column_] + columns[column_ + 1]) / 2) - 13,
                          qRound(rect.top()) - 21);
        tableGrip_->move(qRound(rect.left()) - 24, qRound(rect.top()) - 21);
        for (auto *grip : {rowGrip_, columnGrip_, tableGrip_}) {
            grip->show();
            grip->raise();
        }
        if (auto *blockGrip = editor_->findChild<QToolButton *>("blockHandle"))
            blockGrip->hide();
        if (selectionSet_) {
            toolbar_->setMaximumWidth(qMax(180, editor_->viewport()->width() - 16));
            toolbar_->adjustSize();
            toolbar_->move(8, qBound(0, qRound(rect.bottom()) + 6,
                                     qMax(0, editor_->viewport()->height() - toolbar_->height() - 8)));
            toolbar_->show();
            toolbar_->raise();
            const auto node = canonicalTable();
            for (auto it = marks_.begin(); it != marks_.end(); ++it)
                it.value()->setChecked(allMarked(it.key(), node));
        }
    }
    QRectF tableRectFor(QTextTable *table) const {
        return editor_->document()->documentLayout()->frameBoundingRect(table).translated(
            -editor_->horizontalScrollBar()->value(), -editor_->verticalScrollBar()->value());
    }
    void nativeSelectionChanged() {
        if (busy_)
            return;
        const auto cursor = editor_->textCursor();
        auto *table = cursor.currentTable();
        if (table && !tableID(table).isEmpty() && cursor.hasComplexSelection()) {
            int row, rows, col, cols;
            cursor.selectedTableCells(&row, &rows, &col, &cols);
            if (row >= 0 && rows > 0 && col >= 0 && cols > 0) {
                table_ = table;
                identity_ = tableID(table);
                row_ = row;
                column_ = col;
                firstRow_ = row;
                rowCount_ = rows;
                firstColumn_ = col;
                columnCount_ = cols;
                selectionSet_ = true;
                setActive(true);
                scope_->setCurrentIndex(rows == table->rows() && cols == table->columns() ? 3
                                        : cols == table->columns()                        ? 1
                                        : rows == table->rows()                           ? 2
                                                                                          : 4);
            }
        } else if (!selectionSet_)
            setActive(false);
        refresh_.start();
    }
    void selectRange(int row, int rows, int column, int columns) {
        table_ = findTable(identity_);
        if (!table_)
            return;
        firstRow_ = qBound(0, row, table_->rows() - 1);
        rowCount_ = qMin(rows, table_->rows() - firstRow_);
        firstColumn_ = qBound(0, column, table_->columns() - 1);
        columnCount_ = qMin(columns, table_->columns() - firstColumn_);
        if (rowCount_ < 1 || columnCount_ < 1)
            return;
        busy_ = true;
        auto cursor = table_->cellAt(firstRow_, firstColumn_).firstCursorPosition();
        cursor.setPosition(table_->cellAt(firstRow_ + rowCount_ - 1, firstColumn_ + columnCount_ - 1)
                               .lastCursorPosition()
                               .position(),
                           QTextCursor::KeepAnchor);
        editor_->setTextCursor(cursor);
        busy_ = false;
        selectionSet_ = true;
        setActive(true);
        editor_->setFocus();
        refresh();
    }
    void selectScope(int scope) {
        if (!table_)
            return;
        if (scope == 0)
            selectRange(row_, 1, column_, 1);
        if (scope == 1)
            selectRange(row_, 1, 0, table_->columns());
        if (scope == 2)
            selectRange(0, table_->rows(), column_, 1);
        if (scope == 3)
            selectRange(0, table_->rows(), 0, table_->columns());
    }
    template <class F> void forSelectedParagraphs(QJsonObject &table, F callback) const {
        auto rows = table.value("children").toArray();
        for (int r = firstRow_; r < qMin(int(rows.size()), firstRow_ + rowCount_); ++r) {
            auto row = rows[r].toObject();
            auto cells = row.value("children").toArray();
            for (int c = firstColumn_; c < qMin(int(cells.size()), firstColumn_ + columnCount_); ++c) {
                auto cell = cells[c].toObject();
                auto paragraphs = cell.value("children").toArray();
                for (int p = 0; p < paragraphs.size(); ++p) {
                    auto paragraph = paragraphs[p].toObject();
                    callback(paragraph);
                    paragraphs[p] = paragraph;
                }
                cell["children"] = paragraphs;
                cells[c] = cell;
            }
            row["children"] = cells;
            rows[r] = row;
        }
        table["children"] = rows;
    }
    bool allMarked(const QString &mark, const QJsonObject &table) const {
        bool all = true, found = false;
        const auto rows = table.value("children").toArray();
        for (int r = firstRow_; r < qMin(int(rows.size()), firstRow_ + rowCount_); ++r) {
            const auto cells = rows[r].toObject().value("children").toArray();
            for (int c = firstColumn_; c < qMin(int(cells.size()), firstColumn_ + columnCount_); ++c)
                for (const auto &paragraph : cells[c].toObject().value("children").toArray()) {
                    const auto content = paragraph.toObject().value("content").toArray();
                    if (content.isEmpty())
                        all = false;
                    for (const auto &value : content) {
                        found = true;
                        if (!value.toObject().value("marks").toObject().value(mark).toBool())
                            all = false;
                    }
                }
        }
        return found && all;
    }
    bool run(QJsonObject command) {
        command["node_id"] = identity_;
        QString error;
        busy_ = true;
        const bool ok = editor_->applyCanonicalCommands(QJsonArray{command}, &error);
        busy_ = false;
        if (!ok) {
            toolbar_->setToolTip(error);
            return false;
        }
        table_ = findTable(identity_);
        if (table_)
            selectRange(firstRow_, rowCount_, firstColumn_, columnCount_);
        refresh_.start();
        return true;
    }
    void formatMark(const QString &mark, QJsonValue value = QJsonValue(QJsonValue::Undefined)) {
        auto node = canonicalTable();
        if (node.isEmpty())
            return;
        const bool enable = value.isUndefined() ? !allMarked(mark, node) : true;
        forSelectedParagraphs(node, [&](QJsonObject &p) {
            auto content = p.value("content").toArray();
            if (content.isEmpty())
                content.append(QJsonObject{{"type", "text"}, {"text", ""}});
            for (int i = 0; i < content.size(); ++i) {
                auto in = content[i].toObject();
                auto marks = in.value("marks").toObject();
                if (enable)
                    marks[mark] = value.isUndefined() ? QJsonValue(true) : value;
                else
                    marks.remove(mark);
                if (marks.isEmpty())
                    in.remove("marks");
                else
                    in["marks"] = marks;
                content[i] = in;
            }
            p["content"] = content;
        });
        run({{"type", "replace"}, {"node", node}});
    }
    void align(const QString &alignment) {
        auto node = canonicalTable();
        if (node.isEmpty())
            return;
        forSelectedParagraphs(node, [&](QJsonObject &p) {
            auto attrs = p.value("attrs").toObject();
            attrs["alignment"] = alignment;
            p["attrs"] = attrs;
        });
        run({{"type", "replace"}, {"node", node}});
    }
    void clearFormatting() {
        auto node = canonicalTable();
        if (node.isEmpty())
            return;
        forSelectedParagraphs(node, [](QJsonObject &p) {
            auto content = p.value("content").toArray();
            for (int i = 0; i < content.size(); ++i) {
                auto in = content[i].toObject();
                in.remove("marks");
                content[i] = in;
            }
            p["content"] = content;
            auto attrs = p.value("attrs").toObject();
            attrs.remove("alignment");
            if (attrs.isEmpty())
                p.remove("attrs");
            else
                p["attrs"] = attrs;
        });
        run({{"type", "replace"}, {"node", node}});
    }
    void startDrag(Axis axis, const QPoint &global) {
        axis_ = axis;
        pressed_ = global;
        dragged_ = false;
        dragSource_ = axis == Row ? row_ : column_;
        dropPosition_ = dragSource_;
        scope_->setCurrentIndex(axis == Row ? 1 : 2);
        selectScope(axis == Row ? 1 : 2);
        auto *grip = axis == Row ? rowGrip_ : columnGrip_;
        grip->setCursor(Qt::ClosedHandCursor);
    }
    void updateDrag(const QPoint &global) {
        if (axis_ == None || !table_)
            return;
        if ((global - pressed_).manhattanLength() < QApplication::startDragDistance() && !dragged_)
            return;
        dragged_ = true;
        const auto point = editor_->viewport()->mapFromGlobal(global);
        const auto edges = axis_ == Row ? rowEdges() : columnEdges();
        const qreal coordinate = axis_ == Row ? point.y() : point.x();
        int boundary = 0;
        while (boundary + 1 < edges.size() && coordinate > (edges[boundary] + edges[boundary + 1]) / 2)
            ++boundary;
        dropPosition_ = boundary > dragSource_ ? boundary - 1 : boundary;
        const auto rect = tableRect();
        if (axis_ == Row)
            indicator_->setGeometry(qRound(rect.left()), qRound(edges[boundary]) - 1, qRound(rect.width()),
                                    3);
        else
            indicator_->setGeometry(qRound(edges[boundary]) - 1, qRound(rect.top()), 3,
                                    qRound(rect.height()));
        indicator_->show();
        indicator_->raise();
    }
    void finishDrag() {
        const auto axis = axis_;
        axis_ = None;
        indicator_->hide();
        rowGrip_->setCursor(Qt::OpenHandCursor);
        columnGrip_->setCursor(Qt::OpenHandCursor);
        if (!dragged_ || !table_ || dropPosition_ == dragSource_) {
            refresh();
            return;
        }
        const auto node = canonicalTable();
        if (axis == Row) {
            const auto rows = node.value("children").toArray();
            if (dragSource_ < rows.size()) {
                firstRow_ = row_ = dropPosition_;
                rowCount_ = 1;
                run({{"type", "table_move_row"},
                     {"parent_id", rows[dragSource_].toObject().value("id")},
                     {"position", dropPosition_}});
            }
        } else {
            const auto cols = node.value("attrs").toObject().value("columns").toArray();
            if (dragSource_ < cols.size()) {
                firstColumn_ = column_ = dropPosition_;
                columnCount_ = 1;
                run({{"type", "table_move_column"},
                     {"column_id", cols[dragSource_].toObject().value("id")},
                     {"position", dropPosition_}});
            }
        }
    }
    void showMenu(QObject *watched, const QPoint &global) {
        const auto node = canonicalTable();
        if (node.isEmpty())
            return;
        const auto rows = node.value("children").toArray();
        const auto columns = node.value("attrs").toObject().value("columns").toArray();
        QMenu menu(editor_);
        auto add = [&](const QString &text, QJsonObject command, bool enabled = true) {
            auto *item = menu.addAction(text);
            item->setEnabled(enabled);
            connect(item, &QAction::triggered, this, [this, command] {
                run(command);
            });
        };
        if (watched == rowGrip_ && row_ < rows.size()) {
            auto makeRow = [&] {
                QJsonArray cells;
                for (int col = 0; col < columns.size(); ++col)
                    cells.append(
                        QJsonObject{{"id", freshID()},
                                    {"type", "table_cell"},
                                    {"children", QJsonArray{QJsonObject{{"id", freshID()},
                                                                        {"type", "paragraph"},
                                                                        {"content", QJsonArray{}}}}}});
                return QJsonObject{{"id", freshID()}, {"type", "table_row"}, {"children", cells}};
            };
            add("Insertar fila antes",
                {{"type", "table_insert_row"}, {"position", row_}, {"node", makeRow()}});
            add("Insertar fila después",
                {{"type", "table_insert_row"}, {"position", row_ + 1}, {"node", makeRow()}});
            const auto rowID = rows[row_].toObject().value("id");
            add("Mover fila arriba",
                {{"type", "table_move_row"}, {"parent_id", rowID}, {"position", row_ - 1}}, row_ > 0);
            add("Mover fila abajo",
                {{"type", "table_move_row"}, {"parent_id", rowID}, {"position", row_ + 1}},
                row_ + 1 < rows.size());
            add("Eliminar fila", {{"type", "table_delete_row"}, {"parent_id", rowID}}, rows.size() > 1);
        } else if (watched == columnGrip_ && column_ < columns.size()) {
            const auto columnID = columns[column_].toObject().value("id");
            add("Insertar columna antes", {{"type", "table_insert_column"},
                                           {"position", column_},
                                           {"column_id", freshID()},
                                           {"width", 160}});
            add("Insertar columna después", {{"type", "table_insert_column"},
                                             {"position", column_ + 1},
                                             {"column_id", freshID()},
                                             {"width", 160}});
            add("Restablecer ancho de columna",
                {{"type", "table_set_width"}, {"column_id", columnID}, {"width", 160}});
            add("Ordenar ascendente",
                {{"type", "table_sort"}, {"column_id", columnID}, {"direction", "asc"}});
            add("Ordenar descendente",
                {{"type", "table_sort"}, {"column_id", columnID}, {"direction", "desc"}});
            add("Eliminar columna", {{"type", "table_delete_column"}, {"column_id", columnID}},
                columns.size() > 1);
        }
        if (!menu.isEmpty())
            menu.exec(global);
    }
    QToolButton *grip(const QString &name, const QString &text, const QString &description, QSize size) {
        auto *button = new QToolButton(editor_->viewport());
        button->setObjectName(name);
        button->setText(text);
        button->setAccessibleName(description);
        button->setToolTip(description);
        button->setFixedSize(size);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::OpenHandCursor);
        button->setStyleSheet(
            "QToolButton{background:palette(button);color:palette(button-text);border:1px solid "
            "palette(mid);border-radius:4px}QToolButton:hover{background:palette(highlight);color:palette("
            "highlighted-text)}");
        button->installEventFilter(this);
        button->hide();
        return button;
    }
    QAction *action(const QString &name, const QString &label, const QString &tip,
                    std::function<void()> callback) {
        auto *item = new QAction(label, toolbar_);
        item->setObjectName(name);
        item->setToolTip(tip);
        connect(item, &QAction::triggered, this, std::move(callback));
        toolbar_->addAction(item);
        if (auto *button = qobject_cast<QToolButton *>(toolbar_->widgetForAction(item)))
            button->setAccessibleName(tip);
        return item;
    }

  public:
    explicit TableControls(RichEditor *editor) : QObject(editor), editor_(editor) {
        setObjectName("lumenTableControlsController");
        rowGrip_ = grip("lumenTableRowGrip", "⋮", "Seleccionar fila · arrastrar para mover", {20, 20});
        columnGrip_ =
            grip("lumenTableColumnGrip", "⋮⋮", "Seleccionar columna · arrastrar para mover", {26, 18});
        tableGrip_ = grip("lumenTableAllGrip", "▦", "Seleccionar tabla", {20, 18});
        toolbar_ = new QToolBar(editor_->viewport());
        toolbar_->setObjectName("lumenTableToolbar");
        toolbar_->setAccessibleName("Formato de tabla, fila, columna o celda");
        toolbar_->setMovable(false);
        toolbar_->setFloatable(false);
        toolbar_->setIconSize({16, 16});
        scope_ = new QComboBox(toolbar_);
        scope_->setObjectName("lumenTableScope");
        scope_->setAccessibleName("Ámbito del formato");
        scope_->addItems({"Celda", "Fila", "Columna", "Tabla", "Selección"});
        toolbar_->addWidget(scope_);
        connect(scope_, &QComboBox::activated, this, [this](int index) {
            selectScope(index);
        });
        for (const auto &entry : QList<QPair<QString, QString>>{
                 {"bold", "B"}, {"italic", "I"}, {"underline", "U"}, {"strike", "S"}, {"code", "<>"}}) {
            const QHash<QString, QString> names{{"bold", "Negrita"},
                                                {"italic", "Cursiva"},
                                                {"underline", "Subrayado"},
                                                {"strike", "Tachado"},
                                                {"code", "Código"}};
            auto *item = action("lumenTableMark-" + entry.first, entry.second, names.value(entry.first),
                                [this, mark = entry.first] {
                                    formatMark(mark);
                                });
            item->setCheckable(true);
            marks_[entry.first] = item;
        }
        toolbar_->addSeparator();
        action("lumenTableAlign-left", "←", "Alinear a la izquierda", [this] {
            align("left");
        });
        action("lumenTableAlign-center", "↔", "Centrar texto", [this] {
            align("center");
        });
        action("lumenTableAlign-right", "→", "Alinear a la derecha", [this] {
            align("right");
        });
        action("lumenTableHighlight", "Resaltar", "Resaltar texto", [this] {
            formatMark("highlight", "#fff59d");
        });
        action("lumenTableColor", "Color", "Color del texto", [this] {
            const auto color =
                QColorDialog::getColor(editor_->palette().color(QPalette::Text), editor_, "Color del texto");
            if (color.isValid())
                formatMark("color", color.name());
        });
        auto *size = new QComboBox(toolbar_);
        size->setObjectName("lumenTableTextSize");
        size->setAccessibleName("Tamaño del texto");
        size->addItems({"Tamaño", "12", "14", "16", "20", "24"});
        toolbar_->addWidget(size);
        connect(size, &QComboBox::activated, this, [this, size](int index) {
            if (index > 0)
                formatMark("point_size", size->itemText(index).toDouble());
        });
        action("lumenTableClearFormatting", "Limpiar", "Quitar formato de la selección", [this] {
            clearFormatting();
        });
        action("lumenTableClose", "×", "Cerrar formato de tabla", [this] {
            selectionSet_ = false;
            setActive(false);
            auto cursor = editor_->textCursor();
            cursor.clearSelection();
            editor_->setTextCursor(cursor);
        });
        toolbar_->hide();
        indicator_ = new QFrame(editor_->viewport());
        indicator_->setObjectName("lumenTableDropIndicator");
        indicator_->setStyleSheet("background:palette(highlight)");
        indicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
        indicator_->hide();
        refresh_.setSingleShot(true);
        refresh_.setInterval(0);
        connect(&refresh_, &QTimer::timeout, this, [this] {
            refresh();
        });
        editor_->viewport()->setMouseTracking(true);
        editor_->viewport()->installEventFilter(this);
        editor_->installEventFilter(this);
        connect(editor_, &QTextEdit::selectionChanged, this, [this] {
            nativeSelectionChanged();
        });
        connect(editor_, &QTextEdit::textChanged, this, [this] {
            snapshotRevision_ = -1;
            refresh_.start();
        });
        connect(editor_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            refresh_.start();
        });
        connect(editor_->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            refresh_.start();
        });
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::ReadOnlyChange) {
            axis_ = None;
            indicator_->hide();
            selectionSet_ = false;
            setActive(false);
            if (editor_->isReadOnly())
                hideGrips();
            else
                refresh_.start();
        }
        if (event->type() == QEvent::Resize)
            refresh_.start();
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (selectionSet_ && (key->matches(QKeySequence::Bold) || key->matches(QKeySequence::Italic) ||
                                  key->matches(QKeySequence::Underline))) {
                if (event->type() == QEvent::KeyPress)
                    formatMark(key->matches(QKeySequence::Bold)     ? "bold"
                               : key->matches(QKeySequence::Italic) ? "italic"
                                                                    : "underline");
                event->accept();
                return true;
            }
            if (event->type() == QEvent::KeyPress && selectionSet_) {
                selectionSet_ = false;
                setActive(false);
                if (key->key() == Qt::Key_Escape) {
                    auto cursor = editor_->textCursor();
                    cursor.clearSelection();
                    editor_->setTextCursor(cursor);
                    return true;
                }
            }
        }
        const bool handle = watched == rowGrip_ || watched == columnGrip_ || watched == tableGrip_;
        if (handle && event->type() == QEvent::ContextMenu) {
            showMenu(watched, static_cast<QContextMenuEvent *>(event)->globalPos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (handle && mouse->button() == Qt::LeftButton) {
                if (watched == tableGrip_) {
                    scope_->setCurrentIndex(3);
                    selectScope(3);
                } else
                    startDrag(watched == rowGrip_ ? Row : Column, mouse->globalPosition().toPoint());
                return true;
            }
            if (watched == editor_->viewport() && axis_ == None) {
                selectionSet_ = false;
                setActive(false);
            }
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (axis_ != None) {
                updateDrag(mouse->globalPosition().toPoint());
                return true;
            }
            if (watched == editor_->viewport()) {
                hover_ = mouse->position().toPoint();
                if (selectionSet_ && table_) {
                    const auto cursor = editor_->cursorForPosition(hover_);
                    if (cursor.currentTable() == table_) {
                        const auto cell = table_->cellAt(cursor);
                        row_ = cell.row();
                        column_ = cell.column();
                    }
                }
                refresh();
                refresh_.start();
            }
        }
        if (event->type() == QEvent::MouseButtonRelease && axis_ != None) {
            finishDrag();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};
} // namespace

void LumenTableControls::install(RichEditor *editor) {
    if (!editor || editor->findChild<QObject *>("lumenTableControlsController", Qt::FindDirectChildrenOnly))
        return;
    new TableControls(editor);
}
