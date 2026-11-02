#include "lumen_editor.hpp"
#include "lumen_media.hpp"
#include <QBuffer>
#include <QPdfWriter>
#include <QtTest>
#include <cstdio>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { qCritical() << "EDITOR_MEDIA_TEST_FAILED:" << message; throw std::runtime_error(message); }
}
QByteArray samplePdf() {
    QByteArray bytes;
    QBuffer output(&bytes);
    output.open(QIODevice::WriteOnly);
    QPdfWriter writer(&output);
    writer.setPageSize(QPageSize(QPageSize::A5));
    writer.setTitle("Ficción: prueba del visor");
    QPainter painter(&writer);
    painter.drawText(150, 200, "Primera página ficticia");
    writer.newPage();
    painter.drawText(150, 200, "Segunda página ficticia");
    writer.newPage();
    painter.drawText(150, 200, "Tercera página ficticia");
    painter.end();
    return bytes;
}
void process() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(30); }
}
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    QFontDatabase::addApplicationFont(":/assets/fonts/Inter-Regular.otf");
    application.setFont(QFont("Inter", 10));
    try {
        RichEditor editor;
        editor.resize(740, 500);
        editor.show();
        editor.setFocus();
        process();
        const QJsonArray types{QJsonObject{{"id", "book"}, {"name", "Libro"}}, QJsonObject{{"id", "note"}, {"name", "Nota"}}};
        const QJsonObject book{{"id", "book-one"}, {"type_id", "book"}, {"title", "Leer para aprender"}};
        editor.setReferenceData(QJsonArray{book}, types);
        QFont editorFont = editor.font(); editorFont.setPixelSize(16); editor.setFont(editorFont);
        editor.applyBlockStyle("heading2"); editor.applyBlockStyle("paragraph");
        require(editor.textCursor().blockCharFormat().fontPointSize() > 0, "paragraph formatting must support pixel-sized application fonts");
        editor.setPlainText("Primera idea\nSegunda idea");
        auto blocks = editor.structuredBlocks();
        require(blocks.size() == 2, "paragraphs must be separate blocks");
        const QString firstId = blocks[0].toObject().value("id").toString();
        const QString secondId = blocks[1].toObject().value("id").toString();
        require(!firstId.isEmpty() && firstId != secondId, "block IDs must be unique");
        editor.moveCursor(QTextCursor::End);
        QTest::keyClicks(&editor, " ampliada");
        auto edited = editor.structuredBlocks();
        require(edited[0].toObject().value("id").toString() == firstId && edited[1].toObject().value("id").toString() == secondId, "editing must retain block identity");
        QTest::keyClick(&editor, Qt::Key_Return);
        edited = editor.structuredBlocks();
        require(edited.size() == 3 && edited[2].toObject().value("id").toString() != secondId, "splitting must allocate a fresh block ID");
        editor.undo();
        require(editor.structuredBlocks().size() == 2, "one undo must remove a split including normalization");
        editor.applyBlockStyle("heading2");
        editor.moveCursor(QTextCursor::End);
        editor.insertObjectReference(book);
        require(editor.documentReferenceIds() == QJsonArray{"book-one"}, "mention must carry an object ID");
        auto titleOnly = editor.document()->find(book.value("title").toString());
        require(!titleOnly.isNull(), "inserted mention title must be searchable");
        titleOnly.removeSelectedText();
        require(editor.documentReferenceIds().isEmpty(), "deleting only the visible title must discard an orphan icon reference immediately");
        (void)editor.structuredBlocks();
        editor.undo();
        require(editor.documentReferenceIds() == QJsonArray{"book-one"}, "one undo must restore the title and reference after orphan cleanup");
        const auto structured = editor.structuredBlocks();
        const QString plain = editor.toPlainText();
        RichEditor roundTrip;
        roundTrip.setStructuredBlocks(structured);
        require(roundTrip.toPlainText() == plain && roundTrip.documentReferenceIds() == QJsonArray{"book-one"}, "structured roundtrip must preserve text and references");
        require(roundTrip.structuredBlocks()[0].toObject().value("id").toString() == firstId, "roundtrip must preserve stable IDs");
        require(roundTrip.focusBlock(secondId) && !roundTrip.focusBlock("missing"), "backlink navigation must target an exact stable block ID");
        roundTrip.moveCursor(QTextCursor::End);
        roundTrip.insertObjectReferenceBlock(book);
        auto embedded = roundTrip.structuredBlocks();
        bool hasEmbed = false;
        for (const auto &part : embedded) if (part.toObject().value("type").toString() == "embed") hasEmbed = true;
        require(hasEmbed, "block object presentation must retain embed semantics");
        RichEditor embeddedRoundTrip;
        embeddedRoundTrip.setStructuredBlocks(embedded);
        require(embeddedRoundTrip.structuredBlocks() == embedded, "block object presentation must survive JSON roundtrip");
        editor.setFocus();
        auto cursor = editor.textCursor();
        cursor.select(QTextCursor::Document);
        cursor.removeSelectedText();
        require(editor.documentReferenceIds().isEmpty(), "removing the last mention must remove the reference");
        editor.undo();
        require(editor.documentReferenceIds() == QJsonArray{"book-one"}, "undo must restore mention semantics");
        editor.setPlainText("correo");
        editor.moveCursor(QTextCursor::End);
        QTest::keyClicks(&editor, "@ejemplo.test");
        require(editor.toPlainText() == "correo@ejemplo.test" && !editor.findChild<QLineEdit *>("referenceSearch"), "email addresses must not invoke the object picker");
        editor.clear();
        QTest::keyClicks(&editor, "@");
        auto *results = editor.findChild<QListWidget *>("referenceResults");
        require(results && results->isVisible() && editor.hasFocus() && editor.toPlainText() == "@",
            "at trigger must keep the caret and raw query in the writing block");
        require(!editor.findChild<QLineEdit *>("referenceSearch") && !editor.findChild<QComboBox *>("referenceType"),
            "picker must not move writing to a separate search form");
        QTest::keyClicks(&editor, "Leer");
        require(editor.toPlainText() == "@Leer" && results->currentItem()->text() == "Leer para aprender",
            "typing in the editor must filter the object menu");
        QTest::keyClick(&editor, Qt::Key_Return);
        process();
        require(editor.documentReferenceIds() == QJsonArray{"book-one"} && !editor.toPlainText().contains('@'),
            "Enter must replace inline query with the searched object");
        bool iconRun = false;
        for (const auto &run : editor.structuredBlocks()[0].toObject().value("runs").toArray())
            iconRun |= run.toObject().value("reference_icon").toBool();
        require(iconRun, "mention must use a packaged SVG badge image, not a text glyph");
        editor.moveCursor(QTextCursor::End);
        editor.insertPlainText(" ");
        QTest::keyClicks(&editor, "@consulta");
        QTest::keyClick(&editor, Qt::Key_Escape);
        process();
        require(editor.hasFocus() && editor.toPlainText().endsWith("consulta") && !editor.toPlainText().contains('@'),
            "Escape must keep typed words in the block without the trigger");
        RichEditor::ReferenceCompletion completion;
        editor.referenceCreateRequested = [&completion](QString type, QString title, RichEditor::ReferenceCompletion done) {
            require(type == "book" && title == "Nueva idea", "inline creation must use the chosen type and title");
            completion = std::move(done);
        };
        editor.clear();
        editor.showReferencePicker();
        results = editor.findChild<QListWidget *>("referenceResults");
        int bookTypeRow = -1;
        for (int row = 0; row < results->count(); ++row)
            if (results->item(row)->data(Qt::UserRole + 3).toString() == "book") bookTypeRow = row;
        require(bookTypeRow >= 0, "root menu must offer object types alongside recent objects");
        results->setCurrentRow(bookTypeRow);
        QTest::keyClick(&editor, Qt::Key_Right);
        require(results->count() == 1 && results->item(0)->data(Qt::UserRole).toJsonObject().value("id") == "book-one",
            "Right must enter the selected type without moving the caret");
        QTest::keyClick(&editor, Qt::Key_Left);
        require(results->count() == 4, "Left must return to all recent objects and types");
        results->setCurrentRow(bookTypeRow);
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClicks(&editor, "Nueva idea");
        QTest::keyClick(&editor, Qt::Key_Return);
        require(bool(completion), "creation must deliver async completion");
        editor.insertPlainText("continúo escribiendo");
        completion(QJsonObject{{"id", "new-idea"}, {"type_id", "book"}, {"title", "Nueva idea"}});
        require(editor.documentReferenceIds() == QJsonArray{"new-idea"} && editor.toPlainText().contains("continúo escribiendo"),
            "late creation must preserve intervening text");
        editor.clear();
        editor.showReferencePicker();
        results = editor.findChild<QListWidget *>("referenceResults");
        results->setCurrentRow(bookTypeRow);
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClicks(&editor, "Nueva idea");
        QTest::keyClick(&editor, Qt::Key_Return);
        editor.setPlainText("Otra nota");
        completion(QJsonObject{{"id", "stale-idea"}, {"type_id", "book"}, {"title", "Nueva idea"}});
        require(editor.toPlainText() == "Otra nota" && editor.documentReferenceIds().isEmpty(),
            "late creation must never insert into a replacement document");

        editor.setPlainText("Texto conservado");
        const auto styleBlockId = editor.structuredBlocks()[0].toObject().value("id").toString();
        auto styleCursor = editor.textCursor(); styleCursor.setPosition(0); editor.setTextCursor(styleCursor);
        QTest::keyClicks(&editor, "/Encabezado 4");
        auto *blockResults = editor.findChild<QListWidget *>("blockResults");
        require(blockResults && editor.hasFocus() && blockResults->count() == 1,
            "slash filtering must stay in the block and match headings");
        QTest::keyClick(&editor, Qt::Key_Return);
        process();
        auto styled = editor.structuredBlocks()[0].toObject();
        require(styled.value("type") == "heading" && styled.value("level").toInt() == 4
            && styled.value("text") == "Texto conservado" && styled.value("id") == styleBlockId
            && editor.textCursor().position() == 0,
            "block choice must preserve text, caret and stable block ID");
        editor.undo();
        require(editor.structuredBlocks()[0].toObject().value("type") == "paragraph",
            "one undo must revert the chosen block format");
        editor.setPlainText("Texto conservado");
        editor.moveCursor(QTextCursor::End);
        editor.showBlockMenu();
        QTest::keyClicks(&editor, "Cita");
        QTest::keyClick(&editor, Qt::Key_Escape);
        process();
        require(editor.toPlainText() == "Texto conservadoCita", "slash cancel must preserve typed words");
        for (const auto &style : {QString("small"), QString("alphabetic"), QString("code")}) {
            editor.setPlainText("Contenido"); editor.applyBlockStyle(style);
            const auto json = editor.structuredBlocks();
            RichEditor semanticRoundTrip; semanticRoundTrip.setStructuredBlocks(json);
            require(semanticRoundTrip.structuredBlocks() == json, "all slash styles must preserve their JSON semantics");
        }
        editor.referenceCreateRequested = [](QString type, QString title, RichEditor::ReferenceCompletion done) {
            require(type == "journal" && title == "2026-10-30", "date menu must request the chosen real journal date");
            done(QJsonObject{{"id", "journal-2026-10-30"}, {"type_id", "journal"}, {"title", "30 de octubre de 2026"}});
        };
        editor.clear(); editor.showReferencePicker();
        results = editor.findChild<QListWidget *>("referenceResults");
        int dateRow = -1;
        for (int row = 0; row < results->count(); ++row)
            if (results->item(row)->data(Qt::UserRole + 3).toString() == "journal") dateRow = row;
        require(dateRow >= 0, "at menu must expose Fecha even when the server omits the built-in journal type");
        results->setCurrentRow(dateRow); QTest::keyClick(&editor, Qt::Key_Right);
        auto *dateMenu = editor.findChild<QCalendarWidget *>("referenceCalendar");
        require(dateMenu && dateMenu->isVisible(), "Fecha must reveal an anchored native calendar");
        dateMenu->setSelectedDate(QDate(2026, 10, 29));
        QTest::keyClick(&editor, Qt::Key_Right);
        require(dateMenu->selectedDate() == QDate(2026, 10, 30) && editor.hasFocus(),
            "date arrows must navigate the calendar while the editor retains focus");
        QTest::keyClick(&editor, Qt::Key_Return);
        process();
        require(editor.documentReferenceIds() == QJsonArray{"journal-2026-10-30"}, "date choice must insert a real journal reference");

        // --- Atomic reference deletion test ---
        editor.clear();
        editor.setReferenceData(QJsonArray{book}, types);
        editor.insertObjectReference(book);
        require(editor.documentReferenceIds() == QJsonArray{"book-one"}, "reference inserted");
        auto cur = editor.textCursor();
        cur.setPosition(editor.toPlainText().length());
        editor.setTextCursor(cur);
        // Backspace should delete the trailing space, pos becomes end of span
        QTest::keyClick(&editor, Qt::Key_Backspace);
        process();
        // Next backspace must delete the entire reference chip at once
        QTest::keyClick(&editor, Qt::Key_Backspace);
        process();
        require(editor.documentReferenceIds().isEmpty(), "backspace on reference chip must delete the entire reference atomically");
        require(!editor.toPlainText().contains("Leer"), "reference title must not be deleted letter by letter");
        editor.undo();
        process();
        require(editor.documentReferenceIds() == QJsonArray{"book-one"}, "undo must restore the full atomic reference");

        // Delete key before the reference chip
        cur.setPosition(0);
        editor.setTextCursor(cur);
        QTest::keyClick(&editor, Qt::Key_Delete);
        process();
        require(editor.documentReferenceIds().isEmpty(), "delete key on reference chip must delete entire chip atomically");
        editor.undo();
        process();
        require(editor.documentReferenceIds() == QJsonArray{"book-one"}, "undo restores reference after delete key");

        // --- Markdown live shortcuts test ---
        editor.clear();
        QTest::keyClicks(&editor, "##");
        QTest::keyClick(&editor, Qt::Key_Space);
        require(editor.structuredBlocks()[0].toObject().value("type") == "heading" &&
                editor.structuredBlocks()[0].toObject().value("level").toInt() == 2,
                "typing '## ' must trigger heading level 2");

        editor.clear();
        QTest::keyClicks(&editor, "---");
        process();
        require(editor.structuredBlocks()[0].toObject().value("type") == "divider", "typing '---' must trigger horizontal rule block");

        // La línea horizontal vive a nivel 1: sale de la lista envolvente y no
        // hereda sangrías del bloque anterior.
        editor.clear();
        QTest::keyClicks(&editor, "1. hola");
        process();
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::keyClicks(&editor, "---");
        process();
        QTextBlock dividerBlock;
        for (auto b = editor.document()->begin(); b.isValid(); b = b.next()) {
            if (b.blockFormat().property(LumenSyntax::BlockStyleProperty).toString() == "divider") dividerBlock = b;
        }
        require(dividerBlock.isValid(), "typing '---' after a list item must insert a divider");
        require(!dividerBlock.textList(), "divider must leave the enclosing numbered list");
        require(dividerBlock.blockFormat().leftMargin() == 0 && dividerBlock.blockFormat().indent() == 0,
                "divider must sit at top level without inherited indentation");
        bool serializedDivider = false;
        for (const auto &v : editor.structuredBlocks())
            if (v.toObject().value("type").toString() == "divider") serializedDivider = true;
        require(serializedDivider, "divider after a list item must serialize as divider, not as part of the list");

        editor.clear();
        QTest::keyClicks(&editor, "[[");
        process();
        auto *bracketResults = editor.findChild<QListWidget *>("referenceResults");
        require(bracketResults && bracketResults->isVisible(), "typing '[[' must open reference picker");
        QTest::keyClick(&editor, Qt::Key_Escape);
        process();

        editor.clear();
        editor.showBlockMenu();
        process();
        auto *blockMenu = editor.findChild<QListWidget *>("blockResults");
        require(blockMenu && blockMenu->isVisible(), "slash menu must open");
        bool hasDivider = false, hasColumns = false, hasTable = false, hasGroup = false, hasIcon = false, hasCode = false;
        bool hasObjectSection = false, hasNewTask = false;
        for (int i = 0; i < blockMenu->count(); ++i) {
            auto *it = blockMenu->item(i);
            const QString style = it->data(Qt::UserRole).toString();
            const QString title = it->text();
            if (style == "divider") hasDivider = true;
            if (style == "columns") hasColumns = true;
            if (style == "table") hasTable = true;
            if (style == "group") hasGroup = true;
            if (style == "icon") hasIcon = true;
            if (style == "code") hasCode = true;
            if (title == "Crear un objeto") hasObjectSection = true;
            if (it->data(Qt::UserRole + 1).toString() == "task") hasNewTask = true;
        }
        require(hasDivider && hasColumns && hasTable && hasGroup && hasIcon && hasCode,
            "slash menu must include all editor structural blocks: divider, columns, table, group, icon, code");
        require(hasObjectSection && hasNewTask, "slash menu must include 'Crear un objeto' section with object types like Task");
        QTest::keyClick(&editor, Qt::Key_Escape);
        process();

        const auto captureDirectory = qEnvironmentVariable("LUMEN_EDITOR_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QDir().mkpath(captureDirectory);
            editor.resize(573, 750); editor.setAppearance(true);
            editor.setStyleSheet("QTextEdit{background:#131315;color:#e4e3e2;font-size:16px;}");
            const QJsonArray captureTypes{
                QJsonObject{{"id", "book"}, {"name", "Libros"}, {"icon", "book"}, {"color", "#9e78bc"}},
                QJsonObject{{"id", "journal"}, {"name", "Fecha"}, {"icon", "calendar"}, {"color", "#7294ca"}},
                QJsonObject{{"id", "note"}, {"name", "Ideas"}, {"icon", "bulb"}, {"color", "#b99854"}},
                QJsonObject{{"id", "web"}, {"name", "Enlaces web"}, {"icon", "world"}, {"color", "#7294ca"}},
                QJsonObject{{"id", "project"}, {"name", "Proyectos"}, {"icon", "box"}, {"color", "#6d9b7e"}}};
            editor.setReferenceData(QJsonArray{
                QJsonObject{{"id", "capture-book"}, {"type_id", "book"}, {"title", "Pensar rápido, pensar despacio"}},
                QJsonObject{{"id", "capture-web"}, {"type_id", "web"}, {"title", "GDevelop: Free, Fast, Easy Game Engine"}},
                QJsonObject{{"id", "capture-note"}, {"type_id", "note"}, {"title", "10 Hábitos Simples para Mejorar tu Vida"}},
                QJsonObject{{"id", "capture-habits"}, {"type_id", "note"}, {"title", "Hábitos"}},
                QJsonObject{{"id", "capture-pkm"}, {"type_id", "note"}, {"title", "PKM"}}}, captureTypes);
            auto capture = [&](const QString &kind, const QString &filename) {
                for (auto *panel : editor.findChildren<QFrame *>("editorLayer"))
                    if (panel->isVisible() && panel->property("editorMenuKind") == kind) {
                        require(panel->grab().save(captureDirectory + "/" + filename), "menu screenshot must save");
                        return;
                    }
                require(false, "visible menu must exist for screenshot");
            };
            editor.clear(); editor.showReferencePicker(); process(); capture("reference", "editor-at-recents.png");
            QTest::keyClicks(&editor, "Pensar"); process(); capture("reference", "editor-at-query.png");
            QTest::keyClick(&editor, Qt::Key_Escape); process();
            editor.clear(); editor.showReferencePicker();
            results = editor.findChild<QListWidget *>("referenceResults");
            for (int row = 0; row < results->count(); ++row)
                if (results->item(row)->data(Qt::UserRole + 3).toString() == "journal") results->setCurrentRow(row);
            QTest::keyClick(&editor, Qt::Key_Right); process(); capture("reference", "editor-at-date.png");
            QTest::keyClick(&editor, Qt::Key_Escape); process();
            editor.clear(); editor.showBlockMenu(); process(); capture("block", "editor-slash-blocks.png");
            QTest::keyClick(&editor, Qt::Key_Escape); process();
        }
        editor.setPlainText("Toolbar selection");
        editor.selectAll();
        editor.setFocus();
        QTest::qWait(130);
        bool toolbarVisible = false;
        for (auto *layer : editor.findChildren<QFrame *>("editorLayer")) if (!layer->isWindow() && layer->isVisible()) toolbarVisible = true;
        require(toolbarVisible, "selection must expose the floating format toolbar");
        require(editor.document()->resource(QTextDocument::ImageResource, QUrl("https://example.invalid/tracker.png")).isNull(), "HTML must never fetch remote resources");
        require(editor.document()->resource(QTextDocument::ImageResource, QUrl("file:///etc/passwd")).isNull(), "HTML must never resolve local resources");

        // Verify clipboard / MIME image paste interception
        bool imagePastedReceived = false;
        QSize receivedImageSize;
        editor.imagePasted = [&](QImage img) {
            imagePastedReceived = true;
            receivedImageSize = img.size();
        };
        auto *testMime = new QMimeData;
        QImage testPasteImg(64, 64, QImage::Format_ARGB32);
        testPasteImg.fill(Qt::red);
        testMime->setImageData(testPasteImg);
        QApplication::clipboard()->setMimeData(testMime);
        editor.paste();
        process();
        require(imagePastedReceived, "editor paste must trigger imagePasted callback when clipboard has an image");
        require(receivedImageSize == QSize(64, 64), "pasted image must preserve pixel dimensions");

        // Verify insertImageBlock, inline image document resource caching, roundtrip and documentReferenceIds
        RichEditor imageEditor;
        QPixmap samplePix(200, 100);
        samplePix.fill(Qt::blue);
        imageEditor.insertImageBlock("img-obj-1", samplePix, "Captura 1.png");
        process();
        require(!imageEditor.document()->resource(QTextDocument::ImageResource, QUrl("lumen://image/img-obj-1")).isNull(),
            "lumen:// scheme image must be resolved via PrivateDocument internal resource cache");
        require(imageEditor.documentReferenceIds() == QJsonArray{"img-obj-1"},
            "inline image block must register object reference ID for backlinks");
        const auto imgBlocks = imageEditor.structuredBlocks();
        bool foundImageRun = false;
        for (const auto &b : imgBlocks) {
            for (const auto &r : b.toObject().value("runs").toArray()) {
                if (r.toObject().value("type").toString() == "image" && r.toObject().value("object_id").toString() == "img-obj-1") {
                    foundImageRun = true;
                    require(r.toObject().value("width").toDouble() == 200, "image run must serialize width");
                    require(r.toObject().value("height").toDouble() == 100, "image run must serialize height");
                }
            }
        }
        require(foundImageRun, "structuredBlocks must serialize inline image run");
        RichEditor imgRoundTrip;
        imgRoundTrip.resourceProvider = [&samplePix](int type, const QUrl &name) -> QVariant {
            if (type == QTextDocument::ImageResource && name == QUrl("lumen://image/img-obj-1")) {
                return samplePix;
            }
            return {};
        };
        imgRoundTrip.setStructuredBlocks(imgBlocks);
        process();
        require(!imgRoundTrip.document()->resource(QTextDocument::ImageResource, QUrl("lumen://image/img-obj-1")).isNull(),
            "deserialized image block must resolve resource via resourceProvider and PrivateDocument");
        require(imgRoundTrip.documentReferenceIds() == QJsonArray{"img-obj-1"},
            "deserialized image block must preserve document reference IDs");
        require(imgRoundTrip.structuredBlocks() == imgBlocks,
            "image blocks must survive structuredBlocks roundtrip");

        // Removing the image block clears documentReferenceIds
        imageEditor.selectAll();
        imageEditor.textCursor().removeSelectedText();
        process();
        require(imageEditor.documentReferenceIds().isEmpty(),
            "removing image block must remove its reference ID from documentReferenceIds");

        const auto pdf = samplePdf();
        auto *firstFramePdf = MediaViewer::showAsset(nullptr, "fiction-first-frame", "Primera apertura.pdf", pdf, false, true);
        // The deferred initial restoration must never replace an explicit first-frame request.
        require(firstFramePdf->pageCount() == 3, "PDF page count must be available after loading the memory buffer");
        firstFramePdf->setPage(2);
        require(firstFramePdf->currentPage() == 2, "First-frame page request did not reach the third page");
        QTest::qWait(140);
        require(firstFramePdf->currentPage() == 2, "Deferred PDF restoration replaced a newer explicit page request");
        firstFramePdf->close(); process();
        auto *pdfViewer = MediaViewer::showAsset(nullptr, "fiction-pdf", "Tres páginas ficticias.pdf", pdf, false, true);
        QPointer<MediaViewer> pdfGuard(pdfViewer);
        process();
        require(pdfViewer->pageCount() == 3 && pdfViewer->errorText().isEmpty(), "PDF must render as a three-page native PDF");
        require(pdfViewer->findChild<QToolButton *>("mediaClose"), "viewer must expose a close control");
        pdfViewer->setPage(2);
        require(pdfViewer->currentPage() == 2, "PDF page navigation must reach the third page");
        pdfViewer->zoomIn();
        pdfViewer->close();
        process();
        require(!pdfGuard, "closing must release document bytes");
        pdfViewer = MediaViewer::showAsset(nullptr, "fiction-pdf", "Tres páginas ficticias.pdf", pdf, false, true);
        process();
        require(pdfViewer->currentPage() == 2, "reopening PDF must restore its session page");
        pdfViewer->close(); process();
        QImage image(120, 360, QImage::Format_ARGB32);
        image.fill(QColor(74, 124, 103, 120));
        QByteArray png;
        QBuffer pngOutput(&png);
        pngOutput.open(QIODevice::WriteOnly);
        image.save(&pngOutput, "PNG");
        auto *imageViewer = MediaViewer::showAsset(nullptr, "fiction-image", "Imagen vertical.png", png, true, true);
        process();
        auto *imageLabel = imageViewer->findChild<QLabel *>("mediaImage");
        require(imageLabel && !imageLabel->pixmap().isNull(), "image must render from memory");
        const auto dimensions = imageLabel->pixmap().size();
        require(qAbs(double(dimensions.width()) / dimensions.height() - 1. / 3.) < .01, "image must preserve its vertical ratio");
        imageViewer->zoomIn(); imageViewer->fitWidth(); imageViewer->actualSize();
        QTest::keyClick(imageViewer, Qt::Key_Escape); process();
        auto *bad = MediaViewer::showAsset(nullptr, "fiction-bad", "Dañado.pdf", "%PDF-broken", false, true);
        process();
        require(!bad->errorText().isEmpty(), "invalid files must show a useful error");
        bad->close(); process();
        std::puts("EDITOR_MEDIA_OK: stable blocks, split/undo, JSON roundtrip, reference removal/undo, inline editor query, typed object navigation, chosen-type async creation, stale-document guard, slash block styles, native date picker, selection toolbar, private resources, PDF pages/session position, image fit/zoom, error/close");
        return 0;
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
