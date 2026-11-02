#include "lumen_document.hpp"
#include "lumen_editor.hpp"
#include <QUuid>

namespace {
constexpr int NativeRunProperty = QTextFormat::UserProperty + 715;
QJsonObject location(const QJsonArray &nodes, const QString &id, const QString &parent = {}) {
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes[i].toObject();
        if (node.value("id") == id)
            return {{"parent_id", parent}, {"position", i}};
        const auto nested = location(node.value("children").toArray(), id, node.value("id").toString());
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}
} // namespace
void RichEditor::insertBlockReference(const QJsonObject &hit, bool asBlock) {
    const auto block = hit.value("block").toObject();
    const auto objectID = hit.value("object_id").toString(), blockID = block.value("id").toString();
    if (isReadOnly() || objectID.isEmpty() || blockID.isEmpty())
        return;
    auto label = block.value("text").toString();
    if (label.isEmpty())
        label = hit.value("object_title").toString();
    if (label.isEmpty())
        label = blockID;
    label.replace('\n', QChar::LineSeparator);
    label.remove(QChar::ObjectReplacementCharacter);
    if (asBlock && textCursor().block().text().isEmpty()) {
        const auto canonical = canonicalDocument();
        const bool emptyDocument = canonical.value("blocks").toArray().isEmpty();
        const auto identity = emptyDocument ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                            : blockId(textCursor().block());
        auto node = QJsonObject{{"id", identity},
                                {"type", "object_embed"},
                                {"attrs", QJsonObject{{"object_id", objectID},
                                                      {"block_id", blockID},
                                                      {"view", "embed"},
                                                      {"label", label},
                                                      {"origin_title", hit.value("object_title")}}}};
        const auto command = QJsonObject{{"type", emptyDocument ? "insert" : "replace"},
                                         {"node_id", identity},
                                         {"node", node},
                                         {"position", 0}};
        const auto where = location(canonical.value("blocks").toArray(), identity);
        const auto afterID = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto after = QJsonObject{{"id", afterID}, {"type", "paragraph"}, {"content", QJsonArray()}};
        applyCanonicalCommands(QJsonArray{
            command, QJsonObject{{"type", "insert"},
                                 {"node", after},
                                 {"parent_id", where.value("parent_id").toString()},
                                 {"position", emptyDocument ? 1 : where.value("position").toInt() + 1}}});
        focusBlock(afterID);
        return;
    }
    const auto reference =
        QJsonObject{{"type", "block_ref"}, {"object_id", objectID}, {"block_id", blockID}, {"label", label}};
    QTextCharFormat format;
    format.setAnchor(true);
    format.setAnchorHref("lumen://block/" + blockID + "?object_id=" + objectID);
    format.setForeground(QColor(dark_ ? "#d2b7f5" : "#7b589d"));
    format.setBackground(QColor(dark_ ? "#2d2638" : "#f2edf8"));
    format.setProperty(NativeRunProperty,
                       QJsonDocument(QJsonObject{{"canonical_inline", reference},
                                                 {"instance", QUuid::createUuid().toString()}})
                           .toJson(QJsonDocument::Compact));
    auto cursor = textCursor();
    cursor.beginEditBlock();
    cursor.insertText(label, format);
    cursor.endEditBlock();
    setTextCursor(cursor);
    setCurrentCharFormat(QTextCharFormat());
    ensureBlockIds();
}

void RichEditor::showBlockReferencePicker() {
    if (isReadOnly() || !blockSearchRequested)
        return;
    auto insertion = textCursor();
    if (insertion.positionInBlock() >= 2) {
        insertion.setPosition(insertion.position() - 2, QTextCursor::KeepAnchor);
        if (insertion.selectedText() != "((")
            insertion = textCursor();
    }
    const auto expectedGeneration = contentGeneration_;
    QDialog popup(this, Qt::Popup);
    popup.setObjectName("blockReferencePicker");
    auto *layout = new QVBoxLayout(&popup);
    auto *query = new QLineEdit(&popup);
    query->setPlaceholderText("Busca cualquier bloque para vincularlo");
    query->setObjectName("blockReferenceQuery");
    layout->addWidget(query);
    auto *results = new QListWidget(&popup);
    results->setObjectName("blockReferenceResults");
    results->setMinimumSize(460, 260);
    layout->addWidget(results);
    auto *embed = new QCheckBox("Incrustar bloque", &popup);
    embed->setChecked(textCursor().block().text().remove("((").trimmed().isEmpty());
    layout->addWidget(embed);
    layout->addWidget(new QLabel("↑↓ para navegar · Esc para cancelar · Enter para seleccionar", &popup));
    auto *timer = new QTimer(&popup);
    timer->setSingleShot(true);
    timer->setInterval(150);
    quint64 serial = 0;
    auto search = [&] {
        const auto request = ++serial;
        popup.setProperty("request", QVariant::fromValue(request));
        QPointer<QDialog> guard(&popup);
        QPointer<QListWidget> list(results);
        results->clear();
        auto *waiting = new QListWidgetItem("Buscando bloques…", results);
        waiting->setFlags(Qt::NoItemFlags);
        blockSearchRequested(query->text(), [guard, list, request](QJsonArray hits) {
            if (!guard || !list || guard->property("request").toULongLong() != request)
                return;
            list->clear();
            for (const auto &value : hits) {
                const auto hit = value.toObject();
                auto text = hit.value("block").toObject().value("text").toString().simplified();
                if (text.size() > 130)
                    text = text.left(127) + "…";
                auto *item = new QListWidgetItem(text + "  ·  " + hit.value("object_title").toString(), list);
                item->setData(Qt::UserRole, hit);
                item->setToolTip(hit.value("block").toObject().value("text").toString());
            }
            if (list->count())
                list->setCurrentRow(0);
            else {
                auto *empty = new QListWidgetItem("No hay coincidencias", list);
                empty->setFlags(Qt::NoItemFlags);
            }
        });
    };
    // Set the request token before invoking the callback: fake/local sources may answer synchronously.
    connect(timer, &QTimer::timeout, &popup, search);
    connect(query, &QLineEdit::textChanged, timer, [&] {
        popup.setProperty("request", QVariant::fromValue(++serial));
        results->clear();
        timer->start();
    });
    auto accept = [&] {
        auto *item = results->currentItem();
        if (!item || !(item->flags() & Qt::ItemIsEnabled))
            return;
        if (contentGeneration_ != expectedGeneration) {
            popup.reject();
            return;
        }
        insertion.beginEditBlock();
        insertion.removeSelectedText();
        setTextCursor(insertion);
        insertBlockReference(item->data(Qt::UserRole).value<QJsonObject>(), embed->isChecked());
        insertion.endEditBlock();
        popup.accept();
    };
    connect(query, &QLineEdit::returnPressed, &popup, accept);
    connect(results, &QListWidget::itemActivated, &popup, [accept](QListWidgetItem *) {
        accept();
    });
    for (const auto key : {Qt::Key_Up, Qt::Key_Down}) {
        auto *navigate = new QAction(&popup);
        navigate->setShortcut(QKeySequence(key));
        navigate->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        popup.addAction(navigate);
        connect(navigate, &QAction::triggered, &popup, [results, key] {
            if (results->count())
                results->setCurrentRow(
                    qBound(0, results->currentRow() + (key == Qt::Key_Down ? 1 : -1), results->count() - 1));
        });
    }
    styleLayer(&popup);
    search();
    popup.adjustSize();
    const auto available = screen()->availableGeometry();
    const auto point = viewport()->mapToGlobal(cursorRect(insertion).bottomLeft());
    popup.move(
        qBound(available.left(), point.x(), qMax(available.left(), available.right() - popup.width())),
        qBound(available.top(), point.y(), qMax(available.top(), available.bottom() - popup.height())));
    query->setFocus();
    popup.exec();
    setFocus();
}
