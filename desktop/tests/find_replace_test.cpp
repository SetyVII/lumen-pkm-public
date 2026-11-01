// Find in page and replace over the open object (desktop stage
// "find-replace"). Two layers:
//   1. RichEditor directly: matching across paragraphs, toggles, code and
//      tables, counter accuracy, wrap-around, options, replace/undo and
//      read-only reference chips.
//   2. MainWindow against the in-process fake API: the Ctrl+F path, the
//      autosave body after Replace all, one undo restoring every block, and
//      the session query surviving an object switch.
// Everything runs offscreen; no real server and no network beyond loopback.
#include "fake_api.hpp"
#include "../src/app/main_window.hpp"
#include <QtTest>
#include <cstdio>
#include <stdexcept>

namespace {
bool check(bool value, const char *message) {
    if (!value) fprintf(stderr, "FAIL: %s\n", message);
    return value;
}
void require(bool condition, const char *message) {
    if (!condition) {
        qCritical() << "FIND_REPLACE_TEST_FAILED:" << message;
        throw std::runtime_error(message);
    }
}
void process() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QTest::qWait(30);
}
bool waitFor(std::function<bool()> condition, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        if (condition()) return true;
        QTest::qWait(20);
    }
    return condition();
}
template <class Widget>
Widget *visibleChild(MainWindow &window, const QString &name) {
    for (auto *widget : window.findChildren<Widget *>(name))
        if (widget->isVisible()) return widget;
    return nullptr;
}
QTextBlock blockWithText(RichEditor &editor, const QString &needle) {
    for (auto block = editor.document()->begin(); block.isValid(); block = block.next())
        if (block.text().contains(needle)) return block;
    return {};
}
QString visibleDocumentTitle(MainWindow &window) {
    for (auto *title : window.findChildren<DocumentTitle *>("documentTitle"))
        if (title->isVisible()) return title->text();
    return {};
}
QString allBlockText(RichEditor &editor) {
    QStringList lines;
    for (auto block = editor.document()->begin(); block.isValid(); block = block.next())
        lines << block.text();
    return lines.join('\n');
}
QJsonArray savedBlocks(const FakeApi &fake, const QString &objectId) {
    return fake.findObject(objectId).value("properties").toObject().value("blocks").toArray();
}
QString savedText(const FakeApi &fake, const QString &objectId) {
    QStringList lines;
    for (const auto &value : savedBlocks(fake, objectId)) {
        const auto block = value.toObject();
        lines << block.value("text").toString();
        for (const auto &child : block.value("children").toArray())
            lines << child.toObject().value("text").toString();
    }
    return lines.join('\n');
}
bool fakeSaw(const FakeApi &fake, const QString &method, const QString &pathPart,
             const QString &bodyPart = {}) {
    for (const auto &request : fake.log)
        if (request.method == method && request.path.contains(pathPart) &&
            (bodyPart.isEmpty() ||
             QJsonDocument(request.body).toJson(QJsonDocument::Compact).contains(bodyPart.toUtf8())))
            return true;
    return false;
}
void appendParagraph(RichEditor &editor, const QString &text, const QString &style = {}) {
    auto cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.insertText(text);
    editor.setTextCursor(cursor);
    if (!style.isEmpty()) editor.applyBlockStyle(style);
    process();
}
void insertTableWithText(RichEditor &editor, const QString &cellText) {
    auto cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    editor.setTextCursor(cursor);
    editor.insertTableBlock(2, 2);
    process();
    QTextTable *table = nullptr;
    for (auto *frame : editor.document()->rootFrame()->childFrames())
        if (auto *candidate = qobject_cast<QTextTable *>(frame)) table = candidate;
    require(table != nullptr, "the table block exists");
    auto first = table->cellAt(0, 0).firstCursorPosition();
    first.insertText(cellText);
    auto second = table->cellAt(0, 1).firstCursorPosition();
    second.insertText("Cell B");
    process();
}
QLineEdit *findQueryEdit(QWidget &bar) { return bar.findChild<QLineEdit *>("findQuery"); }
QLabel *findCounter(QWidget &bar) { return bar.findChild<QLabel *>("findCounter"); }
QLabel *findNotice(QWidget &bar) { return bar.findChild<QLabel *>("findNotice"); }
}  // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    bool pass = true;

    // ---------------------------------------------------------------------
    // 1. RichEditor: matching, navigation, options, replace and undo.
    // ---------------------------------------------------------------------
    try {
        RichEditor editor;
        editor.resize(760, 640);
        editor.show();
        process();
        editor.clear();
        process();
        editor.setPlainText("Alpha intro needle");
        process();

        // Closed toggle with an indented child (the flat Lumen model: the
        // marker is the only fold state, content is always in the document).
        {
            auto cursor = editor.textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            cursor.insertText("Secrets");
            editor.setTextCursor(cursor);
            editor.applyBlockStyle("toggle");
            process();
            cursor = editor.textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            cursor.insertText("Hidden Needle one");
            editor.setTextCursor(cursor);
            process();
            editor.indentBlock(editor.textCursor().block());
            process();
        }
        appendParagraph(editor, "Tail needle two");
        appendParagraph(editor, "int needle = 1;", "code");
        {
            auto cursor = editor.textCursor();
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.insertText(QString(QChar(QChar::LineSeparator)) + "return needle;");
            editor.setTextCursor(cursor);
            process();
        }
        appendParagraph(editor, "Needles appear");
        {
            auto cursor = editor.textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            editor.setTextCursor(cursor);
            editor.insertObjectReference(
                QJsonObject{{"id", "o1"}, {"title", "Project Alpha"}, {"type_id", "project"}});
            process();
        }
        insertTableWithText(editor, "Table needle");

        require(blockWithText(editor, "▶ Secrets").isValid(), "the toggle starts closed");
        require(blockWithText(editor, "Needles appear").isValid(), "plural paragraph exists");
        require(allBlockText(editor).contains("Project Alpha"), "the reference chip is in the document");
        require(allBlockText(editor).contains("Table needle"), "the table cell is in the document");

        editor.showFindBar();
        process();
        auto *bar = editor.findChild<QFrame *>("findBar");
        require(bar && bar->isVisible(), "Ctrl+F/API opens the find bar");
        auto *query = findQueryEdit(*bar);
        auto *counter = findCounter(*bar);
        auto *notice = findNotice(*bar);
        require(query && counter && notice, "the find bar exposes query, counter and notice");

        query->setText("needle");
        process();
        require(editor.findMatchCount() == 7, "case-insensitive substring finds paragraphs, toggle child, code and table");
        require(counter->text() == "1/7", "the counter starts at the first match");
        require(editor.textCursor().selectedText() == "needle", "the first match is selected");
        require(editor.findCurrentMatch() == 1, "the current match is 1-based");
        // The ShortcutOverride guard must not block normal typing.
        query->clear();
        process();
        QTest::keyClicks(query, "needle");
        process();
        require(query->text() == "needle" && editor.findMatchCount() == 7, "typing in the find field works");

        // Next / previous with wrap-around. Lumen toggles never hide content,
        // so a match in a toggle child is found without mutating the marker.
        const QString textBeforeNavigation = allBlockText(editor);
        require(editor.findNextMatch(), "next selects the second match");
        require(counter->text() == "2/7", "the counter follows next");
        require(editor.textCursor().selectedText() == "Needle", "the second match is the capitalised one");
        require(blockWithText(editor, "▶ Secrets").isValid(), "search does not flip a closed toggle");
        require(allBlockText(editor) == textBeforeNavigation, "navigation never mutates the document");
        require(editor.findPreviousMatch(), "previous goes back");
        require(counter->text() == "1/7", "previous returns to the first match");
        require(editor.findPreviousMatch(), "previous from the first match wraps");
        require(counter->text() == "7/7", "wrap-around reaches the last match (table)");
        require(editor.textCursor().selectedText() == "needle", "the wrapped match is selected");
        require(editor.findNextMatch(), "next from the last match wraps");
        require(counter->text() == "1/7", "wrap-around returns to the first match");
        require(editor.extraSelections().size() == 7, "every match is highlighted while the bar is open");

        // Match case and whole word.
        query->setText("Needle");
        process();
        require(editor.findMatchCount() == 7, "case-insensitive matching ignores the query case");
        editor.setFindMatchCase(true);
        process();
        require(editor.findMatchCount() == 2, "match case keeps Needle and Needles, drops the lowercase ones");
        editor.setFindWholeWord(true);
        process();
        require(editor.findMatchCount() == 1, "whole word keeps the standalone Needle");
        editor.setFindMatchCase(false);
        process();
        require(editor.findMatchCount() == 6, "whole word excludes Needles without match case");
        editor.setFindWholeWord(false);
        process();
        require(editor.findMatchCount() == 7, "clearing the options restores the substring count");

        // Replace current advances to the next remaining match.
        query->setText("needle");
        process();
        require(editor.findMatchCount() == 7, "the replace run starts from seven matches");
        require(editor.replaceFindCurrent("thread"), "replace current succeeds on editable text");
        require(blockWithText(editor, "Alpha intro thread").isValid(), "the first match is replaced");
        require(editor.findMatchCount() == 6, "the replaced match leaves the list");
        require(editor.findCurrentMatch() == 1, "replace current advances to the next remaining match");
        require(editor.textCursor().selectedText() == "Needle", "the advanced match is selected");

        // Replace all: every editable block, one undo step. Table cells are
        // counted but skipped (table serialization is a known follow-up).
        const QString beforeAll = allBlockText(editor);
        const int replaced = editor.replaceFindAll("thread");
        process();
        require(replaced == 5, "replace all updates every editable match");
        const QString afterAll = allBlockText(editor);
        require(afterAll.count("needle", Qt::CaseInsensitive) == 1, "only the table cell keeps the query");
        require(afterAll.contains("Table needle"), "the table cell is skipped, not corrupted");
        require(afterAll.contains("Hidden thread one"), "the toggle child is replaced");
        require(afterAll.contains("int thread = 1;"), "the code block is replaced");
        require(afterAll.contains("return thread;"), "the second code line is replaced");
        require(afterAll.contains("threads appear"), "the plural keeps its suffix");
        require(afterAll.contains("Project Alpha"), "the reference chip is untouched");
        require(notice->text().contains("Replaced 5"), "replace all reports the count");
        require(notice->text().contains("table cell"), "replace all reports the skipped table");
        editor.undo();
        process();
        require(allBlockText(editor) == beforeAll, "ONE undo restores every replaced block");

        // Reference chips are matched but read-only.
        query->setText("Project Alpha");
        process();
        require(editor.findMatchCount() == 1, "the chip title is matched");
        require(!editor.replaceFindCurrent("Renamed"), "replace current refuses a reference chip");
        require(notice->text().contains("read-only"), "the chip refusal is explained");
        require(allBlockText(editor).contains("Project Alpha"), "the chip title is unchanged");
        require(editor.replaceFindAll("Renamed") == 0, "replace all skips the only chip match");
        require(allBlockText(editor).contains("Project Alpha"), "replace all leaves the chip title intact");

        // Table cells are searchable but not replaceable yet: structuredBlocks()
        // flattens tables, so a replace would silently lose the table on save.
        query->setText("Table needle");
        process();
        require(editor.findMatchCount() == 1, "the table cell is searchable");
        require(!editor.replaceFindCurrent("Renamed"), "replace current refuses a table cell");
        require(notice->text().contains("Table cells"), "the table refusal is explained");
        require(allBlockText(editor).contains("Table needle"), "the table cell is unchanged");
        require(editor.replaceFindAll("Renamed") == 0, "replace all skips the table cell");
        require(notice->text().contains("Not replaceable"), "replace all explains the table skip");
        require(allBlockText(editor).contains("Table needle"), "replace all leaves the table intact");

        // No-match state is explicit and non-blocking.
        query->setText("zzz");
        process();
        require(editor.findMatchCount() == 0, "no matches are reported");
        require(counter->text() == "0/0", "the counter shows 0/0");
        require(notice->isVisible() && notice->text() == "No matches", "the no-match notice is visible");
        require(bar->isVisible(), "the bar stays open on no matches");

        // Closing keeps the session; reopening restores and selects the query.
        query->setText("needle");
        process();
        editor.setFindReplaceVisible(true);
        process();
        auto *replaceEdit = bar->findChild<QLineEdit *>("findReplace");
        require(replaceEdit && replaceEdit->isVisible(), "the replace row is visible");
        editor.closeFindBar();
        process();
        require(!bar->isVisible(), "closing hides the bar");
        require(editor.extraSelections().isEmpty(), "closing clears the match highlights");
        QTest::keyClick(&editor, Qt::Key_F, Qt::ControlModifier);
        process();
        require(bar->isVisible(), "Ctrl+F reopens the bar");
        require(query->text() == "needle", "the query survives closing");
        require(query->selectedText() == "needle", "the restored query is selected");
        require(replaceEdit->isVisible(), "the replace row visibility survives closing");
        require(counter->text() == "1/6", "reopening refreshes the counter");

        // F3 / Shift+F3 repeat the search without stealing other shortcuts.
        QTest::keyClick(&editor, Qt::Key_F3);
        process();
        require(counter->text() == "2/6", "F3 moves to the next match");
        QTest::keyClick(&editor, Qt::Key_F3, Qt::ShiftModifier);
        process();
        require(counter->text() == "1/6", "Shift+F3 moves to the previous match");
        QTest::keyClick(query, Qt::Key_Return);
        process();
        require(counter->text() == "2/6", "Enter in the query field goes to the next match");
        QTest::keyClick(query, Qt::Key_Up);
        process();
        require(counter->text() == "1/6", "Up goes to the previous match");
        QTest::keyClick(query, Qt::Key_Escape);
        process();
        require(!bar->isVisible(), "Esc closes the bar from the find field");
    } catch (const std::exception &error) {
        fprintf(stderr, "FAIL: %s\n", error.what());
        pass = false;
    }

    // ---------------------------------------------------------------------
    // 2. MainWindow + fake API: Ctrl+F, autosave and session across objects.
    // ---------------------------------------------------------------------
    FakeApi fake;
    fake.types = QJsonArray{
        QJsonObject{{"id", "note"}, {"name", "Note"}, {"icon", "file-text"}, {"color", "#b99854"}},
        QJsonObject{{"id", "project"}, {"name", "Project"}, {"icon", "box"}, {"color", "#6d9b7e"}},
        QJsonObject{{"id", "book"}, {"name", "Book"}, {"icon", "book"}, {"color", "#d6ba51"}}};
    auto seedObject = [](const QString &id, const QString &type, const QString &title) {
        return QJsonObject{{"id", id}, {"type_id", type}, {"title", title}, {"content", ""},
                           {"links", QJsonArray{}}, {"properties", QJsonObject{}}, {"revision", 1},
                           {"created_at", "2026-10-01T10:00:00.000Z"}, {"updated_at", "2026-10-01T10:00:00.000Z"},
                           {"sync_status", "synced"}};
    };
    auto run = [](const QString &text) {
        return QJsonArray{QJsonObject{{"text", text}}};
    };
    QJsonArray blocks{
        QJsonObject{{"id", "fr-b1"}, {"type", "paragraph"}, {"text", "Save needle one"}, {"runs", run("Save needle one")}},
        QJsonObject{{"id", "fr-b2"}, {"type", "toggle"}, {"text", QString::fromUtf8("▶ Save needle two")},
                    {"runs", run(QString::fromUtf8("▶ Save needle two"))}},
        QJsonObject{{"id", "fr-b3"}, {"type", "paragraph"}, {"text", "Save needle three"}, {"indent", 1},
                    {"runs", run("Save needle three")}},
        QJsonObject{{"id", "fr-b4"}, {"type", "paragraph"}, {"text", " Project Alpha"},
                    {"runs", QJsonArray{QJsonObject{{"text", " Project Alpha"},
                                                    {"href", "lumen://object/o1"},
                                                    {"type_id", "project"}}}}}};
    auto findObject = seedObject("fr1", "note", "Find target");
    auto properties = findObject.value("properties").toObject();
    properties["blocks"] = blocks;
    findObject["properties"] = properties;
    fake.objects = QJsonArray{findObject, seedObject("b1", "book", "Other object")};
    if (!fake.start()) {
        fprintf(stderr, "FAIL: fake API could not listen on loopback\n");
        return 1;
    }
    qputenv("LUMEN_API_URL", fake.baseUrl().toString().toUtf8());
    qputenv("LUMEN_TOKEN", "find-replace-test");

    MainWindow window;
    window.resize(1448, 900);
    window.show();
    pass &= check(waitFor([&] { return window.ready; }), "startup reached ready state against the fake API");

    try {
        window.openObject("fr1");
        require(waitFor([&] { return visibleDocumentTitle(window) == "Find target"; }),
                "the object page opens");
        auto *editor = visibleChild<RichEditor>(window, "editor");
        require(editor != nullptr, "the object page renders its editor");
        editor->setFocus();
        QTest::keyClick(editor, Qt::Key_F, Qt::ControlModifier);
        auto *bar = waitFor([&] { return visibleChild<QFrame>(window, "findBar"); })
                        ? visibleChild<QFrame>(window, "findBar")
                        : nullptr;
        require(bar != nullptr, "Ctrl+F opens the find bar over the object");
        auto *query = findQueryEdit(*bar);
        auto *counter = findCounter(*bar);
        require(query && counter, "the bar is wired");
        query->setText("needle");
        process();
        require(editor->findMatchCount() == 3, "the open object has three matches");
        require(counter->text() == "1/3", "the counter is live");
        require(editor->textCursor().selectedText() == "needle", "the first match is selected");

        // App shortcuts must not fire while the find field has focus.
        const int weightBefore = editor->textCursor().charFormat().fontWeight();
        QTest::keyClick(query, Qt::Key_B, Qt::ControlModifier);
        process();
        require(editor->textCursor().charFormat().fontWeight() == weightBefore,
                "Ctrl+B in the find field does not format the note");
        QTest::keyClick(query, Qt::Key_R, Qt::ControlModifier);
        process();
        require(editor->findReplaceVisible(), "Ctrl+R in the find field opens the replace row");
        QTest::keyClick(query, Qt::Key_R, Qt::ControlModifier);
        process();
        require(!editor->findReplaceVisible(), "Ctrl+R toggles the replace row");

        // Navigate into the closed toggle's child: content is always visible,
        // so the marker must not change (no document mutation from search).
        editor->findNextMatch();
        editor->findNextMatch();
        process();
        require(counter->text() == "3/3", "navigation reaches the toggle child");
        require(blockWithText(*editor, QString::fromUtf8("▶ Save needle two")).isValid(),
                "search navigation does not flip the closed toggle");

        // Replace all goes through the normal save path.
        editor->setFindReplaceVisible(true);
        process();
        auto *replaceEdit = bar->findChild<QLineEdit *>("findReplace");
        auto *replaceAllButton = bar->findChild<QPushButton *>("findReplaceAll");
        require(replaceEdit && replaceAllButton, "the replace row is available");
        replaceEdit->setText("thread");
        QTest::mouseClick(replaceAllButton, Qt::LeftButton);
        process();
        require(editor->findMatchCount() == 0, "replace all removes every match");
        require(waitFor([&] { return fakeSaw(fake, "PATCH", "/v1/objects/fr1", "thread"); }),
                "replace all reaches the autosave PATCH");
        const QString savedAfterReplace = savedText(fake, "fr1");
        require(savedAfterReplace.contains("Save thread one"), "the first block is saved");
        require(savedAfterReplace.contains(QString::fromUtf8("▶ Save thread two")), "the toggle block is saved");
        require(savedAfterReplace.contains("Save thread three"), "the toggle child is saved");
        require(!savedAfterReplace.contains("needle"), "the saved object has no needle left");

        // ONE undo restores all blocks and saves them again.
        editor->undo();
        process();
        require(allBlockText(*editor).contains("Save needle one"), "undo restores the first block");
        require(allBlockText(*editor).contains(QString::fromUtf8("▶ Save needle two")), "undo restores the toggle block");
        require(allBlockText(*editor).contains("Save needle three"), "undo restores the toggle child");
        require(waitFor([&] { return fakeSaw(fake, "PATCH", "/v1/objects/fr1", "Save needle three"); }),
                "the undo is saved through the same path");

        // Switching objects hides the bar; reopening restores the session.
        window.openObject("b1");
        require(waitFor([&] { return visibleDocumentTitle(window) == "Other object"; }),
                "switching objects replaces the editor");
        process();
        require(visibleChild<QFrame>(window, "findBar") == nullptr, "the bar hides when switching objects");
        window.openObject("fr1");
        require(waitFor([&] { return visibleDocumentTitle(window) == "Find target"; }),
                "the object reopens");
        auto *reopened = waitFor([&] { return visibleChild<RichEditor>(window, "editor"); })
                             ? visibleChild<RichEditor>(window, "editor")
                             : nullptr;
        require(reopened != nullptr, "the reopened page has an editor");
        reopened->setFocus();
        QTest::keyClick(reopened, Qt::Key_F, Qt::ControlModifier);
        auto *reopenedBar = waitFor([&] { return visibleChild<QFrame>(window, "findBar"); })
                                ? visibleChild<QFrame>(window, "findBar")
                                : nullptr;
        require(reopenedBar != nullptr, "Ctrl+F reopens the bar on the new page");
        auto *reopenedQuery = findQueryEdit(*reopenedBar);
        require(reopenedQuery && reopenedQuery->text() == "needle", "the session query is restored");
        require(reopenedQuery->selectedText() == "needle", "the restored query is selected");
        require(findCounter(*reopenedBar)->text() == "1/3", "the restored search finds the object again");
    } catch (const std::exception &error) {
        fprintf(stderr, "FAIL: %s\n", error.what());
        pass = false;
    }

    if (pass)
        fprintf(stdout,
                "LUMEN_FIND_REPLACE_OK: multi-block/toggle/table/code matching, counter, wrap-around, "
                "match case/whole word, replace current/all, one-step undo, read-only chips, no-match, "
                "session reopen, Ctrl+F and autosave save path\n");
    return pass ? 0 : 1;
}
