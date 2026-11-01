// Traslado de bloques y alineación del tirador.
// Regresión del 30-10-2026: al arrastrar la línea "---" el divisor se destruía
// (dropEvent reinsertaba solo el texto plano), el tirador no quedaba centrado
// en la línea y la sangría de listas iba demasiado a la derecha.
#include "lumen_editor.hpp"
#include <QDir>
#include <QtTest>
#include <cstdio>

namespace {
void require(bool condition, const char *message) {
    if (!condition) { qCritical() << "EDITOR_BLOCKS_TEST_FAILED:" << message; throw std::runtime_error(message); }
}
void process() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QTest::qWait(30); }
QString blockStyle(const QTextBlock &b) {
    return b.blockFormat().property(LumenSyntax::BlockStyleProperty).toString();
}
QTextCharFormat firstFormat(const QTextBlock &b) {
    for (auto it = b.begin(); !it.atEnd(); ++it)
        if (it.fragment().isValid() && !it.fragment().text().isEmpty())
            return it.fragment().charFormat();
    return b.charFormat();
}
// Subclase para alcanzar la API protegida de traslado y geometría.
struct ProbeEditor : RichEditor {
    using RichEditor::performBlockMove;
    using RichEditor::positionBlockHandle;
    using RichEditor::blockAtViewportPoint;
    using RichEditor::insertFromMimeData;
    using QTextEdit::cursorRect;
    static DropMode dropBefore() { return DropBefore; }
    static DropMode dropAfter() { return DropAfter; }
};
QTextBlock appendBlock(RichEditor &editor, const QString &text, const QString &style) {
    auto c = editor.textCursor();
    c.movePosition(QTextCursor::End);
    c.insertBlock(QTextBlockFormat(), QTextCharFormat());
    c.insertText(text);
    editor.setTextCursor(c);
    if (!style.isEmpty()) editor.applyBlockStyle(style);
    process();   // ensureBlockIds corre con temporizador asíncrono
    return editor.textCursor().block();
}
QTextBlock blockWithText(RichEditor &editor, const QString &needle) {
    for (auto b = editor.document()->begin(); b.isValid(); b = b.next())
        if (b.text().contains(needle)) return b;
    return {};
}
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    ProbeEditor editor;
    editor.resize(760, 500);
    editor.show();
    process();
    try {
        {
            QFile file(QFINDTESTDATA("../../contracts/fixtures/toggles.json"));
            require(file.open(QIODevice::ReadOnly), "toggle fixture opens");
            const auto blocks = QJsonDocument::fromJson(file.readAll()).object().value("objects").toArray().first().toObject().value("properties").toObject().value("blocks").toArray();
            editor.setStructuredBlocks(blocks); process();
            auto saved = editor.structuredBlocks();
            require(saved.size() == 2, "toggle has no extra boundary paragraphs");
            require(saved.first().toObject().value("children").toArray().size() == 2, "toggle owns editable child blocks");
            require(blockWithText(editor, "Aquí van").isVisible(), "expanded body is visible");
            require(!blockWithText(editor, "Texto anidado").isVisible(), "nested collapsed body is hidden");
            auto body = blockWithText(editor, "Aquí van");
            QTextCursor edit(body); edit.movePosition(QTextCursor::EndOfBlock); edit.insertText(" editadas"); process();
            auto header = blockWithText(editor, "Esto es un toggle");
            editor.setTextCursor(QTextCursor(header));
            QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, editor.cursorRect().center()); process();
            require(header.text().startsWith(QString::fromUtf8("▶ ")), "triangle closes toggle");
            require(!blockWithText(editor, "Aquí van").isVisible(), "closing hides the body");
            require(blockWithText(editor, "Fuera").isVisible(), "closing leaves the next sibling visible");
            saved = editor.structuredBlocks();
            require(saved.first().toObject().value("children").toArray().first().toObject().value("text").toString().endsWith(" editadas"), "hidden body retains edits");
            RichEditor reopened; reopened.setStructuredBlocks(saved);
            require(!blockWithText(reopened, "Aquí van").isVisible(), "reload retains collapsed state");
            editor.undo(); process();
            require(blockWithText(editor, "Aquí van").isVisible(), "undo reopens without losing body edits");
            require(!blockWithText(editor, "Texto anidado").isVisible(), "undo respects nested collapsed state");
            const auto dir = qEnvironmentVariable("LUMEN_EDITOR_CAPTURE_DIR");
            if (!dir.isEmpty()) {
                QDir().mkpath(dir); editor.setAppearance(true);
                editor.setStyleSheet("QTextEdit { background: #151518; color: #e4e3e2; font-size: 16px; border: 0; }");
                editor.resize(760, 340); process();
                require(editor.grab().save(dir + "/toggle-expanded.png"), "capture expanded toggle");
                editor.redo(); process();
                require(editor.cursorRect(QTextCursor(blockWithText(editor, "Fuera"))).top() > editor.cursorRect(QTextCursor(blockWithText(editor, "Esto es"))).bottom(), "redo reflows the next paragraph below the collapsed title");
                require(editor.grab().save(dir + "/toggle-collapsed.png"), "capture collapsed toggle");
                reopened.resize(760, 340); reopened.setAppearance(true); reopened.setStyleSheet("QTextEdit { background: #151518; color: #e4e3e2; font-size: 16px; border: 0; }"); reopened.show(); process();
                require(reopened.grab().save(dir + "/toggle-reloaded.png"), "capture reloaded toggle");
                editor.setStyleSheet("");
            }
            editor.setTextCursor(QTextCursor(blockWithText(editor, "Esto es un toggle")));
            QTest::keyClick(&editor, Qt::Key_Return); process();
            require(editor.textCursor().block().text().contains("Aquí van"), "Enter opens the toggle and enters its body");
            editor.setStructuredBlocks(blocks); process();
            editor.performBlockMove(blockWithText(editor, "Esto es"), blockWithText(editor, "Fuera"), ProbeEditor::dropAfter()); process();
            auto moved = editor.structuredBlocks();
            require(moved.size() == 2 && moved.last().toObject().value("children").toArray().size() == 2, "moving a toggle moves its complete subtree");
            require(moved.last().toObject().value("id") == "toggle-main", "moving keeps the toggle ID");
            editor.undo(); process();
            require(editor.structuredBlocks().first().toObject().value("id") == "toggle-main", "undo restores moved toggle");
            editor.duplicateBlock(blockWithText(editor, "Esto es")); process();
            auto duplicated = editor.structuredBlocks();
            require(duplicated.size() == 3 && duplicated.at(1).toObject().value("children").toArray().size() == 2, "duplicate toggle includes the complete editable body");
            require(duplicated.first().toObject().value("id") != duplicated.at(1).toObject().value("id"), "duplicate gets a distinct block ID");
            editor.clear(); editor.setPlainText("Nuevo toggle");
            editor.applyBlockStyle("toggle"); process();
            require(editor.structuredBlocks().first().toObject().value("type") == "toggle", "creating a toggle owns its body");
            QTest::keyClick(&editor, Qt::Key_Return); QTest::keyClicks(&editor, "Editable"); process();
            require(editor.structuredBlocks().first().toObject().value("children").toArray().first().toObject().value("text") == "Editable", "new toggle body edits persist");
            QTest::keyClick(&editor, Qt::Key_Return); QTest::keyClick(&editor, Qt::Key_Return); QTest::keyClicks(&editor, "Sibling"); process();
            require(editor.structuredBlocks().size() == 2, "Enter in the empty final child exits the toggle");
            editor.deleteBlock(blockWithText(editor, "Nuevo toggle")); process();
            require(!editor.toPlainText().contains("Editable") && editor.toPlainText().contains("Sibling"), "delete toggle removes its body and preserves sibling");
            editor.undo(); process();
            require(editor.structuredBlocks().first().toObject().value("children").toArray().first().toObject().value("text") == "Editable", "undo delete restores the toggle body");
            editor.clear();
        }
        {
            QFile file(QFINDTESTDATA("../../contracts/fixtures/native-structures.json"));
            require(file.open(QIODevice::ReadOnly), "shared native structures fixture opens");
            const auto blocks = QJsonDocument::fromJson(file.readAll()).object().value("blocks").toArray();
            RichEditor editor;
            editor.resourceProvider = [](int type, const QUrl &url) -> QVariant {
                if (type != QTextDocument::ImageResource || url != QUrl("lumen://image/shared-image")) return {};
                QImage image(400, 100, QImage::Format_ARGB32); image.fill(QColor("#356d8a")); return image;
            };
            editor.setStructuredBlocks(blocks);
            auto saved = editor.structuredBlocks();
            require(saved.size() == 3, "table and columns stay logical root blocks");
            require(saved.at(0).toObject().value("id") == "shared-table", "table id stays stable");
            require(saved.at(0).toObject().value("custom") == "preserve-frame", "table extension metadata survives");
            QTextTable *table = nullptr;
            for (auto *frame : editor.document()->rootFrame()->childFrames())
                if (auto *candidate = qobject_cast<QTextTable *>(frame)) { table = candidate; break; }
            require(table != nullptr, "table is represented by a real editable QTextTable");
            auto cursor = table->cellAt(1, 0).firstCursorPosition();
            cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); cursor.insertText("Nota editada");
            saved = editor.structuredBlocks();
            auto cell = saved.at(0).toObject().value("rows").toArray().at(1).toArray().at(0).toObject();
            require(cell.value("runs").toArray().first().toObject().value("text") == "Nota editada", "cell edits serialize inside the same table");
            const auto header = saved.at(0).toObject().value("rows").toArray().at(0).toArray().at(0).toObject().value("runs").toArray().first().toObject();
            require(header.value("bold").toBool() && header.value("object_id") == "shared-book", "table cell styles and references survive");
            const auto column = saved.at(1).toObject().value("columns").toArray();
            require(column.size() == 2 && column.at(0).toArray().first().toObject().value("id") == "shared-child", "columns retain their child ids");
            const auto image = column.at(1).toArray().first().toObject().value("runs").toArray().first().toObject();
            require(image.value("object_id") == "shared-image" && image.value("width").toInt() == 400, "nested image identity and dimensions survive");
            require(editor.documentReferenceIds().contains("shared-image") && editor.documentReferenceIds().contains("shared-book"), "nested native references are real document links");
            RichEditor reopened; reopened.setStructuredBlocks(saved);
            const auto roundTrip = reopened.structuredBlocks();
            require(roundTrip.size() == 3 && roundTrip.at(0).toObject().value("rows") == saved.at(0).toObject().value("rows"), "reload keeps the edited table without extra boundary paragraphs");
            const QJsonObject group{{"id", "complex-group"}, {"type", "group"}, {"text", ""}, {"runs", QJsonArray()}, {"children", blocks}};
            RichEditor grouped; grouped.setStructuredBlocks(QJsonArray{group});
            const auto groupResult = grouped.structuredBlocks();
            require(groupResult.size() == 1 && groupResult.first().toObject().value("children").toArray().size() == 3, "nested tables inside groups retain their structure");
            auto compoundToggle = group; compoundToggle.insert("type", "toggle"); compoundToggle.insert("text", "▼ Estructuras internas");
            compoundToggle.insert("runs", QJsonArray{QJsonObject{{"text", "▼ Estructuras internas"}}});
            RichEditor toggled; toggled.resize(760, 600); toggled.setStructuredBlocks(QJsonArray{compoundToggle, QJsonObject{{"type", "paragraph"}, {"text", "Después"}}}); toggled.show(); process();
            auto toggleHeader = blockWithText(toggled, "Estructuras internas"); toggled.setTextCursor(QTextCursor(toggleHeader));
            QTest::mouseClick(toggled.viewport(), Qt::LeftButton, Qt::NoModifier, toggled.cursorRect().center()); process();
            require(toggled.structuredBlocks().first().toObject().value("children").toArray().size() == 3, "collapsed table/columns retain all children");
            require(toggled.cursorRect(QTextCursor(blockWithText(toggled, "Después"))).top() - toggled.cursorRect(QTextCursor(toggleHeader)).top() < 80, "collapsed complex bodies do not reserve their content height");
            const auto captureDir = qEnvironmentVariable("LUMEN_EDITOR_CAPTURE_DIR");
            if (!captureDir.isEmpty()) require(toggled.grab().save(captureDir + "/toggle-complex-collapsed.png"), "capture hidden tables and columns");
            toggled.undo(); process();
            require(toggled.cursorRect(QTextCursor(blockWithText(toggled, "Después"))).top() - toggled.cursorRect(QTextCursor(toggleHeader)).top() > 80, "undo restores complex body layout");
            if (!captureDir.isEmpty()) {
                QDir().mkpath(captureDir); editor.resize(760, 600); editor.setAppearance(true); editor.show(); process();
                require(editor.grab().save(captureDir + "/editor-native-structures.png"), "capture real Qt table/columns component");
            }
        }

        // ---- el divisor sobrevive al traslado (caso del usuario) ----------------
        editor.clear(); process();
        editor.setPlainText("uno\ndos\ntres"); process();
        editor.moveCursor(QTextCursor::End);
        editor.insertDivider(); process();
        QTextBlock divider;
        for (auto b = editor.document()->begin(); b.isValid(); b = b.next())
            if (blockStyle(b) == "divider") divider = b;
        require(divider.isValid(), "insertDivider debe dejar un bloque divisor");
        const QString dividerId = RichEditor::blockId(divider);
        editor.performBlockMove(divider, editor.document()->firstBlock(), ProbeEditor::dropBefore());
        process();
        QTextBlock moved;
        for (auto b = editor.document()->begin(); b.isValid(); b = b.next())
            if (blockStyle(b) == "divider") moved = b;
        require(moved.isValid() && moved.text().contains(QChar::ObjectReplacementCharacter),
                "el divisor debe sobrevivir al traslado");
        require(RichEditor::blockId(moved) == dividerId, "el divisor debe conservar su id");
        bool imageOk = false;
        for (auto it = moved.begin(); !it.atEnd(); ++it) {
            const auto f = it.fragment().charFormat();
            if (f.isImageFormat() && f.toImageFormat().name().startsWith("lumen-rule://divider/")) imageOk = true;
        }
        require(imageOk, "la imagen de la línea debe seguir presente tras el traslado");
        require(moved.blockFormat().alignment() & Qt::AlignHorizontal_Mask, "el divisor conserva su alineación");
        // De vuelta al final y undo: el divisor vuelve y undo restaura.
        editor.performBlockMove(moved, editor.document()->lastBlock(), ProbeEditor::dropAfter());
        process();
        require(blockStyle(editor.document()->lastBlock()) == "divider", "el divisor vuelve al final intacto");
        editor.undo(); process();
        bool restored = false;
        for (auto b = editor.document()->begin(); b.isValid(); b = b.next())
            if (blockStyle(b) == "divider") restored = true;
        require(restored, "undo debe restaurar el divisor tras el traslado");

        // ---- las tareas conservan su estilo al moverse ---------------------------
        editor.clear(); process();
        editor.setPlainText("uno\ndos\ntres"); process();
        appendBlock(editor, QString::fromUtf8("○ Wuwa"), "todo");
        editor.performBlockMove(editor.document()->lastBlock(), editor.document()->firstBlock(), ProbeEditor::dropBefore());
        process();
        const QTextBlock todo = blockWithText(editor, "Wuwa");
        require(blockStyle(todo) == "todo" && todo.text().startsWith(QString::fromUtf8("○ ")),
                "la tarea movida conserva el estilo y el prefijo");
        require(todo.textList() == nullptr, "la tarea movida no hereda una lista");

        // ---- viñetas: conservan su lista y no pierden la del destino -------------
        editor.clear(); process();
        editor.setPlainText("uno\ndos\ntres"); process();
        appendBlock(editor, "X", "bullet");
        editor.performBlockMove(editor.document()->lastBlock(), blockWithText(editor, "dos"), ProbeEditor::dropBefore());
        process();
        const QTextBlock bullet = blockWithText(editor, "X");
        require(bullet.textList() && bullet.textList()->format().style() == QTextListFormat::ListDisc,
                "la viñeta movida conserva su lista");
        editor.clear(); process();
        editor.setPlainText("uno\ndos\ntres"); process();
        QTextBlock target = blockWithText(editor, "dos");
        editor.setTextCursor(QTextCursor(target));
        editor.applyBlockStyle("bullet");
        appendBlock(editor, QString::fromUtf8("○ Wuwa"), "todo");
        editor.performBlockMove(editor.document()->lastBlock(), blockWithText(editor, "dos"), ProbeEditor::dropBefore());
        process();
        require(blockWithText(editor, "dos").textList() != nullptr, "el destino viñeta sigue en su lista");

        // ---- el destino con formato no lo pierde ---------------------------------
        editor.clear(); process();
        editor.setPlainText("uno\ndos\ntres"); process();
        target = blockWithText(editor, "dos");
        editor.setTextCursor(QTextCursor(target));
        editor.applyBlockStyle("heading2");
        appendBlock(editor, QString::fromUtf8("○ Wuwa"), "todo");
        editor.performBlockMove(editor.document()->lastBlock(), blockWithText(editor, "dos"), ProbeEditor::dropBefore());
        process();
        require(blockWithText(editor, "dos").blockFormat().headingLevel() == 2,
                "el encabezado destino conserva su nivel");

        // ---- soltar un bloque sobre sí mismo no lo destruye ----------------------
        editor.clear(); process();
        editor.setPlainText("uno\ndos"); process();
        const int blocksBefore = editor.document()->blockCount();
        editor.performBlockMove(editor.document()->firstBlock(), editor.document()->firstBlock(), ProbeEditor::dropAfter());
        process();
        require(editor.document()->blockCount() == blocksBefore, "soltar sobre sí mismo no cambia el documento");

        // ---- tirador centrado en la línea del divisor ----------------------------
        editor.clear(); process();
        editor.setPlainText("uno"); process();
        editor.moveCursor(QTextCursor::End);
        editor.insertDivider(); process();
        for (auto b = editor.document()->begin(); b.isValid(); b = b.next())
            if (blockStyle(b) == "divider") divider = b;
        editor.setTextCursor(QTextCursor(divider));
        process();
        auto *handle = editor.findChild<QToolButton *>("blockHandle");
        require(handle != nullptr, "el tirador de bloque existe");
        editor.positionBlockHandle();
        const QRect handleGeo = handle->geometry();
        const QRect line = editor.cursorRect(QTextCursor(divider));
        require(qAbs((handleGeo.y() + handleGeo.height() / 2.0) - (line.y() + line.height() / 2.0)) <= 1.0,
                "el tirador debe quedar centrado en la línea visible del divisor");

        // ---- hover por fila completa: el tirador no salta de línea ---------------
        const QPoint inGap(60, line.bottom() + 6);   // dentro del margen inferior del divisor
        const QTextBlock hovered = editor.blockAtViewportPoint(inGap);
        require(hovered.isValid() && blockStyle(hovered) == "divider",
                "el hover en la fila del divisor (incluidos márgenes) debe resolver al divisor");
        const QPoint inGutter(10, line.center().y()); // dentro del margen izquierdo del tirador (x < 32)
        const QTextBlock gutterHovered = editor.blockAtViewportPoint(inGutter);
        require(gutterHovered.isValid() && blockStyle(gutterHovered) == "divider",
                "el hover en el margen izquierdo del tirador también resuelve al bloque");

        // ---- sangría de listas y alineación de tareas al ras ---------------------
        editor.clear(); process();
        editor.setPlainText("normal"); process();
        appendBlock(editor, "punto", "bullet");
        appendBlock(editor, QString::fromUtf8("○ tarea"), "todo");
        process();
        const int paragraphX = editor.cursorRect(QTextCursor(editor.document()->firstBlock())).x();
        const int bulletX = editor.cursorRect(QTextCursor(blockWithText(editor, "punto"))).x();
        const int todoX = editor.cursorRect(QTextCursor(blockWithText(editor, "tarea"))).x();
        require(bulletX > paragraphX && bulletX - paragraphX < 24,
                "el texto de la viñeta arranca cerca del párrafo (sangría <= 24 px)");
        require(todoX == paragraphX,
                "la tarea arranca exactamente al mismo ras horizontal que el párrafo");

        // ---- Enter en tarea crea la siguiente tarea; Enter en tarea vacía sale ---
        editor.moveCursor(QTextCursor::End);
        QTest::keyClick(&editor, Qt::Key_Return);
        process();
        require(editor.textCursor().block().text() == QString::fromUtf8("○ ") &&
                blockStyle(editor.textCursor().block()) == "todo",
                "Enter al final de una tarea debe crear otra tarea con casilla desmarcada");
        QTest::keyClick(&editor, Qt::Key_Return);
        process();
        require(editor.textCursor().block().text().isEmpty() &&
                blockStyle(editor.textCursor().block()) == "paragraph" &&
                editor.cursorRect(editor.textCursor()).x() == paragraphX,
                "Enter en una tarea vacía debe salir a párrafo normal al mismo ras");

        // ---- Tab y Shift+Tab cambian la sangría sin insertar tabuladores ---------
        editor.clear(); process();
        editor.setPlainText("nivel"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("bullet"); process();
        QTextBlock indented = editor.document()->firstBlock();
        const int listIndent = indented.textList()->format().indent();
        QTest::keyClick(&editor, Qt::Key_Tab); process();
        indented = editor.document()->firstBlock();
        require(indented.textList() && indented.textList()->format().indent() == listIndent + 1,
                "Tab debe aumentar la sangría de la lista");
        QTest::keyClick(&editor, Qt::Key_Backtab, Qt::ShiftModifier); process();
        indented = editor.document()->firstBlock();
        require(indented.textList() && indented.textList()->format().indent() == listIndent,
                "Shift+Tab debe reducir la sangría de la lista");
        editor.applyBlockStyle("paragraph"); process();
        require(editor.document()->firstBlock().blockFormat().indent() == 0,
                "salir de una lista no debe conservar la sangría de la lista");
        QTest::keyClick(&editor, Qt::Key_Tab); process();
        require(editor.document()->firstBlock().blockFormat().indent() == 1,
                "Tab debe aumentar la sangría de un bloque normal");
        QTest::keyClick(&editor, Qt::Key_Backtab, Qt::ShiftModifier); process();
        require(editor.document()->firstBlock().blockFormat().indent() == 0,
                "Shift+Tab debe reducir la sangría de un bloque normal");

        // ---- Mover el primer bloque no destruye el formato del segundo -----------
        editor.clear(); process();
        editor.setPlainText("primero"); process();
        appendBlock(editor, "segundo", "bullet");
        appendBlock(editor, "tercero", "paragraph");
        process();
        editor.performBlockMove(editor.document()->firstBlock(), blockWithText(editor, "tercero"), ProbeEditor::dropAfter());
        process();
        require(blockWithText(editor, "segundo").textList() != nullptr,
                "mover el primer bloque conserva la lista del segundo bloque");

        // ---- cambiar tipo de bloque: marcador, formato, id y undo ---------------
        editor.clear(); process();
        QFont conversionFont = editor.font(); conversionFont.setPixelSize(16); editor.setFont(conversionFont);
        editor.setPlainText("Texto convertido"); process();
        QTextBlock converted = editor.document()->firstBlock();
        const QString convertedId = RichEditor::blockId(converted);
        editor.setTextCursor(QTextCursor(converted));
        editor.applyBlockStyle("paragraph"); process();
        const qreal baseSize = editor.textCursor().blockCharFormat().fontPointSize();
        require(baseSize > 0, "el párrafo debe fijar un tamaño base para comparar");

        editor.applyBlockStyle("todo"); process();
        converted = editor.document()->firstBlock();
        require(blockStyle(converted) == "todo" && converted.text().startsWith(QString::fromUtf8("○ ")),
                "párrafo a tarea debe añadir la casilla");

        editor.applyBlockStyle("bullet"); process();
        converted = editor.document()->firstBlock();
        require(!converted.text().startsWith(QString::fromUtf8("○ ")) &&
                !converted.text().startsWith(QString::fromUtf8("✔ ")),
                "tarea a viñeta debe retirar la casilla");
        require(converted.textList() && converted.textList()->format().style() == QTextListFormat::ListDisc,
                "tarea a viñeta debe crear la lista");

        editor.applyBlockStyle("heading1"); process();
        converted = editor.document()->firstBlock();
        auto convertedFormat = firstFormat(converted);
        require(converted.blockFormat().headingLevel() == 1, "viñeta a encabezado debe cambiar el nivel");
        require(!converted.text().startsWith(QString::fromUtf8("○ ")) &&
                !converted.text().startsWith(QString::fromUtf8("▶ ")),
                "viñeta a encabezado no debe dejar marcadores");
        require(convertedFormat.fontWeight() >= QFont::DemiBold,
                "el texto existente de un encabezado debe quedar en seminegrita");
        require(convertedFormat.fontPointSize() > baseSize,
                "el texto existente de un encabezado debe crecer");
        require(RichEditor::blockId(converted) == convertedId,
                "cambiar de tipo debe conservar el id del bloque");

        editor.applyBlockStyle("paragraph"); process();
        converted = editor.document()->firstBlock();
        convertedFormat = firstFormat(converted);
        require(converted.blockFormat().headingLevel() == 0, "encabezado a párrafo debe bajar el nivel");
        require(convertedFormat.fontWeight() < QFont::DemiBold,
                "encabezado a párrafo debe quitar la seminegrita");
        require(qAbs(convertedFormat.fontPointSize() - baseSize) < 0.01,
                "encabezado a párrafo debe volver al tamaño base");
        require(converted.text() == "Texto convertido", "el texto debe conservarse al cambiar de tipo");
        require(RichEditor::blockId(converted) == convertedId,
                "el id debe sobrevivir a toda la secuencia de cambios");

        editor.undo(); process();
        converted = editor.document()->firstBlock();
        require(converted.blockFormat().headingLevel() == 1,
                "un undo debe revertir el último cambio de tipo");

        // ---- tarea <-> toggle <-> encabezado no acumula marcas -------------------
        editor.clear(); process();
        editor.setPlainText("Tarea"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("todo"); process();
        converted = editor.document()->firstBlock();
        require(converted.text().startsWith(QString::fromUtf8("○ ")), "la tarea debe empezar con casilla");
        editor.applyBlockStyle("toggle"); process();
        converted = blockWithText(editor, "Tarea");
        require(converted.text().startsWith(QString::fromUtf8("▼ ")) &&
                !converted.text().contains(QString::fromUtf8("○ ")),
                "tarea a toggle debe sustituir la casilla por el triángulo");
        editor.applyBlockStyle("heading2"); process();
        converted = blockWithText(editor, "Tarea");
        require(converted.blockFormat().headingLevel() == 2 &&
                !converted.text().startsWith(QString::fromUtf8("▶ ")),
                "toggle a encabezado debe retirar el triángulo");
        editor.applyBlockStyle("toggle"); process();
        require(editor.structuredBlocks().first().toObject().value("type") == "toggle" && editor.structuredBlocks().first().toObject().value("children").toArray().size() == 1,
                "changing the title back to toggle reuses its body");

        editor.clear(); process();
        editor.setPlainText("Hecha"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("todo"); process();
        converted = editor.document()->firstBlock();
        QTextCursor check(converted);
        check.movePosition(QTextCursor::StartOfBlock);
        check.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
        check.insertText(QString::fromUtf8("✔ "));
        editor.applyBlockStyle("numbered"); process();
        converted = editor.document()->firstBlock();
        require(!converted.text().startsWith(QString::fromUtf8("✔ ")),
                "tarea marcada a lista numerada debe retirar la casilla");
        require(converted.textList() && converted.textList()->format().style() == QTextListFormat::ListDecimal,
                "la lista numerada debe crearse");

        // ---- el encabezado conserva negrita y tamaño al guardar y cargar ---------
        editor.clear(); process();
        editor.setPlainText("Persistente"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("paragraph"); process();
        const qreal roundBase = editor.textCursor().blockCharFormat().fontPointSize();
        editor.applyBlockStyle("heading3"); process();
        const auto headingBlocks = editor.structuredBlocks();
        RichEditor loaded;
        loaded.setStructuredBlocks(headingBlocks);
        const QTextBlock loadedBlock = loaded.document()->firstBlock();
        const auto loadedFormat = firstFormat(loadedBlock);
        require(loadedBlock.blockFormat().headingLevel() == 3,
                "el encabezado debe conservar su nivel al guardar y cargar");
        require(loadedFormat.fontWeight() >= QFont::DemiBold,
                "el encabezado debe conservar la seminegrita al guardar y cargar");
        require(loadedFormat.fontPointSize() > roundBase,
                "el encabezado debe conservar su tamaño al guardar y cargar");

        // ---- tagMentions: etiquetas mencionadas con su bloque, sin duplicados ----
        editor.clear(); process();
        const QJsonObject tagObject{{"id", "tag-atencion"}, {"type_id", "tag"}, {"title", "Atención"}};
        editor.setReferenceData(QJsonArray{tagObject}, QJsonArray{});
        editor.setPlainText("primera\nsegunda con la etiqueta\ntercera"); process();
        QTextCursor mention(editor.document());
        mention.movePosition(QTextCursor::Start);
        mention.movePosition(QTextCursor::NextBlock);
        mention.movePosition(QTextCursor::EndOfBlock);
        mention.insertText(" ");
        QTextCharFormat tagFormat;
        tagFormat.setAnchor(true);
        tagFormat.setAnchorHref("lumen://object/tag-atencion");
        mention.insertText("Atención", tagFormat);
        process();
        const auto mentions = editor.tagMentions();
        require(mentions.size() == 1, "una etiqueta mencionada una vez produce una mención");
        require(mentions.first().id == "tag-atencion", "la mención apunta al objeto Etiqueta");
        require(mentions.first().blockId == RichEditor::blockId(blockWithText(editor, "segunda")),
                "la mención apunta al bloque que la contiene");
        // Segunda mención de la misma etiqueta en otro bloque: no duplica.
        QTextCursor second(editor.document());
        second.movePosition(QTextCursor::Start);
        second.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, 2);
        second.movePosition(QTextCursor::EndOfBlock);
        second.insertText(" ");
        second.insertText("Atención", tagFormat);
        process();
        const auto deduped = editor.tagMentions();
        require(deduped.size() == 1, "las menciones repetidas se deduplican");
        require(deduped.first().blockId == RichEditor::blockId(blockWithText(editor, "segunda")),
                "se conserva el primer bloque con la mención");
        // Sin referencia de etiqueta no hay menciones.
        editor.clear(); process();
        editor.setPlainText("sin etiquetas"); process();
        require(editor.tagMentions().isEmpty(), "un documento sin referencias no menciona etiquetas");
        // ---- Ctrl+G sin selección mantiene el grupo de un solo bloque -------
        editor.clear(); process();
        editor.setPlainText("solo"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        QTest::keyClick(&editor, Qt::Key_G, Qt::ControlModifier);
        process();
        require(blockStyle(editor.document()->firstBlock()) == "group",
                "Ctrl+G without a selection keeps today's single-block group");
        // Selección dentro de un solo bloque: mismo comportamiento actual.
        editor.clear(); process();
        editor.setPlainText("solito"); process();
        auto inside = editor.textCursor();
        inside.setPosition(editor.document()->firstBlock().position());
        inside.setPosition(editor.document()->firstBlock().position() + 3, QTextCursor::KeepAnchor);
        editor.setTextCursor(inside);
        editor.groupSelection();
        process();
        require(blockStyle(editor.document()->firstBlock()) == "group",
                "a selection inside one block keeps today's single-block group");

        // ---- Ctrl+G agrupa una selección de varios bloques -------------------
        editor.clear(); process();
        editor.setPlainText("alpha\nbeta\ngamma"); process();
        QTextBlock alphaBlock = blockWithText(editor, "alpha");
        QTextBlock gammaBlock = blockWithText(editor, "gamma");
        const QString alphaId = RichEditor::blockId(alphaBlock);
        const QString gammaId = RichEditor::blockId(gammaBlock);
        auto span = editor.textCursor();
        span.setPosition(alphaBlock.position());
        span.setPosition(gammaBlock.position() + gammaBlock.length() - 1, QTextCursor::KeepAnchor);
        editor.setTextCursor(span);
        editor.groupSelection();
        process();
        const auto groupedBlocks = editor.structuredBlocks();
        require(groupedBlocks.size() == 1,
                "a grouped selection serializes as a single group block");
        const auto groupObject = groupedBlocks[0].toObject();
        require(groupObject.value("type").toString() == "group" && groupObject.value("text").toString().isEmpty(),
                "the group entry is a container block");
        const auto groupChildren = groupObject.value("children").toArray();
        require(groupChildren.size() == 3,
                "the group keeps its three member blocks as children");
        require(groupChildren[0].toObject().value("text").toString() == "alpha" &&
                groupChildren[1].toObject().value("text").toString() == "beta" &&
                groupChildren[2].toObject().value("text").toString() == "gamma",
                "children preserve the document order");
        require(groupChildren[0].toObject().value("id").toString() == alphaId &&
                groupChildren[2].toObject().value("id").toString() == gammaId,
                "members keep their block ids inside the group");
        require(groupChildren[0].toObject().value("type").toString() == "paragraph",
                "members keep their own type inside the group");
        // Guardar y cargar conserva la caja y los hijos en orden.
        RichEditor reloadedGroup;
        reloadedGroup.setStructuredBlocks(groupedBlocks);
        process();
        const auto reloadedGroupBlocks = reloadedGroup.structuredBlocks();
        require(reloadedGroupBlocks.size() == 1 &&
                reloadedGroupBlocks[0].toObject().value("children").toArray().size() == 3,
                "group children survive a save/load roundtrip");
        require(reloadedGroupBlocks[0].toObject().value("children").toArray()[0].toObject().value("id").toString() == alphaId,
                "member ids survive the roundtrip");
        // Desagrupar restaura los bloques, el orden y los ids.
        editor.ungroupBlock(blockWithText(editor, "beta"));
        process();
        const auto ungroupedBlocks = editor.structuredBlocks();
        require(ungroupedBlocks.size() == 3,
                "ungroup restores the member blocks at the top level");
        require(ungroupedBlocks[0].toObject().value("text").toString() == "alpha" &&
                ungroupedBlocks[1].toObject().value("text").toString() == "beta",
                "ungroup preserves the document order");
        require(ungroupedBlocks[0].toObject().value("id").toString() == alphaId &&
                ungroupedBlocks[2].toObject().value("id").toString() == gammaId,
                "ungroup preserves the member ids");
        require(ungroupedBlocks[0].toObject().value("type").toString() == "paragraph",
                "ungrouped members are plain blocks again");
        // Desagrupar un bloque de grupo clásico lo devuelve a párrafo.
        editor.clear(); process();
        editor.setPlainText("grupo suelto"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("group"); process();
        require(editor.blockInGroup(editor.document()->firstBlock()),
                "a group-styled block counts as being in a group");
        editor.ungroupBlock(editor.document()->firstBlock());
        process();
        require(blockStyle(editor.document()->firstBlock()) == "paragraph",
                "ungrouping a legacy single-block group returns it to a paragraph");

        // ---- mover un miembro fuera del grupo lo saca de la caja ------------
        editor.clear(); process();
        editor.setPlainText("m1\nm2\nm3\nfuera"); process();
        auto memberSpan = editor.textCursor();
        memberSpan.setPosition(blockWithText(editor, "m1").position());
        memberSpan.setPosition(blockWithText(editor, "m3").position() + blockWithText(editor, "m3").length() - 1, QTextCursor::KeepAnchor);
        editor.setTextCursor(memberSpan);
        editor.groupSelection();
        process();
        editor.moveBlockDown(blockWithText(editor, "m3"));
        process();
        const auto splitGroup = editor.structuredBlocks();
        require(splitGroup.size() == 3 && splitGroup[0].toObject().value("children").toArray().size() == 2,
                "a member moved out of the group leaves the box");

        // ---- copy block reference: formato del portapapeles ------------------
        editor.clear(); process();
        editor.setPlainText("hello reference"); process();
        const QTextBlock helloBlock = editor.document()->firstBlock();
        const QString helloId = RichEditor::blockId(helloBlock);
        editor.copyBlockReference(helloBlock);
        require(QGuiApplication::clipboard()->text() == "lumen://block/" + helloId,
                "copy block reference puts lumen://block/<block-id> on the clipboard");

        // ---- pegar una referencia en un bloque vacío crea el embed ----------
        editor.moveCursor(QTextCursor::End);
        QTest::keyClick(&editor, Qt::Key_Return);
        process();
        QMimeData referenceMime;
        referenceMime.setText("lumen://block/" + helloId);
        editor.insertFromMimeData(&referenceMime);
        process();
        const auto pastedBlocks = editor.structuredBlocks();
        require(pastedBlocks.size() == 3,
                "pasting into an empty block adds the embed and keeps writing below");
        const auto embedObject = pastedBlocks[1].toObject();
        require(embedObject.value("type").toString() == "embed",
                "a block reference pasted into an empty block embeds the block");
        bool embedChip = false;
        for (const auto &run : embedObject.value("runs").toArray())
            if (run.toObject().value("href").toString() == "lumen://block/" + helloId &&
                run.toObject().value("text").toString().contains("hello reference"))
                embedChip = true;
        require(embedChip,
                "the embed chip renders the referenced block's text");
        require(pastedBlocks[0].toObject().value("text").toString() == "hello reference",
                "the referenced block is untouched by the paste");

        // ---- pegar una referencia en medio del texto crea un chip inline ----
        editor.clear(); process();
        editor.setPlainText("the source\ninline target text"); process();
        const QString sourceId = RichEditor::blockId(blockWithText(editor, "the source"));
        editor.setTextCursor(QTextCursor(blockWithText(editor, "inline target text")));
        QMimeData inlineMime;
        inlineMime.setText("lumen://block/" + sourceId);
        editor.insertFromMimeData(&inlineMime);
        process();
        const auto inlineBlocks = editor.structuredBlocks();
        const auto inlineObject = inlineBlocks[1].toObject();
        require(inlineObject.value("type").toString() == "paragraph",
                "an inline paste keeps the block a paragraph");
        bool inlineChip = false;
        for (const auto &run : inlineObject.value("runs").toArray())
            if (run.toObject().value("href").toString() == "lumen://block/" + sourceId &&
                run.toObject().value("text").toString().contains("the source"))
                inlineChip = true;
        require(inlineChip, "an inline paste inserts a block-reference chip run");

        // ---- turn-into consolidation: code llega desde el menú del tirador --
        editor.clear(); process();
        editor.setPlainText("snippet"); process();
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("code:cpp"); process();
        const auto codeBlocks = editor.structuredBlocks();
        require(codeBlocks[0].toObject().value("type").toString() == "code" &&
                codeBlocks[0].toObject().value("language").toString() == "cpp",
                "turn into code keeps the code block behavior");

        // ---- estilos de bloque: color de texto y fondo ----------------------
        editor.clear(); process();
        editor.setPlainText("colorful"); process();
        QTextBlock colorful = editor.document()->firstBlock();
        editor.setBlockStyleColors(colorful, "#7294ca", "#f2edf8");
        process();
        const auto styledBlocks = editor.structuredBlocks();
        const auto styledObject = styledBlocks[0].toObject();
        require(styledObject.value("text_color").toString() == "#7294ca",
                "text_color serializes on the block");
        require(styledObject.value("background_color").toString() == "#f2edf8",
                "background_color serializes on the block");
        require(colorful.blockFormat().background().color() == QColor("#f2edf8"),
                "the background brush is applied to the block");
        bool coloredFragment = false;
        for (auto it = colorful.begin(); !it.atEnd(); ++it)
            if (it.fragment().isValid() && it.fragment().charFormat().foreground().color() == QColor("#7294ca"))
                coloredFragment = true;
        require(coloredFragment, "the text color reaches the existing fragments");
        RichEditor reloadedColors;
        reloadedColors.setStructuredBlocks(styledBlocks);
        const auto reloadedColorBlocks = reloadedColors.structuredBlocks();
        require(reloadedColorBlocks[0].toObject().value("text_color").toString() == "#7294ca" &&
                reloadedColorBlocks[0].toObject().value("background_color").toString() == "#f2edf8",
                "block style colors survive a save/load roundtrip");
        require(reloadedColors.document()->firstBlock().blockFormat().background().color() == QColor("#f2edf8"),
                "the background is re-applied on load");
        editor.setBlockStyleColors(colorful, QString(), QString());
        process();
        const auto clearedBlocks = editor.structuredBlocks();
        require(!clearedBlocks[0].toObject().contains("text_color") &&
                !clearedBlocks[0].toObject().contains("background_color"),
                "reset removes the color keys from the block");
        require(colorful.blockFormat().background() == QBrush(),
                "reset clears the background brush");

        // ---- mover arriba/abajo y sangría desde el menú del tirador ---------
        editor.clear(); process();
        editor.setPlainText("uno\ndos\ntres"); process();
        const QString dosId = RichEditor::blockId(blockWithText(editor, "dos"));
        editor.moveBlockUp(blockWithText(editor, "dos"));
        process();
        require(editor.document()->firstBlock().text() == "dos" &&
                RichEditor::blockId(editor.document()->firstBlock()) == dosId,
                "move up puts the block first and keeps its id");
        editor.moveBlockDown(editor.document()->firstBlock());
        process();
        require(editor.document()->firstBlock().text() == "uno" &&
                editor.document()->firstBlock().next().text() == "dos",
                "move down restores the order");
        editor.moveBlockUp(editor.document()->firstBlock());
        process();
        require(editor.document()->firstBlock().text() == "uno",
                "move up on the first block is a no-op");
        editor.moveBlockDown(editor.document()->lastBlock());
        process();
        require(editor.document()->lastBlock().text() == "tres",
                "move down on the last block is a no-op");
        editor.indentBlock(editor.document()->firstBlock());
        process();
        require(editor.document()->firstBlock().blockFormat().indent() == 1,
                "indentBlock indents a normal block like Tab");
        editor.outdentBlock(editor.document()->firstBlock());
        process();
        require(editor.document()->firstBlock().blockFormat().indent() == 0,
                "outdentBlock restores the indent like Shift+Tab");
        editor.setTextCursor(QTextCursor(editor.document()->firstBlock()));
        editor.applyBlockStyle("bullet"); process();
        editor.indentBlock(editor.document()->firstBlock());
        process();
        require(editor.document()->firstBlock().textList() &&
                editor.document()->firstBlock().textList()->format().indent() == 2,
                "indentBlock indents a list item like Tab");
        editor.outdentBlock(editor.document()->firstBlock());
        process();
        require(editor.document()->firstBlock().textList() &&
                editor.document()->firstBlock().textList()->format().indent() == 1,
                "outdentBlock outdents a list item like Shift+Tab");

        // Native blocks emitted by the mobile editor must remain native in Qt.
        QFile mobileFixture(QFINDTESTDATA("../../contracts/fixtures/mobile-editor.json"));
        require(mobileFixture.open(QIODevice::ReadOnly), "mobile editor fixture opens");
        const auto fixtureObjects = QJsonDocument::fromJson(mobileFixture.readAll()).object().value("objects").toArray();
        const auto mobileNote = fixtureObjects.at(1).toObject();
        editor.setReferenceData(fixtureObjects, QJsonArray{});
        editor.setStructuredBlocks(mobileNote.value("properties").toObject().value("blocks").toArray());
        process();
        require(editor.toPlainText().contains("Revisar el capítulo") && !editor.toPlainText().contains("lumen://"),
                "mobile object link displays its title instead of source syntax");
        require(blockStyle(editor.document()->findBlockByNumber(2)) == "divider",
                "mobile separator loads as a native divider");
        require(editor.documentReferenceIds().contains(QJsonValue("editor-fixture-task")),
                "mobile object link keeps its target ID in Qt");

        QFile nativeFixture(QFINDTESTDATA("../../contracts/fixtures/native-editor.json"));
        require(nativeFixture.open(QIODevice::ReadOnly), "rich mobile roundtrip fixture opens");
        const auto richObjects = QJsonDocument::fromJson(nativeFixture.readAll()).object().value("objects").toArray();
        editor.setReferenceData(richObjects, QJsonArray{});
        editor.setStructuredBlocks(richObjects.last().toObject().value("properties").toObject().value("blocks").toArray());
        process();
        const auto roundtrip = editor.structuredBlocks();
        require(roundtrip.at(0).toObject().value("type").toString() == "bullet_list", "mobile list remains a list in Qt");
        require(roundtrip.at(0).toObject().value("runs").toArray().first().toObject().value("bold").toBool(), "mobile edit retains bold in Qt");
        require(roundtrip.at(2).toObject().value("level").toInt() == 2, "mobile heading level survives in Qt");
        require(roundtrip.at(4).toObject().value("language").toString() == "kotlin", "mobile code language survives in Qt");
        require(editor.documentReferenceIds().contains(QJsonValue("fixture-image-a")) &&
                editor.documentReferenceIds().contains(QJsonValue("fixture-image-b")), "both inline image IDs survive in Qt");

        const auto captureDirectory = qEnvironmentVariable("LUMEN_EDITOR_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QDir().mkpath(captureDirectory);
            editor.clear(); process();
            editor.setPlainText("Párrafo normal\nTarea convertida\nEncabezado convertido\nViñeta convertida\nCita convertida"); process();
            auto styleAt = [&](int index, const QString &style) {
                auto b = editor.document()->begin();
                for (int i = 0; i < index && b.isValid(); ++i) b = b.next();
                if (!b.isValid()) return;
                editor.setTextCursor(QTextCursor(b));
                editor.applyBlockStyle(style);
                process();
            };
            styleAt(1, "todo");
            styleAt(2, "heading2");
            styleAt(3, "bullet");
            styleAt(4, "quote");
            editor.resize(573, 420);
            editor.setAppearance(true);
            editor.show();
            process();
            require(editor.grab().save(captureDirectory + "/editor-cambio-tipo-bloque.png"),
                    "la captura del cambio de tipo debe guardarse");
        }
    } catch (const std::exception &) {
        return 1;
    }
    printf("EDITOR_BLOCKS_TEST_OK\n");
    return 0;
}
