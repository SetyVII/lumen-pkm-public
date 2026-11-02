#include "lumen_document.hpp"
#include "lumen_editor.hpp"
#include "lumen_clipboard.hpp"
#include <QtTest>
#include <stdexcept>
namespace {
class TestEditor : public RichEditor { public: using RichEditor::createMimeDataFromSelection; using RichEditor::insertFromMimeData; };
void require(bool yes, const char *message) {
    if (!yes)
        throw std::runtime_error(message);
}
QJsonObject paragraph(QString id, QString text) {
    return {{"id", id},
            {"type", "paragraph"},
            {"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}}}};
}
QJsonObject doc(QJsonArray blocks) {
    return {{"schema_version", 2}, {"blocks", blocks}};
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        RichEditor blank;
        blank.setCanonicalDocument(LumenDocument::empty());
        require(blank.canonicalDocument() == LumenDocument::empty(), "empty canonical document stays empty");
        const auto emptyColumn = QJsonObject{{"id", "empty-column"}, {"type", "column"}, {"attrs", QJsonObject{{"width", 1}}}, {"children", QJsonArray()}};
        const auto emptyCell = QJsonObject{{"id", "empty-cell"}, {"type", "table_cell"}, {"children", QJsonArray()}};
        const auto emptyContainers = doc(QJsonArray{
            QJsonObject{{"id", "empty-group"}, {"type", "group"}, {"children", QJsonArray()}},
            QJsonObject{{"id", "empty-toggle"}, {"type", "toggle"}, {"content", QJsonArray()}, {"attrs", QJsonObject{{"expanded", true}}}, {"children", QJsonArray()}},
            QJsonObject{{"id", "empty-columns"}, {"type", "columns"}, {"children", QJsonArray{emptyColumn}}},
            QJsonObject{{"id", "empty-table"}, {"type", "table"}, {"attrs", QJsonObject{{"columns", QJsonArray{QJsonObject{{"id", "empty-def"}, {"width", 160}}}}}},
                {"children", QJsonArray{QJsonObject{{"id", "empty-row"}, {"type", "table_row"}, {"children", QJsonArray{emptyCell}}}}}}
        });
        blank.setCanonicalDocument(emptyContainers);
        if (blank.canonicalDocument() != emptyContainers) qCritical() << QJsonDocument(blank.canonicalDocument()).toJson();
        require(blank.canonicalDocument() == emptyContainers, "empty containers do not fabricate paragraphs");
        auto p = paragraph("parent", "Español 👩🏽‍💻 é");
        p["attrs"] = QJsonObject{{"extension", QJsonObject{{"keep", true}}}};
        auto child = paragraph("child", "second");
        p["children"] = QJsonArray{child};
        auto group = QJsonObject{{"id", "g"}, {"type", "group"}, {"children", QJsonArray{p}}};
        auto cell = QJsonObject{
            {"id", "cell"}, {"type", "table_cell"}, {"children", QJsonArray{paragraph("cp", "Cell")}}};
        auto table = QJsonObject{
            {"id", "table"},
            {"type", "table"},
            {"attrs",
             QJsonObject{{"columns", QJsonArray{QJsonObject{{"id", "column-def"}, {"width", 200}}}}}},
            {"children",
             QJsonArray{QJsonObject{{"id", "row"}, {"type", "table_row"}, {"children", QJsonArray{cell}}}}}};
        auto column = QJsonObject{{"id", "column"},
                                  {"type", "column"},
                                  {"attrs", QJsonObject{{"width", 1}}},
                                  {"children", QJsonArray{paragraph("col-p", "Column")}}};
        auto columns = QJsonObject{{"id", "columns"}, {"type", "columns"}, {"children", QJsonArray{column}}};
        auto source = doc(QJsonArray{group, table, columns});
        require(LumenDocument::fromPresentation(LumenDocument::toPresentation(source)) == source,
                "pure codec must preserve tree, IDs and attrs");
        TestEditor editor;
        editor.resize(700, 500);
        editor.setCanonicalDocument(source);
        auto actual = editor.canonicalDocument();
        if (actual != source) {
            qCritical().noquote() << QJsonDocument(actual).toJson();
            qCritical().noquote() << QJsonDocument(source).toJson();
        }
        require(actual == source, "Qt roundtrip must preserve all canonical nodes");
        auto cursor = editor.document()->find("second");
        require(!cursor.isNull(), "child rendered");
        cursor.insertText("changed");
        auto changed = editor.canonicalDocument();
        auto changedParent = changed["blocks"].toArray()[0].toObject()["children"].toArray()[0].toObject();
        require(changedParent["id"] == "parent", "parent identity retained");
        require(
            changedParent["children"].toArray()[0].toObject()["content"].toArray()[0].toObject()["text"] ==
                "changed",
            "editing child updates correct node");
        require(changed["blocks"].toArray()[1] == table, "editing another block preserves table and cells");
        editor.undo();
        require(editor.canonicalDocument() == source, "undo preserves model metadata");
        auto rich = paragraph("rich", "");
        rich["content"] =
            QJsonArray{QJsonObject{{"type", "text"},
                                   {"text", "Bold"},
                                   {"marks", QJsonObject{{"bold", true}, {"color", "#ff0000"}}}},
                       QJsonObject{{"type", "break"}},
                       QJsonObject{{"type", "object_ref"}, {"object_id", "obj"}, {"label", "Object"}},
                       QJsonObject{{"type", "temporal"},
                                   {"attrs", QJsonObject{{"kind", "datetime"},
                                                         {"start", "2026-11-02T02:17:00+01:00"},
                                                         {"timezone", "Europe/Madrid"}}}}};
        auto richSource = doc(QJsonArray{rich});
        editor.setCanonicalDocument(richSource);
        require(editor.canonicalDocument() == richSource, "rich inline semantics retained");
        QDir cases(QFINDTESTDATA("../../contracts/fixtures/document-commands"));
        require(cases.exists(), "shared operation fixtures exist");
        for (const auto &name : cases.entryList({"*.json"}, QDir::Files)) {
            QFile f(cases.filePath(name));
            require(f.open(QIODevice::ReadOnly), "read operation fixture");
            const auto test = QJsonDocument::fromJson(f.readAll()).object();
            QJsonObject result;
            QString error;
            const bool ok = LumenDocument::applyCommands(test["input"].toObject(), test["commands"].toArray(),
                                                         &result, &error);
            if (test.contains("expected")) {
                if (!ok || result != test["expected"].toObject())
                    qCritical() << name << error << QJsonDocument(result).toJson();
                require(ok && result == test["expected"].toObject(), "command fixture parity");
                editor.setCanonicalDocument(test["input"].toObject());
                require(editor.applyCanonicalCommands(test["commands"].toArray(), &error),
                        "editor operation accepted");
                if (editor.canonicalDocument() != test["expected"].toObject()) qCritical() << name << QJsonDocument(editor.canonicalDocument()).toJson();
                require(editor.canonicalDocument() == test["expected"].toObject(),
                        "Qt structural operation preserves canonical result");
                editor.undo();
                require(editor.canonicalDocument() == test["input"].toObject(),
                        "Qt structural operation undo");
                editor.redo();
                require(editor.canonicalDocument() == test["expected"].toObject(),
                        "Qt structural operation redo");
            } else
                require(!ok && result == test["input"].toObject(), "failed batch leaves input unchanged");
        }
        if (argc > 1) {
            QFile fixture(argv[1]);
            require(fixture.open(QIODevice::ReadOnly), "open shared fixture");
            const auto canonical = QJsonDocument::fromJson(fixture.readAll()).object();
            editor.setCanonicalDocument(canonical);
            const auto actual = editor.canonicalDocument();
            if (actual != canonical) {
                if (argc > 2) {
                    QFile output(argv[2]);
                    if (output.open(QIODevice::WriteOnly))
                        output.write(QJsonDocument(actual).toJson());
                } else
                    qCritical().noquote() << QJsonDocument(actual).toJson();
            }
            require(actual == canonical, "shared canonical corpus survives Qt roundtrip");
        }
        QFile clipboardFixture(QFINDTESTDATA("../../contracts/fixtures/clipboard-v1/internal-and-external-reference.json"));
        require(clipboardFixture.open(QIODevice::ReadOnly), "shared clipboard fixture");
        const auto clipboardCase = QJsonDocument::fromJson(clipboardFixture.readAll()).object();
        const auto clipboardFrame = clipboardCase["clipboard"].toObject(); QJsonObject prepared; QString clipboardError;
        require(LumenClipboard::prepare(QJsonDocument(clipboardFrame).toJson(), clipboardCase["target_object_id"].toString(),
            &prepared, &clipboardError, clipboardCase["new_ids"].toObject()), "clipboard fixture preparation");
        require(prepared == clipboardCase["expected_document"].toObject(), "clipboard shared golden refs marks attrs image/table widths");
        editor.setDocumentObjectId("source-note"); editor.setCanonicalDocument(clipboardFrame["document"].toObject());
        auto selection = editor.textCursor(); selection.select(QTextCursor::Document); editor.setTextCursor(selection);
        std::unique_ptr<QMimeData> copied(editor.createMimeDataFromSelection());
        require(copied->hasFormat(LumenClipboard::Mime), "whole subtree copy emits interoperable envelope");
        const auto copiedDoc = QJsonDocument::fromJson(copied->data(LumenClipboard::Mime)).object()["document"].toObject();
        if (copiedDoc != clipboardFrame["document"].toObject()) qCritical() << QJsonDocument(copiedDoc).toJson();
        require(copiedDoc == clipboardFrame["document"].toObject(), "native copy preserves full subtree");
        TestEditor destination; destination.setDocumentObjectId("target-note"); destination.setCanonicalDocument(LumenDocument::empty());
        destination.insertFromMimeData(copied.get());
        const auto pasted = destination.canonicalDocument();
        require(LumenClipboard::validate(pasted, &clipboardError), "native clipboard paste is canonical");
        const auto pastedChild = pasted["blocks"].toArray()[0].toObject()["children"].toArray()[0].toObject();
        const auto parts = pastedChild["content"].toArray();
        require(pastedChild["id"] != "shared-block" && parts[1].toObject()["object_id"] == "target-note" && parts[1].toObject()["block_id"] == pastedChild["id"], "internal reference targets copied block and destination owner");
        require(parts[2].toObject()["object_id"] == "external-note" && parts[2].toObject()["block_id"] == "shared-block", "external same block ID remains external");
        destination.undo(); require(destination.canonicalDocument() == LumenDocument::empty(), "clipboard paste undo restores empty document");
        const auto targetText = LumenDocument::fromText("Before after"); destination.setCanonicalDocument(targetText);
        auto caret = destination.textCursor(); caret.setPosition(7); destination.setTextCursor(caret);
        require(destination.pasteCanonicalDocument(doc(QJsonArray{paragraph("fresh-inserted", "Pasted")})), "block paste into paragraph");
        require(LumenDocument::plainText(destination.canonicalDocument()).contains("Before ") && LumenDocument::plainText(destination.canonicalDocument()).contains("after"), "block paste preserves prefix and suffix text");
        destination.undo(); require(destination.canonicalDocument() == targetText, "block paste undo restores rich source");
        destination.setCanonicalDocument(LumenDocument::empty());

        auto invalidFrame = clipboardFrame; invalidFrame["clipboard_version"] = 2; QMimeData unsupported;
        unsupported.setData(LumenClipboard::Mime, QJsonDocument(invalidFrame).toJson()); unsupported.setText("must not silently paste");
        destination.insertFromMimeData(&unsupported); require(destination.canonicalDocument() == LumenDocument::empty(), "unsupported MIME does not flatten or mutate");
        QMimeData externalHTML; externalHTML.setHtml("<p><b>Bold</b><img src='https://invalid.test/private.png'> tail</p><table><tr><td>A</td><td>B</td></tr></table>");
        destination.insertFromMimeData(&externalHTML); const auto normalized = destination.canonicalDocument();
        require(LumenClipboard::validate(normalized, &clipboardError), "external HTML normalizes to valid document");
        require(destination.toPlainText().contains("Bold") && destination.toPlainText().contains("incorporar archivo"), "external image markup never fabricates assetless image");
        editor.setCanonicalDocument(LumenDocument::empty());
        require(editor.applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "insert"},
            {"position", 0}, {"node", paragraph("fresh", "Hello")}}}), "insert in empty doc");
        editor.undo(); require(editor.canonicalDocument() == LumenDocument::empty(), "undo to empty document");
        editor.setCanonicalDocument(source);
        require(editor.selectCanonicalBlock("parent"), "select parent subtree");
        require(editor.textCursor().selectedText().contains("second"), "parent selection includes child");
        require(editor.applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "delete"}, {"node_id", "parent"}}}), "delete subtree");
        require(!LumenDocument::plainText(editor.canonicalDocument()).contains("second"), "child removed with parent");
        require(editor.canonicalDocument().value("blocks").toArray()[1] == table, "subtree deletion preserves sibling table");
        editor.setCanonicalDocument(source);
        editor.setTextCursor(editor.document()->find("Cell"));
        QTest::keyClick(&editor, Qt::Key_Return);
        require(editor.textCursor().currentTable() == nullptr, "Enter exits a table to text");
        require(editor.textCursor().blockFormat().alignment() == Qt::AlignLeft, "table exit resets alignment");
        QFile imageFixture(QFINDTESTDATA("../../contracts/fixtures/document-v2/rich.document.json"));
        require(imageFixture.open(QIODevice::ReadOnly), "image interaction fixture");
        const auto imageDocument = QJsonDocument::fromJson(imageFixture.readAll()).object();
        editor.setCanonicalDocument(imageDocument); editor.focusBlock("cell-text-0-1");
        auto *copyTable = editor.textCursor().currentTable(); require(copyTable != nullptr, "table selection source");
        auto columnSelection = copyTable->cellAt(0, 1).firstCursorPosition();
        columnSelection.setPosition(copyTable->cellAt(copyTable->rows() - 1, 1).lastCursorPosition().position(), QTextCursor::KeepAnchor);
        editor.setTextCursor(columnSelection); std::unique_ptr<QMimeData> tableCopy(editor.createMimeDataFromSelection());
        require(tableCopy->hasFormat(LumenClipboard::Mime), "column copy has semantic table payload");
        const auto clippedTable = QJsonDocument::fromJson(tableCopy->data(LumenClipboard::Mime)).object()["document"].toObject()["blocks"].toArray()[0].toObject();
        require(clippedTable["attrs"].toObject()["columns"].toArray().size() == 1 && clippedTable["attrs"].toObject()["columns"].toArray()[0].toObject()["width"] == 96, "column copy preserves the selected column width");
        require(clippedTable["children"].toArray()[0].toObject()["children"].toArray().size() == 1, "column copy clips matching row cells");
        editor.setAppearance(true, true);
        editor.setCanonicalDocument(imageDocument); editor.focusBlock("image"); app.processEvents();
        auto *leftImage = editor.findChild<QToolButton *>("imagealign-left");
        require(leftImage != nullptr, "native image alignment toolbar exists"); leftImage->click();
        auto imageBlocks = editor.canonicalDocument()["blocks"].toArray();
        bool aligned = false; for (const auto &v : imageBlocks) if (v.toObject()["id"] == "image")
            aligned = v.toObject()["attrs"].toObject()["alignment"] == "left";
        require(aligned, "image toolbar persists semantic alignment");
        editor.undo(); require(editor.canonicalDocument() == imageDocument, "image alignment undo preserves entire document");
        editor.focusBlock("image");
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            require(dialog != nullptr, "image caption editor opens"); dialog->setTextValue("Leyenda nueva"); dialog->accept();
        });
        editor.findChild<QToolButton *>("imagetypography")->click();
        require(editor.toPlainText().contains("Leyenda nueva"), "image caption renders inside atomic image block");
        editor.undo(); require(editor.canonicalDocument() == imageDocument, "caption undo restores document");
        editor.focusBlock("image"); editor.show(); app.processEvents();
        auto *resize = editor.findChild<QWidget *>("imageResizeRight");
        require(resize != nullptr, "image resize handle exists");
        const auto handleStart = resize->rect().center();
        QTest::mousePress(resize, Qt::LeftButton, Qt::NoModifier, handleStart);
        const auto local = handleStart + QPoint(40, 0);
        QMouseEvent drag(QEvent::MouseMove, QPointF(local), QPointF(resize->mapToGlobal(local)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(resize, &drag);
        QTest::mouseRelease(resize, Qt::LeftButton, Qt::NoModifier, handleStart);
        bool resized = false; for (const auto &v : editor.canonicalDocument()["blocks"].toArray()) if (v.toObject()["id"] == "image")
            resized = v.toObject()["attrs"].toObject()["width"].toDouble() > 320;
        require(resized, "native handle resizes image semantic dimensions");
        editor.undo(); require(editor.canonicalDocument() == imageDocument, "image resize undo restores full document");
        editor.setCanonicalDocument(LumenDocument::fromText("Before "));
        auto endCursor = editor.textCursor(); endCursor.movePosition(QTextCursor::End); editor.setTextCursor(endCursor);
        const auto blockHit = QJsonObject{{"object_id", "source-object"}, {"object_title", "Source"},
            {"block", QJsonObject{{"id", "source-block"}, {"text", "Referenced text"}}}};
        editor.insertBlockReference(blockHit);
        const auto referenced = editor.canonicalDocument()["blocks"].toArray()[0].toObject()["content"].toArray();
        require(referenced.last().toObject()["type"] == "block_ref" && referenced.last().toObject()["object_id"] == "source-object", "block ref stores object and stable block IDs");
        editor.setCanonicalDocument(LumenDocument::empty());
        editor.blockSearchRequested = [blockHit](QString, std::function<void(QJsonArray)> callback) { callback(QJsonArray{blockHit}); };
        QTimer::singleShot(0, [&] {
            QDialog *picker = nullptr; for (auto *widget : QApplication::allWidgets()) if (widget->objectName() == "blockReferencePicker") picker = qobject_cast<QDialog *>(widget);
            require(picker != nullptr, "native block reference picker opens");
            require(picker->findChild<QListWidget *>("blockReferenceResults")->count() == 1, "synchronous search result accepted");
            if (!qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR").isEmpty()) picker->grab().save(qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR") + "/block-reference-picker.png");
            QMetaObject::invokeMethod(picker->findChild<QLineEdit *>("blockReferenceQuery"), "returnPressed", Qt::DirectConnection);
        });
        editor.showBlockReferencePicker();
        const auto embedded = editor.canonicalDocument()["blocks"].toArray()[0].toObject();
        require(embedded["type"] == "object_embed" && embedded["attrs"].toObject()["block_id"] == "source-block", "block picker embeds stable referenced block");

        const auto displayMath = QJsonObject{{"id", "display-math"}, {"type", "math"}, {"attrs", QJsonObject{{"source", "x^2"}}}};
        editor.setCanonicalDocument(doc(QJsonArray{displayMath})); editor.focusBlock("display-math");
        QTest::keyClick(&editor, Qt::Key_Return); editor.insertPlainText("After formula");
        require(editor.textCursor().blockFormat().alignment() == Qt::AlignLeft, "text after display math resets to left");
        require(editor.canonicalDocument()["blocks"].toArray().size() == 2, "math exit creates independent text block");
        QJsonObject imageNode; for (const auto &v : imageDocument["blocks"].toArray()) if (v.toObject()["id"] == "image") imageNode = v.toObject();
        editor.setCanonicalDocument(doc(QJsonArray{imageNode, paragraph("image-neighbor", "Neighbor text")}));
        int imageOpens = 0; editor.openReference = [&](QString) { ++imageOpens; };
        editor.focusBlock("image-neighbor"); app.processEvents();
        QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, editor.cursorRect().center());
        require(imageOpens == 0, "clicking adjacent text does not open image object");
        editor.setCanonicalDocument(source); require(editor.selectCanonicalBlock("parent"), "select canonical parent for grouping");
        editor.groupSelection();
        require(LumenClipboard::validate(editor.canonicalDocument(), &clipboardError) && LumenDocument::plainText(editor.canonicalDocument()).contains("second"), "grouping canonical parent preserves children without duplicate IDs");
        editor.undo(); require(editor.canonicalDocument() == source, "canonical parent grouping undo is lossless");
        editor.setPlainText("Group must retain text"); editor.moveCursor(QTextCursor::End); editor.groupSelection();
        const auto groupedSingle = editor.canonicalDocument();
        require(groupedSingle["blocks"].toArray()[0].toObject()["type"] == "group" && LumenDocument::plainText(groupedSingle).contains("Group must retain text"), "CtrlG single block preserves all text in canonical child");
        require(LumenClipboard::validate(groupedSingle, &clipboardError), "grouped single block is valid canonical document");
        editor.undo(); require(editor.toPlainText() == "Group must retain text", "single group undo retains text");
        editor.setCanonicalDocument(LumenDocument::empty());
        const auto temporalAttributes = QJsonObject{{"kind", "datetime"},
            {"start", "2026-11-02T02:17:00+01:00"}, {"end", "2026-11-02T03:17:00+01:00"},
            {"timezone", "Europe/Madrid"}};
        auto duplicateTemporal = paragraph("duplicate-time", "");
        duplicateTemporal["content"] = QJsonArray{QJsonObject{{"type", "temporal"}, {"attrs", temporalAttributes}}, QJsonObject{{"type", "temporal"}, {"attrs", temporalAttributes}}};
        const auto duplicateDocument = doc(QJsonArray{duplicateTemporal});
        editor.setCanonicalDocument(duplicateDocument);
        require(editor.canonicalDocument() == duplicateDocument, "identical adjacent temporal atoms retain multiplicity");
        editor.setCanonicalDocument(LumenDocument::empty());
        editor.insertTemporalReference(temporalAttributes);
        auto savedTemporal = editor.canonicalDocument()["blocks"].toArray()[0].toObject()["content"].toArray();
        require(savedTemporal[0].toObject()["attrs"] == temporalAttributes, "temporal chip retains semantic range");
        editor.insertPlainText(" after");
        savedTemporal = editor.canonicalDocument()["blocks"].toArray()[0].toObject()["content"].toArray();
        require(savedTemporal.size() == 2 && savedTemporal[1].toObject()["type"] == "text", "typing after date is plain text");
        editor.setCanonicalDocument(LumenDocument::empty());
        QTimer::singleShot(0, [&] {
            QDialog *picker = nullptr;
            for (auto *widget : QApplication::allWidgets()) if (widget->objectName() == "temporalPicker") picker = qobject_cast<QDialog *>(widget);
            require(picker != nullptr, "native temporal popup present");
            picker->findChild<QCheckBox *>("includeEnd")->setChecked(true);
            picker->findChild<QDateEdit *>("startDate")->setDate(QDate(2026, 11, 2));
            picker->findChild<QDateEdit *>("endDate")->setDate(QDate(2026, 11, 2));
            picker->findChild<QTimeEdit *>("startTime")->setTime(QTime(2, 17));
            picker->findChild<QTimeEdit *>("endTime")->setTime(QTime(3, 17));
            if (!qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR").isEmpty())
                picker->grab().save(qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR") + "/temporal-range.png");
            picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        editor.showTemporalPicker(true);
        const auto picked = editor.canonicalDocument()["blocks"].toArray()[0].toObject()["content"].toArray()[0].toObject()["attrs"].toObject();
        require(picked["kind"] == "datetime" && picked.contains("end") && picked.contains("timezone"), "native range picker saves semantic data");
        if (!qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR").isEmpty()) {
            const auto path = qEnvironmentVariable("LUMEN_QA_CAPTURE_DIR"); QDir().mkpath(path);
            QImage image(640, 320, QImage::Format_RGB32); image.fill(QColor("#27384f"));
            QPainter painter(&image); painter.setPen(Qt::white); painter.setFont(QFont("Sans", 24));
            painter.drawText(image.rect(), Qt::AlignCenter, "Lumen · imagen de prueba"); painter.end();
            editor.resourceProvider = [image](int type, const QUrl &) -> QVariant {
                return type == QTextDocument::ImageResource ? QVariant(image) : QVariant();
            };
            QFile richFixture(QFINDTESTDATA("../../contracts/fixtures/document-v2/rich.document.json"));
            require(richFixture.open(QIODevice::ReadOnly), "capture fixture");
            const auto richDoc = QJsonDocument::fromJson(richFixture.readAll()).object();
            editor.setAppearance(true, true);
            editor.setStyleSheet("QTextEdit { background: #131316; color: #e5e5e8; border: none; }");
            editor.setFont(QFont("Sans", 12)); editor.resize(850, 1200);
            editor.setCanonicalDocument(richDoc); editor.show(); app.processEvents();
            require(editor.grab().save(path + "/document-rich.png"), "save Qt real component capture");
            editor.focusBlock("image"); QTest::qWait(650); app.processEvents();
            require(editor.grab().save(path + "/image-toolbar.png"), "save image toolbar capture");
        }
        qInfo() << "DOCUMENT_V2_QT_PASS";
        return 0;
    } catch (const std::exception &e) {
        qCritical() << e.what();
        return 1;
    }
}
