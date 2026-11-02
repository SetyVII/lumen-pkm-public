#include "lumen_clipboard.hpp"
#include "lumen_document.hpp"
#include "lumen_editor.hpp"
#include <QUuid>
#include <algorithm>
#include <memory>

namespace {
constexpr int NativeFrameProperty = QTextFormat::UserProperty + 712;
constexpr int NativeBlockProperty = QTextFormat::UserProperty + 714;
constexpr int NativeRunProperty = QTextFormat::UserProperty + 715;
QString fresh() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QJsonObject locate(const QJsonArray &nodes, const QString &id, const QString &parent = {}) {
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes[i].toObject();
        if (node.value("id") == id)
            return {{"node", node}, {"parent_id", parent}, {"position", i}};
        const auto nested = locate(node.value("children").toArray(), id, node.value("id").toString());
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}
QJsonArray selectedContent(QTextDocument *source, const QTextBlock &block, int from, int to) {
    if (from >= to)
        return {};
    QTextCursor selection(source);
    selection.setPosition(from);
    selection.setPosition(to, QTextCursor::KeepAnchor);
    RichEditor temporary;
    QTextCursor copy(temporary.document());
    copy.setBlockFormat(block.blockFormat());
    copy.insertFragment(QTextDocumentFragment(selection));
    const auto nodes = temporary.canonicalDocument().value("blocks").toArray();
    return nodes.isEmpty() ? QJsonArray() : nodes.first().toObject().value("content").toArray();
}
QString lastLeaf(const QJsonObject &node) {
    const auto children = node.value("children").toArray();
    return children.isEmpty() ? node.value("id").toString() : lastLeaf(children.last().toObject());
}
} // namespace

QMimeData *RichEditor::createMimeDataFromSelection() const {
    const std::unique_ptr<QMimeData> original(QTextEdit::createMimeDataFromSelection());
    auto *mime = new QMimeData;
    for (const auto &format : original->formats())
        mime->setData(format, original->data(format));
    const auto selection = textCursor();
    if (!selection.hasSelection())
        return mime;
    const auto current = canonicalDocument();
    if (selection.selectionStart() == 0 && selection.selectionEnd() == document()->characterCount() - 1) {
        mime->setData(
            LumenClipboard::Mime,
            QJsonDocument(LumenClipboard::frame(current, documentObjectId_)).toJson(QJsonDocument::Compact));
        return mime;
    }
    QJsonObject selectedFrame;
    std::function<void(QTextFrame *)> findFrame = [&](QTextFrame *frame) {
        if (frame != document()->rootFrame() && selection.selectionStart() == frame->firstPosition() - 1 &&
            selection.selectionEnd() == frame->lastPosition() + 1) {
            const auto node =
                QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray())
                    .object()
                    .value("canonical_node")
                    .toObject();
            selectedFrame = locate(current.value("blocks").toArray(), node.value("id").toString())
                                .value("node")
                                .toObject();
        }
        for (auto *child : frame->childFrames())
            findFrame(child);
    };
    findFrame(document()->rootFrame());
    if (!selectedFrame.isEmpty()) {
        mime->setData(LumenClipboard::Mime,
                      QJsonDocument(LumenClipboard::frame(QJsonObject{{"schema_version", 2},
                                                                      {"blocks", QJsonArray{selectedFrame}}},
                                                          documentObjectId_))
                          .toJson(QJsonDocument::Compact));
        return mime;
    }
    if (selection.hasComplexSelection() && selection.currentTable()) {
        const auto *table = selection.currentTable();
        const auto source =
            QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray())
                .object()
                .value("canonical_node")
                .toObject();
        auto clipped = locate(canonicalDocument().value("blocks").toArray(), source.value("id").toString())
                           .value("node")
                           .toObject();
        if (clipped.value("type") == "table") {
            int firstRow, rows, firstColumn, columns;
            selection.selectedTableCells(&firstRow, &rows, &firstColumn, &columns);
            auto attrs = clipped.value("attrs").toObject();
            const auto definitions = attrs.value("columns").toArray();
            QJsonArray selectedDefinitions;
            for (int c = firstColumn; c < firstColumn + columns; ++c)
                selectedDefinitions.append(definitions[c]);
            attrs["columns"] = selectedDefinitions;
            clipped["attrs"] = attrs;
            const auto allRows = clipped.value("children").toArray();
            QJsonArray selectedRows;
            for (int r = firstRow; r < firstRow + rows; ++r) {
                auto row = allRows[r].toObject();
                const auto cells = row.value("children").toArray();
                QJsonArray selectedCells;
                for (int c = firstColumn; c < firstColumn + columns; ++c)
                    selectedCells.append(cells[c]);
                row["children"] = selectedCells;
                selectedRows.append(row);
            }
            clipped["children"] = selectedRows;
            const auto selected = QJsonObject{{"schema_version", 2}, {"blocks", QJsonArray{clipped}}};
            QString error;
            if (LumenClipboard::validate(selected, &error))
                mime->setData(LumenClipboard::Mime,
                              QJsonDocument(LumenClipboard::frame(selected, documentObjectId_))
                                  .toJson(QJsonDocument::Compact));
            return mime;
        }
    }
    const auto first = document()->findBlock(selection.selectionStart());
    const auto last = document()->findBlock(qMax(selection.selectionStart(), selection.selectionEnd() - 1));
    // Partial text uses the standard rich-text clipboard. Full block selections
    // carry the authoritative tree, with no Qt formatting state on the wire.
    if (selection.selectionStart() != first.position() ||
        selection.selectionEnd() < last.position() + last.length() - 1) {
        return mime;
    }
    RichEditor temporary;
    QTextCursor copy(temporary.document());
    copy.setBlockFormat(first.blockFormat());
    copy.insertFragment(QTextDocumentFragment(selection));
    const auto selected = temporary.canonicalDocument();
    QString error;
    if (!LumenClipboard::validate(selected, &error))
        qWarning() << "Clipboard selection is not a complete canonical subtree:" << error;
    if (LumenClipboard::validate(selected, &error))
        mime->setData(
            LumenClipboard::Mime,
            QJsonDocument(LumenClipboard::frame(selected, documentObjectId_)).toJson(QJsonDocument::Compact));
    return mime;
}

bool RichEditor::pasteCanonicalDocument(const QJsonObject &incoming, QString *error) {
    if (isReadOnly())
        return false;
    if (!LumenClipboard::validate(incoming, error))
        return false;
    const auto additions = incoming.value("blocks").toArray();
    if (additions.isEmpty())
        return true;
    auto transaction = textCursor();
    transaction.beginEditBlock();
    const bool removedSelection = transaction.hasSelection();
    if (removedSelection) {
        transaction.removeSelectedText();
        setTextCursor(transaction);
    }
    const auto current = canonicalDocument();
    const auto nodes = current.value("blocks").toArray();
    const auto block = textCursor().block();
    const auto where = locate(nodes, blockId(block));
    QJsonArray commands;
    int position = 0;
    QString parent;
    bool movedOutsideTable = false;
    if (!where.isEmpty()) {
        const auto container = locate(nodes, where.value("parent_id").toString()).value("node").toObject();
        const bool needsBlockContainer =
            std::any_of(additions.begin(), additions.end(), [](const QJsonValue &v) {
                return v.toObject().value("type") != "paragraph";
            });
        if (container.value("type") == "table_cell" && needsBlockContainer) {
            if (auto *table = textCursor().currentTable()) {
                const auto meta =
                    QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray())
                        .object()
                        .value("canonical_node")
                        .toObject();
                const auto tableWhere = locate(nodes, meta.value("id").toString());
                parent = tableWhere.value("parent_id").toString();
                position = tableWhere.value("position").toInt() + 1;
                movedOutsideTable = true;
            }
        }
    }
    if (!where.isEmpty() && !movedOutsideTable) {
        const auto original = where.value("node").toObject();
        const auto kind = original.value("type").toString();
        position = where.value("position").toInt();
        parent = where.value("parent_id").toString();
        const bool atomic = kind == "image" || kind == "math" || kind == "divider" || kind == "object_embed";
        if (atomic)
            ++position;
        else {
            const auto caret = textCursor().position();
            auto prefix = original;
            prefix["content"] = selectedContent(document(), block, block.position(), caret);
            auto suffix = original;
            suffix["content"] =
                selectedContent(document(), block, caret, block.position() + block.length() - 1);
            const bool before = !prefix.value("content").toArray().isEmpty();
            const bool after = !suffix.value("content").toArray().isEmpty();
            const bool hasChildren = !original.value("children").toArray().isEmpty();
            if (before) {
                commands.append(
                    QJsonObject{{"type", "replace"}, {"node_id", original.value("id")}, {"node", prefix}});
                ++position;
            } else if (after || hasChildren) {
                commands.append(
                    QJsonObject{{"type", "replace"}, {"node_id", original.value("id")}, {"node", suffix}});
            } else
                commands.append(QJsonObject{{"type", "delete"}, {"node_id", original.value("id")}});
            if (before && after) {
                suffix["id"] = fresh();
                suffix.remove("children");
                commands.append(QJsonObject{
                    {"type", "insert"}, {"node", suffix}, {"parent_id", parent}, {"position", position}});
            }
        }
    }
    for (int i = 0; i < additions.size(); ++i)
        commands.append(QJsonObject{
            {"type", "insert"}, {"node", additions[i]}, {"parent_id", parent}, {"position", position + i}});
    const bool applied = applyCanonicalCommands(commands, error);
    transaction.endEditBlock();
    if (!applied) {
        if (removedSelection)
            undo();
        return false;
    }
    const auto last = lastLeaf(additions.last().toObject());
    if (focusBlock(last))
        moveCursor(QTextCursor::EndOfBlock);
    setCurrentCharFormat(QTextCharFormat());
    return true;
}

bool RichEditor::pasteStructuredMime(const QMimeData *source) {
    QJsonObject document;
    QString error;
    if (!LumenClipboard::prepare(source->data(LumenClipboard::Mime), documentObjectId_, &document, &error) ||
        !pasteCanonicalDocument(document, &error)) {
        QToolTip::showText(viewport()->mapToGlobal(cursorRect().bottomLeft()), "No se pegó: " + error, this);
    }
    return true; // An invalid structured payload never silently degrades to text.
}

bool RichEditor::pasteNormalizedHtml(const QString &html) {
    RichEditor temporary;
    temporary.setReferenceData(objects_, types_);
    temporary.setHtml(html);
    // HTML does not prove that an image has been incorporated into the library.
    // Importing its markup must never create an asset-less image node or fetch
    // external network/file resources from QTextDocument.
    QList<QTextCursor> unsupported;
    for (auto block = temporary.document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
            if (it.fragment().charFormat().isImageFormat()) {
                const auto fragment = it.fragment();
                auto cursor = QTextCursor(temporary.document());
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                unsupported.prepend(cursor);
            }
    for (auto &image : unsupported)
        image.insertText("[Imagen: incorporar archivo]", QTextCharFormat());
    std::function<void(QTextFrame *)> tables = [&](QTextFrame *frame) {
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            QJsonArray definitions;
            const auto widths = table->format().columnWidthConstraints();
            for (int i = 0; i < table->columns(); ++i)
                definitions.append(
                    QJsonObject{{"id", fresh()},
                                {"width", i < widths.size() && widths[i].type() == QTextLength::FixedLength &&
                                                  widths[i].rawValue() > 0
                                              ? widths[i].rawValue()
                                              : 160}});
            auto format = table->format();
            format.setProperty(
                NativeFrameProperty,
                QJsonDocument(QJsonObject{{"type", "table"},
                                          {"id", fresh()},
                                          {"canonical_node",
                                           QJsonObject{{"id", fresh()},
                                                       {"type", "table"},
                                                       {"attrs", QJsonObject{{"columns", definitions}}}}}})
                    .toJson(QJsonDocument::Compact));
            table->setFormat(format);
        }
        for (auto *child : frame->childFrames())
            tables(child);
    };
    tables(temporary.document()->rootFrame());
    auto normalized = temporary.canonicalDocument();
    QString error;
    if (!LumenClipboard::validate(normalized, &error)) {
        QToolTip::showText(QCursor::pos(), "No se pegó: " + error, this);
        return true;
    }
    const auto blocks = normalized.value("blocks").toArray();
    const auto native =
        QJsonDocument::fromJson(textCursor().blockFormat().property(NativeBlockProperty).toByteArray())
            .object()
            .value("canonical_node")
            .toObject();
    const auto kind = native.value("type").toString();
    const bool atomic = kind == "image" || kind == "math" || kind == "object_embed" || kind == "divider";
    if (!atomic && blocks.size() == 1 && blocks.first().toObject().value("type") == "paragraph" &&
        !blocks.first().toObject().contains("children")) {
        QTextCursor selection(temporary.document());
        selection.select(QTextCursor::Document);
        auto insertion = textCursor();
        insertion.beginEditBlock();
        insertion.insertFragment(QTextDocumentFragment(selection));
        insertion.endEditBlock();
        setTextCursor(insertion);
        setCurrentCharFormat(QTextCharFormat());
        ensureBlockIds();
        return true;
    }
    if (!pasteCanonicalDocument(normalized, &error))
        QToolTip::showText(QCursor::pos(), "No se pegó: " + error, this);
    return true;
}

void RichEditor::pastePlainText(QString text) {
    if (isReadOnly())
        return;
    text.replace("\r\n", "\n").replace('\r', '\n');
    if (textCursor().blockFormat().property(LumenSyntax::BlockStyleProperty).toString() == "code") {
        text.replace('\n', QChar::LineSeparator);
        textCursor().insertText(text);
        return;
    }
    const auto node =
        QJsonDocument::fromJson(textCursor().blockFormat().property(NativeBlockProperty).toByteArray())
            .object()
            .value("canonical_node")
            .toObject();
    const auto type = node.value("type").toString();
    if (text.contains('\n') || type == "image" || type == "math" || type == "object_embed" ||
        type == "divider") {
        QString error;
        if (!pasteCanonicalDocument(LumenDocument::fromText(text), &error))
            QToolTip::showText(QCursor::pos(), error, this);
    } else {
        auto cursor = textCursor();
        cursor.insertText(text, QTextCharFormat());
        setTextCursor(cursor);
        setCurrentCharFormat(QTextCharFormat());
        ensureBlockIds();
    }
}
