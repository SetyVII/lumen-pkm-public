// Aliases (desktop stage "aliases-ui", D6) against the in-process fake API
// (tests/fake_api.hpp), extended with the aliases contract slice
// (docs/implementation/backend/aliases.md + contracts/SYNC.md "Aliases"):
// the per-object alias editor (add via Enter, remove via the chip's x and
// Backspace), the properties PATCH with the server's normalization and
// limits (400s surfaced non-blocking), the object-page display, the command
// palette over GET /v1/search with matched_alias, the signature
// matched-alias insertion rule in the editor @-dropdown, the type-page
// search surfacing alias matches, aliases riding the normal save path, and
// the header chips row (WrapBar) never clipping a label. No server, no
// network beyond loopback inside the test process, entirely offscreen.
#include "fake_api.hpp"
#include "../src/app/main_window.hpp"
#include <QDir>
#include <QSettings>
#include <QtTest>
#include <cstdio>

static bool check(bool value, const char *message) {
    if (!value)
        fprintf(stderr, "FAIL: %s\n", message);
    return value;
}
static bool waitFor(std::function<bool()> condition, int timeout = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeout) {
        if (condition())
            return true;
        QTest::qWait(20);
    }
    return condition();
}
template <class Widget>
static Widget *visibleChild(MainWindow &window, const QString &name) {
    for (auto *widget : window.findChildren<Widget *>(name))
        if (widget->isVisible())
            return widget;
    return nullptr;
}
static LumenViews *activeViews(MainWindow &window) {
    for (auto *views : window.findChildren<LumenViews *>("workspaceViews"))
        if (views->isVisible())
            return views;
    return nullptr;
}
static QString pageTitle(MainWindow &window) {
    auto *heading = visibleChild<QTextEdit>(window, "documentTitle");
    return heading ? heading->toPlainText() : QString();
}
static QAction *menuAction(QMenu *menu, const QString &text) {
    if (!menu)
        return nullptr;
    for (auto *action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}
static bool fakeSaw(const FakeApi &fake, const QString &method, const QString &pathPart) {
    for (const auto &request : fake.log)
        if (request.method == method && request.path.contains(pathPart))
            return true;
    return false;
}
static int countRequests(const FakeApi &fake, const QString &method, const QString &pathPart) {
    int count = 0;
    for (const auto &request : fake.log)
        if (request.method == method && request.path.contains(pathPart))
            ++count;
    return count;
}
static const FakeApi::Request *lastRequest(const FakeApi &fake, const QString &method,
                                           const QString &pathPart) {
    for (int i = fake.log.size() - 1; i >= 0; --i)
        if (fake.log[i].method == method && fake.log[i].path.contains(pathPart))
            return &fake.log[i];
    return nullptr;
}
// Drives the alias editor dialog opened (modally) by `open`: types each
// alias + Enter, then optionally clicks chips/remove keys, then saves or
// cancels. The helpers run while open() blocks in exec().
static void driveAliasDialog(MainWindow &window, const QStringList &type,
                            const std::function<void(QDialog *)> &extra, bool save) {
    QTimer::singleShot(0, &window, [&window, type, extra, save]() {
        auto *dialog = window.findChild<QDialog *>("aliasDialog");
        auto *input = dialog ? dialog->findChild<QLineEdit *>("aliasInput") : nullptr;
        if (!dialog || !input)
            return;
        for (const auto &alias : type) {
            QTest::keyClicks(input, alias);
            QTest::keyClick(input, Qt::Key_Return);
        }
        if (extra)
            extra(dialog);
        QTest::qWait(20);
        if (save) {
            auto *box = dialog->findChild<QDialogButtonBox *>();
            if (auto *ok = box ? box->button(QDialogButtonBox::Save) : nullptr)
                ok->click();
            else
                dialog->reject();
        } else {
            dialog->reject();
        }
    });
}
// Finds a palette result row by its object id.
static QListWidgetItem *paletteRow(MainWindow &window, const QString &objectId) {
    auto *dialog = window.findChild<QDialog *>("commandPalette");
    auto *list = dialog ? dialog->findChild<QListWidget *>() : nullptr;
    if (!list)
        return nullptr;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->data(Qt::UserRole).toString() == objectId)
            return list->item(i);
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    const QString configHome =
        QDir::temp().filePath(QString("lumen-aliases-test-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(configHome);
    qputenv("XDG_CONFIG_HOME", configHome.toUtf8());
    bool pass = true;

    FakeApi fake;
    fake.types = QJsonArray{
        QJsonObject{{"id", "book"}, {"name", "Book"}, {"icon", "book"}, {"color", "#d6ba51"},
                    {"properties", QJsonArray{
                        QJsonObject{{"key", "tags"}, {"type", "tags"}},
                        QJsonObject{{"key", "author"}, {"type", "text"}}}}},
        QJsonObject{{"id", "journal"}, {"name", "Journal"}, {"icon", "calendar"}, {"color", "#48c08a"},
                    {"properties", QJsonArray{QJsonObject{{"key", "date"}, {"type", "date"}}}}},
        QJsonObject{{"id", "person"}, {"name", "Person"}, {"icon", "user"}, {"color", "#e0a458"},
                    {"properties", QJsonArray{}}},
        QJsonObject{{"id", "collection"}, {"name", "Collection"}, {"icon", "view-list-details"}, {"color", "#8f7fb5"},
                    {"properties", QJsonArray{
                        QJsonObject{{"key", "item_type_id"}, {"type", "text"}},
                        QJsonObject{{"key", "items"}, {"type", "object"}, {"multiple", true}}}}},
        QJsonObject{{"id", "query"}, {"name", "Query"}, {"icon", "filter"}, {"color", "#7f8fb5"},
                    {"properties", QJsonArray{}}}};
    auto seed = [](const QString &id, const QString &type, const QString &title, const QString &content,
                   const QJsonObject &properties) {
        const QString stamp = QString("2026-10-%1T09:00:00.000Z").arg(id == "b1" ? "02" : "03");
        return QJsonObject{{"id", id}, {"type_id", type}, {"title", title}, {"content", content},
                            {"links", QJsonArray{}}, {"property_links", QJsonArray{}},
                            {"properties", properties}, {"revision", 1},
                            {"created_at", stamp}, {"updated_at", stamp}, {"sync_status", "synced"}};
    };
    fake.objects = QJsonArray{
        seed("b1", "book", "Meeting notes", "Weekly sync.", QJsonObject{{"author", "Grace"}}),
        seed("b2", "book", "HP1", "First edition.", QJsonObject{{"author", "Rowling"}}),
        seed("p1", "person", "Ada", "Analytical engine.", QJsonObject{}),
        seed("tpl-book", "book", "Book template", "Template body.",
             QJsonObject{{"is_template", true}})};
    if (!fake.start()) {
        fprintf(stderr, "FAIL: fake API could not listen on loopback\n");
        return 1;
    }
    qputenv("LUMEN_API_URL", fake.baseUrl().toString().toUtf8());
    qputenv("LUMEN_TOKEN", "aliases-test");

    MainWindow window;
    window.resize(1448, 900);
    window.show();
    pass &= check(waitFor([&] { return window.ready; }), "startup reached ready state against the fake API");

    // 1. The object page of an object without aliases shows no aliases row.
    window.openObject("b1");
    pass &= check(waitFor([&] { return window.currentRoute() == "object" && pageTitle(window) == "Meeting notes"; }),
                  "the object page opens");
    pass &= check(visibleChild<QLabel>(window, "aliasesLabel") == nullptr,
                  "an object without aliases shows no aliases display");

    // 2. The "..." Customize menu carries the Aliases entry and opens the
    //    editor from the open page (preview shares the same action).
    {
        bool menuHasEntry = false, dialogOpened = false;
        window.openObject("b2");
        waitFor([&] { return pageTitle(window) == "HP1"; });
        QTimer::singleShot(0, &window, [&window, &menuHasEntry, &dialogOpened]() {
            // The exec'ing menu is the active popup; scanning every child
            // QMenu would touch Qt-internal menus (the editors' text
            // controls own some), so do not iterate findChildren here.
            auto *target = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            menuHasEntry = target && menuAction(target, "Aliases…") != nullptr;
            // Triggering opens the alias dialog with its own nested exec:
            // register its driver BEFORE the trigger call blocks.
            QTimer::singleShot(30, &window, [&window, &dialogOpened]() {
                auto *dialog = window.findChild<QDialog *>("aliasDialog");
                dialogOpened = dialog && dialog->isVisible();
                if (dialog)
                    dialog->reject();
            });
            if (auto *action = target ? menuAction(target, "Aliases…") : nullptr)
                action->trigger();
            if (target && !menuHasEntry)
                target->close();
        });
        window.showObjectOptionsMenu(nullptr);
        pass &= check(menuHasEntry, "the object menu offers Aliases…");
        pass &= check(dialogOpened, "Aliases… opens the alias editor from the page");
    }

    // 3. Adding aliases through the editor: Enter commits a chip, the typed
    //    duplicate is skipped (the server dedupes case-insensitively), the
    //    Save PATCHes the whole editable property map, the server trims,
    //    and the page shows the aliases under the title.
    driveAliasDialog(window, {"  Harry Potter  ", "The Stone", "harry potter"}, nullptr, true);
    window.showAliasEditor("b2");
    pass &= check(waitFor([&] { return lastRequest(fake, "PATCH", "/v1/objects/b2") != nullptr; }),
                  "saving aliases PATCHes the object");
    {
        const auto &request = *lastRequest(fake, "PATCH", "/v1/objects/b2");
        const auto properties = request.body.value("properties").toObject();
        QStringList sent;
        for (auto v : properties.value("aliases").toArray()) sent << v.toString();
        pass &= check(sent == QStringList{"Harry Potter", "The Stone"},
                      "the PATCH sends the aliases array without the duplicate");
        pass &= check(properties.value("author").toString() == "Rowling",
                      "the PATCH carries the whole editable property map");
    }
    pass &= check(waitFor([&] {
                      const auto stored = fake.findObject("b2");
                      return FakeApi::objectAliases(stored) == QStringList{"Harry Potter", "The Stone"};
                  }),
                  "the server-side normalization trims and stores the aliases");
    pass &= check(waitFor([&] {
                      auto *aliases = visibleChild<QLabel>(window, "aliasesLabel");
                      return aliases && aliases->text() == "Harry Potter, The Stone";
                  }),
                  "the object page shows the aliases near the title");

    // 4. Removing: a chip's x removes that alias, Backspace on the empty
    //    input removes the last one, and an empty list removes the key
    //    (the properties document is a full replacement).
    driveAliasDialog(window, {}, [](QDialog *dialog) {
        for (auto *remove : dialog->findChildren<QToolButton *>("aliasChipRemove"))
            if (remove->property("aliasText").toString() == "The Stone") {
                remove->click();
                break;
            }
    }, true);
    window.showAliasEditor("b2");
    pass &= check(waitFor([&] {
                      const auto *request = lastRequest(fake, "PATCH", "/v1/objects/b2");
                      if (!request)
                          return false;
                      QStringList sent;
                      for (auto v : request->body.value("properties").toObject().value("aliases").toArray())
                          sent << v.toString();
                      return sent == QStringList{"Harry Potter"};
                  }),
                  "the chip's x removes exactly that alias");
    // The label refresh runs in the PATCH response callback; waiting for it
    // guarantees the client cache (and the next dialog) sees fresh aliases.
    pass &= check(waitFor([&] {
                      auto *aliases = visibleChild<QLabel>(window, "aliasesLabel");
                      return aliases && aliases->text() == "Harry Potter";
                  }),
                  "the aliases display refreshes after the removal");
    driveAliasDialog(window, {}, [](QDialog *dialog) {
        if (auto *input = dialog->findChild<QLineEdit *>("aliasInput"))
            QTest::keyClick(input, Qt::Key_Backspace);
    }, true);
    window.showAliasEditor("b2");
    pass &= check(waitFor([&] {
                      const auto *request = lastRequest(fake, "PATCH", "/v1/objects/b2");
                      return request && !request->body.value("properties").toObject().contains("aliases");
                  }),
                  "Backspace on the empty input removes the last alias");
    pass &= check(waitFor([&] { return visibleChild<QLabel>(window, "aliasesLabel") == nullptr; }),
                  "an empty alias list removes the page display");
    pass &= check(waitFor([&] { return !fake.findObject("b2").value("properties").toObject().contains("aliases"); }),
                  "the stored object has no aliases key");

    // 5. Re-add the aliases for the search sections below.
    driveAliasDialog(window, {"Harry Potter", "The Stone"}, nullptr, true);
    window.showAliasEditor("b2");
    pass &= check(waitFor([&] {
                      auto *aliases = visibleChild<QLabel>(window, "aliasesLabel");
                      return aliases && aliases->text() == "Harry Potter, The Stone";
                  }),
                  "aliases restored for the search sections");

    // 6. Limits are the server's contract: violations 400 and surface
    //    through the app's non-blocking notice; the dialog is not blocked
    //    and the stored aliases survive.
    const QString tooLong(201, 'a');
    driveAliasDialog(window, {tooLong}, nullptr, true);
    window.showAliasEditor("b2");
    pass &= check(waitFor([&] { return window.failure.contains("200"); }),
                  "the over-length alias surfaces the server's 400 non-blocking");
    pass &= check(waitFor([&] {
                      return FakeApi::objectAliases(fake.findObject("b2")) ==
                             QStringList{"Harry Potter", "The Stone"};
                  }),
                  "a rejected save does not change the stored aliases");
    window.failure.clear();
    {
        QStringList fiftyOne;
        for (int i = 1; i <= 51; ++i) fiftyOne << QString("alias %1").arg(i);
        driveAliasDialog(window, fiftyOne, nullptr, true);
        window.showAliasEditor("b2");
        pass &= check(waitFor([&] { return window.failure.contains("50"); }),
                      "the 51st alias surfaces the server's limit 400");
        pass &= check(window.currentRoute() == "object" && pageTitle(window) == "HP1",
                      "the limit error never blocks the app");
        window.failure.clear();
    }

    // 7. The command palette: typed queries go to GET /v1/search, an
    //    alias-matched result carries the alias hint, picking it opens the
    //    object, and a title hit carries no hint.
    {
        QTimer::singleShot(0, &window, [&window]() {
            auto *dialog = window.findChild<QDialog *>("commandPalette");
            if (auto *search = dialog ? dialog->findChild<QLineEdit *>() : nullptr)
                QTest::keyClicks(search, "harr");
        });
        QTimer::singleShot(60, &window, [&window, &pass]() {
            pass &= check(waitFor([&] { return paletteRow(window, "b2") != nullptr; }),
                          "the palette lists the object found through its alias");
            auto *dialog = window.findChild<QDialog *>("commandPalette");
            auto *list = dialog ? dialog->findChild<QListWidget *>() : nullptr;
            auto *row = paletteRow(window, "b2");
            pass &= check(row && row->text().contains("HP1"),
                          "the alias-matched row shows the object title");
            bool hintOk = false;
            if (row && list) {
                if (auto *hint = qobject_cast<QLabel *>(list->itemWidget(row)))
                    hintOk = hint->text() == "Harry Potter" && hint->accessibleName() == "Matched alias";
            }
            pass &= check(hintOk, "the row carries the matched-alias hint");
            if (list && row) {
                list->setCurrentRow(list->row(row));
                QTest::keyClick(dialog->findChild<QLineEdit *>(), Qt::Key_Return);
            } else if (dialog) {
                dialog->reject();
            }
        });
        window.commandPalette();
        pass &= check(fakeSaw(fake, "GET", "/v1/search?q=harr"),
                      "typing in the palette calls GET /v1/search");
        pass &= check(waitFor([&] { return window.currentRoute() == "object" && pageTitle(window) == "HP1"; }),
                      "picking the alias-matched result opens the object");
    }
    {
        QTimer::singleShot(0, &window, [&window]() {
            auto *dialog = window.findChild<QDialog *>("commandPalette");
            if (auto *search = dialog ? dialog->findChild<QLineEdit *>() : nullptr)
                QTest::keyClicks(search, "hp1");
        });
        QTimer::singleShot(60, &window, [&window, &pass]() {
            auto *dialog = window.findChild<QDialog *>("commandPalette");
            auto *list = dialog ? dialog->findChild<QListWidget *>() : nullptr;
            auto *row = paletteRow(window, "b2");
            pass &= check(waitFor([&] { return row != nullptr; }),
                          "a title query still finds the object");
            pass &= check(row && list && list->itemWidget(row) == nullptr,
                          "a title hit carries no matched-alias hint");
            if (dialog)
                dialog->reject();
        });
        window.commandPalette();
    }
    {
        // Server authority: a title that only the server knows (the client
        // cache is stale) appears in the palette only through the search
        // response.
        auto object = fake.findObject("b2");
        const QString originalTitle = object.value("title").toString();
        object["title"] = "Zzserver marker";
        fake.replaceObject(object);
        bool sawServerRow = false;
        QTimer::singleShot(0, &window, [&window]() {
            auto *dialog = window.findChild<QDialog *>("commandPalette");
            if (auto *search = dialog ? dialog->findChild<QLineEdit *>() : nullptr)
                QTest::keyClicks(search, "Zzserver");
        });
        QTimer::singleShot(80, &window, [&window, &sawServerRow]() {
            sawServerRow = waitFor([&] { return paletteRow(window, "b2") != nullptr; });
            if (auto *dialog = window.findChild<QDialog *>("commandPalette"))
                dialog->reject();
        });
        window.commandPalette();
        pass &= check(sawServerRow, "the palette renders the server's search response");
        object["title"] = originalTitle;
        fake.replaceObject(object);
        window.rememberObject(fake.findObject("b2"));
    }

    // 8. Aliases ride the normal save path: editing the note autosaves the
    //    properties document with the aliases intact.
    {
        const int before = countRequests(fake, "PATCH", "/v1/objects/b2");
        if (auto *editor = visibleChild<RichEditor>(window, "editor")) {
            editor->setFocus();
            QTest::keyClicks(editor, "A line to trigger the autosave.");
        }
        pass &= check(waitFor([&] { return countRequests(fake, "PATCH", "/v1/objects/b2") > before; }),
                      "editing the note autosaves the object");
        const auto *request = lastRequest(fake, "PATCH", "/v1/objects/b2");
        QStringList saved;
        if (request)
            for (auto v : request->body.value("properties").toObject().value("aliases").toArray())
                saved << v.toString();
        pass &= check(saved == QStringList{"Harry Potter", "The Stone"},
                      "the autosave carries the aliases inside the properties document");
    }

    // 9. THE signature behavior: the @-linking dropdown finds the object
    //    through an alias, shows the hint, and picking it inserts THE
    //    ALIAS as the chip display text while the link keeps the object id.
    {
        RichEditor editor;
        editor.resize(740, 500);
        editor.show();
        editor.setFocus();
        const QJsonArray editorTypes{QJsonObject{{"id", "book"}, {"name", "Libro"}},
                                      QJsonObject{{"id", "person"}, {"name", "Persona"}}};
        const QJsonObject hp{{"id", "b2"}, {"type_id", "book"}, {"title", "HP1"},
                              {"properties", QJsonObject{{"aliases", QJsonArray{"Harry Potter", "The Stone"}}}}};
        const QJsonObject ada{{"id", "p1"}, {"type_id", "person"}, {"title", "Ada"},
                               {"properties", QJsonObject{}}};
        editor.setReferenceData(QJsonArray{hp, ada}, editorTypes);

        QTest::keyClicks(&editor, "@harr");
        auto *results = editor.findChild<QListWidget *>("referenceResults");
        pass &= check(results && results->isVisible() && results->currentItem() &&
                          results->currentItem()->text() == "HP1",
                      "the @-dropdown lists the object found through its alias");
        bool hintOk = false;
        if (results && results->currentItem())
            if (auto *hint = qobject_cast<QLabel *>(results->itemWidget(results->currentItem())))
                hintOk = hint->text() == "Harry Potter";
        pass &= check(hintOk, "the @-dropdown row shows the matched-alias hint");
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::qWait(40);
        pass &= check(editor.toPlainText().contains("Harry Potter"),
                      "picking an alias-matched result inserts THE ALIAS as the chip text");
        pass &= check(editor.documentReferenceIds() == QJsonArray{"b2"},
                      "the inserted chip still references the object id");
        QString anchor;
        for (auto block = editor.document()->begin(); block.isValid(); block = block.next())
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto format = it.fragment().charFormat();
                if (format.isAnchor())
                    anchor = format.anchorHref();
            }
        pass &= check(anchor == "lumen://object/b2", "the chip's anchor keeps the object id");

        // Title match regression: the title is inserted, no hint.
        editor.clear();
        QTest::keyClicks(&editor, "@hp1");
        results = editor.findChild<QListWidget *>("referenceResults");
        if (results && results->currentItem())
            pass &= check(results->itemWidget(results->currentItem()) == nullptr,
                          "a title hit in the dropdown carries no alias hint");
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::qWait(40);
        pass &= check(editor.toPlainText().contains("HP1") && editor.documentReferenceIds() == QJsonArray{"b2"},
                      "picking a title-matched result inserts the title (no regression)");

        // The scoped branch (@<type>/<query>) applies the same rule. The
        // scoped list places its "Crear…" row first, so locate the object's
        // row explicitly instead of relying on the current row.
        editor.clear();
        QTest::keyClicks(&editor, "@book/harr");
        results = editor.findChild<QListWidget *>("referenceResults");
        QListWidgetItem *scopedRow = nullptr;
        if (results)
            for (int i = 0; i < results->count(); ++i)
                if (results->item(i)->text() == "HP1")
                    scopedRow = results->item(i);
        pass &= check(scopedRow != nullptr,
                      "the scoped @<type>/ dropdown finds the object through its alias");
        bool scopedHint = false;
        if (results && scopedRow)
            if (auto *hint = qobject_cast<QLabel *>(results->itemWidget(scopedRow)))
                scopedHint = hint->text() == "Harry Potter";
        pass &= check(scopedHint, "the scoped row carries the matched-alias hint");
        if (results && scopedRow)
            results->setCurrentRow(results->row(scopedRow));
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::qWait(40);
        pass &= check(editor.toPlainText().contains("Harry Potter"),
                      "the scoped pick also inserts the alias as the link text");

        // The block-reference path (embed) uses the alias text too.
        editor.clear();
        editor.showReferencePicker(true);
        QTest::keyClicks(&editor, "harr");
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::qWait(40);
        bool embedWithAlias = false;
        for (const auto &block : editor.structuredBlocks())
            if (block.toObject().value("type").toString() == "embed" &&
                editor.toPlainText().contains("Harry Potter"))
                embedWithAlias = true;
        pass &= check(embedWithAlias, "the block reference embeds the alias as the display text");

        // An object without aliases still inserts by title.
        editor.clear();
        QTest::keyClicks(&editor, "@ada");
        QTest::keyClick(&editor, Qt::Key_Return);
        QTest::qWait(40);
        pass &= check(editor.toPlainText().contains("Ada") && editor.documentReferenceIds() == QJsonArray{"p1"},
                      "objects without aliases keep the title insertion");
    }

    // 10. The type-page search (the shared view machinery, the client's
    //     GET /v1/objects?q= equivalent) surfaces alias matches.
    window.showCollection("book");
    pass &= check(waitFor([&] { return activeViews(window) != nullptr; }), "the book type page opens");
    {
        auto *search = activeViews(window)->findChild<QLineEdit *>("viewSearch");
        pass &= check(search != nullptr, "the view search field exists");
        if (search) {
            search->setText("harr");
            pass &= check(waitFor([&] {
                              return activeViews(window) &&
                                     activeViews(window)->visibleIds().contains("b2");
                          }),
                          "the type-page search surfaces the alias match");
            search->setText("stone");
            pass &= check(waitFor([&] {
                              return activeViews(window) &&
                                     activeViews(window)->visibleIds() == QStringList{"b2"};
                          }),
                          "the second alias matches too");
            search->setText("ada");
            pass &= check(waitFor([&] {
                              return activeViews(window) && activeViews(window)->visibleIds().isEmpty();
                          }),
                          "a non-matching query still filters");
        }
    }

    // 11. The header chips row (visual fix): wide windows keep the classic
    //     single row with the trailing group right-aligned; narrow windows
    //     wrap instead of clipping, and a chip alone wider than the row
    //     elides its text.
    window.openObject("b2");
    pass &= check(waitFor([&] { return window.currentRoute() == "object"; }), "the object page reopens");
    QTest::qWait(60);
    {
        auto *bar = visibleChild<WrapBar>(window, "headerChipsBar");
        pass &= check(bar != nullptr, "the page header uses the wrapping chips bar");
        pass &= check(bar && bar->height() <= 52, "a wide window keeps the chips on one row");
        QToolButton *more = nullptr;
        if (bar)
            for (auto *tool : bar->findChildren<QToolButton *>())
                if (tool->toolTip() == "Opciones del objeto")
                    more = tool;
        pass &= check(more && more->geometry().right() >= bar->width() - 60,
                      "the trailing group stays right-aligned when the row fits");
        QPushButton *collections = nullptr;
        if (bar)
            for (auto *push : bar->findChildren<QPushButton *>())
                if (push->text() == "Colecciones")
                    collections = push;
        pass &= check(collections && collections->x() < more->x(),
                      "the context chips stay on the left of the trailing actions");
    }
    window.resize(560, 780);
    QTest::qWait(90);
    {
        auto *bar = visibleChild<WrapBar>(window, "headerChipsBar");
        bool allInside = bar != nullptr;
        int chipCount = 0;
        if (bar)
            for (auto *child : bar->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
                if (!child->isVisible() || child->width() <= 0)
                    continue;
                ++chipCount;
                allInside &= child->geometry().right() <= bar->width();
                allInside &= child->geometry().bottom() <= bar->height();
            }
        pass &= check(allInside && chipCount >= 5, "no chip is clipped at the narrow width");
        pass &= check(bar && bar->height() > 52,
                      "the chips row wraps onto extra lines instead of overflowing");
    }
    window.resize(1448, 900);
    QTest::qWait(60);
    {
        // A single chip wider than the whole row elides (never clips), and
        // the full text returns when space allows.
        WrapBar bar;
        bar.setFixedWidth(170);
        bar.show();
        const QString longText = "A chip label far too long for this narrow row";
        auto *chip = button(longText, "photo", "quiet");
        bar.addChip(chip);
        QTest::qWait(50);
        pass &= check(chip->width() <= 170 && chip->text().contains("…") &&
                          chip->text() != longText,
                      "a chip alone wider than the row elides its text");
        pass &= check(chip->toolTip().contains(longText), "the elided chip keeps the full text as tooltip");
        bar.setFixedWidth(900);
        QTest::qWait(50);
        pass &= check(chip->text() == longText, "the full text returns when space allows");
    }
    {
        // The preview dialog header uses the same container.
        bool previewOk = false;
        {
            window.openPreview("b2");
            // openPreview is non-modal (d->show()); settle the layout and
            // inspect the preview header directly.
            QTest::qWait(120);
            auto *bar = window.findChild<WrapBar *>("previewChipsBar");
            bool inside = bar != nullptr;
            if (bar)
                for (auto *child : bar->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
                    if (!child->isVisible() || child->width() <= 0)
                        continue;
                    inside &= child->geometry().right() <= bar->width();
                    inside &= child->geometry().bottom() <= bar->height();
                }
            previewOk = inside;
            for (auto *dialog : window.findChildren<GuardedDialog *>())
                if (dialog->isVisible())
                    dialog->reject();
        }
        pass &= check(previewOk, "the preview header chips never clip either");
    }

    if (pass)
        fprintf(stdout,
                "LUMEN_ALIASES_OK: alias editor (add/remove/limits with non-blocking 400s), "
                "object-page display, palette over GET /v1/search with matched_alias hint, "
                "alias inserted as the link text with the id intact, title regression, "
                "type-page search, aliases riding the save path, header chips wrap/elide\n");
    QDir(configHome).removeRecursively();
    return pass ? 0 : 1;
}
