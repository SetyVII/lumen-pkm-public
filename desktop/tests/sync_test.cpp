#include "fake_api.hpp"
#include "../src/app/main_window.hpp"
#include <QtTest>

static bool waitFor(std::function<bool()> condition) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        if (condition()) return true;
        QTest::qWait(10);
    }
    return condition();
}

static RichEditor *activeEditor(MainWindow &window) {
    for (auto *editor : window.findChildren<RichEditor *>("editor"))
        if (editor->isVisible()) return editor;
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir preferences;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
    FakeApi fake;
    fake.types = {QJsonObject{{"id", "note"}, {"name", "Note"}, {"properties", QJsonArray{}}}};
    QJsonObject note{{"id", "mobile-note"}, {"type_id", "note"}, {"title", "Note"},
                     {"content", "Original"}, {"revision", 1}, {"sync_status", "synced"},
                     {"properties", QJsonObject{}}, {"links", QJsonArray{}}};
    fake.objects = {note};
    if (!fake.start()) return 1;
    qputenv("LUMEN_API_URL", fake.baseUrl().toString().toUtf8());
    qputenv("LUMEN_TOKEN", "sync-test");
    MainWindow window;
    window.show();
    auto check = [](bool ok, const char *message) {
        if (!ok) qCritical() << "FAIL:" << message;
        return ok;
    };
    if (!check(waitFor([&] { return window.ready; }), "workspace loaded")) return 1;
    window.openObject("mobile-note");
    if (!check(waitFor([&] { return activeEditor(window) && activeEditor(window)->toPlainText() == "Original"; }),
               "note opened")) return 1;
    auto *editor = activeEditor(window);
    editor->setFocus();
    auto cursor = editor->textCursor();
    cursor.setPosition(2);
    cursor.setPosition(5, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    note["content"] = "Original\nTest desde el movil";
    note["revision"] = 2;
    fake.replaceObject(note);
    for (int i = 0; i < 5; ++i) window.pollWorkspace();
    if (!check(waitFor([&] { return activeEditor(window) && activeEditor(window)->toPlainText().contains("Test desde el movil"); }),
               "poll updates the open editor without navigation")) {
        qCritical() << "route" << window.currentRoute() << "cache" << window.objectFor("mobile-note")
                    << "editor" << (activeEditor(window) ? activeEditor(window)->toPlainText() : QString("hidden"));
        for (const auto &request : fake.log) qCritical() << request.method << request.path << request.body;
        return 1;
    }
    editor = activeEditor(window);
    if (!check(editor->textCursor().anchor() == 2 && editor->textCursor().position() == 5,
               "remote refresh preserves selection")) return 1;
    if (!check(editor->hasFocus(), "remote refresh preserves editor focus")) return 1;
    for (const auto &request : fake.log)
        if (!check(request.method != "PATCH", "remote refresh does not write the object back")) return 1;

    // A local edit made while a remote poll runs must keep both its text and
    // its original base revision, so a later save still detects the conflict.
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(" local draft");
    note["content"] = "Another device edited this";
    note["revision"] = 3;
    fake.replaceObject(note);
    for (int i = 0; i < 5; ++i) window.pollWorkspace();
    if (!check(waitFor([&] { return window.objectFor("mobile-note").value("revision").toInt() == 3; }),
               "poll sees the concurrent edit")) return 1;
    if (!check(activeEditor(window)->toPlainText().endsWith(" local draft"), "local draft survives the poll")) return 1;
    window.saveCurrent();
    if (!check(waitFor([&] {
        for (const auto &request : fake.log)
            if (request.method == "PATCH") return request.body.value("base_revision").toInt() == 2;
        return false;
    }), "local save retains the pre-conflict base revision")) return 1;

    window.navigateJournal(QDate::currentDate());
    if (!check(waitFor([&] {
        return window.currentRoute() == "journal" && activeEditor(window) && activeEditor(window)->toPlainText().isEmpty();
    }), "daily note opened")) return 1;
    const auto journalId = "journal-" + QDate::currentDate().toString(Qt::ISODate);
    auto journal = fake.findObject(journalId);
    journal["content"] = "Test desde el movil";
    journal["revision"] = 2;
    fake.replaceObject(journal);
    const int requestCount = fake.log.size();
    for (int i = 0; i < 5; ++i) window.pollWorkspace();
    if (!check(waitFor([&] {
        return activeEditor(window) && activeEditor(window)->toPlainText() == "Test desde el movil";
    }), "poll also updates the open daily note")) return 1;
    QTest::qWait(550);
    for (int i = requestCount; i < fake.log.size(); ++i)
        if (!check(fake.log[i].method != "PATCH", "loading remote daily text does not schedule an autosave")) return 1;
    qInfo() << "SYNC_TEST_OK";
    return 0;
}
