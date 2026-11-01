// Templates (desktop stage "templates-ui", D5) against the in-process fake
// API (tests/fake_api.hpp), extended with the templates slice of the
// contract (docs/implementation/backend/templates.md + contracts/SYNC.md
// "Templates"): save/unsave the is_template flag with the page cue, the
// create-dialog template picker, POST /v1/objects/from-template with the
// resolved title, the type default template in the type settings dialog, the
// journal apply_default_template parameter (with and without a default, and
// no re-apply on an existing note), the Templates dashboard section, and
// blank creation staying unchanged for template-less types. No server, no
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
static QToolButton *sectionTab(MainWindow &window, const QString &key) {
    for (auto *tab : window.findChildren<QToolButton *>())
        if (tab->isVisible() && tab->property("sectionKey").toString() == key)
            return tab;
    return nullptr;
}
static QString sectionTabText(MainWindow &window, const QString &key) {
    auto *tab = sectionTab(window, key);
    return tab ? tab->text() : QString();
}
static bool clickSection(MainWindow &window, const QString &key) {
    if (!waitFor([&] { return sectionTab(window, key) != nullptr; }))
        return false;
    QTest::mouseClick(sectionTab(window, key), Qt::LeftButton);
    return true;
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
static QString emptyTitle(MainWindow &window) {
    auto *title = visibleChild<QLabel>(window, "viewEmpty");
    return title ? title->text() : QString();
}
static QString pageTitle(MainWindow &window) {
    auto *heading = visibleChild<QTextEdit>(window, "documentTitle");
    return heading ? heading->toPlainText() : QString();
}
static QAction *menuAction(QMenu *menu, const QString &text) {
    for (auto *action : menu->actions())
        if (action->text() == text)
            return action;
    return nullptr;
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    // The dashboard persists per-type section visibility in QSettings; point
    // XDG_CONFIG_HOME at a throwaway directory so the test neither reads nor
    // pollutes the developer's own configuration.
    const QString configHome =
        QDir::temp().filePath(QString("lumen-templates-test-%1").arg(QCoreApplication::applicationPid()));
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
    auto seed = [](const QString &id, const QString &type, const QString &title, int day,
                   const QJsonObject &properties) {
        const QString stamp = QString("2026-10-%1T09:00:00.000Z").arg(day, 2, 10, QChar('0'));
        return QJsonObject{{"id", id}, {"type_id", type}, {"title", title},
                            {"content", properties.value("blocks").toArray().size() ? "Template body" : ""},
                            {"links", QJsonArray{}}, {"property_links", QJsonArray{}},
                            {"properties", properties}, {"revision", 1},
                            {"created_at", stamp}, {"updated_at", stamp}, {"sync_status", "synced"}};
    };
    // tpl-book and tpl-journal carry the reserved flag; the fake excludes
    // them from every listing unless include_templates=true, exactly like the
    // server, so the client never sees them in the workspace cache.
    fake.objects = QJsonArray{
        seed("b1", "book", "Meeting notes", 2,
             QJsonObject{{"author", "Grace"}, {"blocks", QJsonArray{
                 QJsonObject{{"id", "b1-block"}, {"type", "paragraph"}, {"text", "The weekly sync."}}}}}),
        seed("b2", "book", "Second book", 3, QJsonObject{}),
        seed("tpl-book", "book", "Book template", 1,
             QJsonObject{{"is_template", true}, {"tags", QJsonArray{"template"}},
                         {"blocks", QJsonArray{
                             QJsonObject{{"id", "tb1"}, {"type", "heading"}, {"level", 2}, {"text", "Template heading"}},
                             QJsonObject{{"id", "tb2"}, {"type", "paragraph"}, {"text", "Template body"}}}}}),
        seed("p1", "person", "Ada", 4, QJsonObject{})};
    {
        auto journalTemplate = seed("tpl-journal", "journal", "Daily {date:iso}", 1,
                                    QJsonObject{{"is_template", true}, {"blocks", QJsonArray{
                                        QJsonObject{{"id", "jtb1"}, {"type", "paragraph"}, {"text", "Journal template body"}}}}});
        journalTemplate["content"] = "Journal template body";
        fake.objects.append(journalTemplate);
    }
    if (!fake.start()) {
        fprintf(stderr, "FAIL: fake API could not listen on loopback\n");
        return 1;
    }
    qputenv("LUMEN_API_URL", fake.baseUrl().toString().toUtf8());
    qputenv("LUMEN_TOKEN", "templates-test");

    MainWindow window;
    window.resize(1448, 900);
    window.show();
    pass &= check(waitFor([&] { return window.ready; }), "startup reached ready state against the fake API");

    // 1. The workspace cache never contains templates (server listing
    //    excludes them; the client does not fight it).
    pass &= check(window.objectFor("tpl-book").isEmpty(),
                  "flagged objects are excluded from the workspace cache");

    // 2. The book type page gains the "Templates" dashboard section once the
    //    flagged listing lands; "All" never shows the template.
    window.showCollection("book");
    pass &= check(waitFor([&] { return sectionTab(window, "templates") != nullptr; }),
                  "Templates tab appears for a type with templates");
    pass &= check(waitFor([&] { return sectionTabText(window, "templates") == "Templates  ·  1"; }),
                  "Templates tab count badge");
    pass &= check(fakeSaw(fake, "GET", "/v1/objects?type_id=book&include_templates=true"),
                  "the flagged listing is requested with include_templates=true");
    pass &= check(waitFor([&] {
                      return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"b2", "b1"};
                  }),
                  "All tab lists normal objects only, newest first");
    pass &= clickSection(window, "templates");
    pass &= check(waitFor([&] {
                      return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"tpl-book"};
                  }),
                  "Templates tab lists flagged objects only");
    pass &= check(waitFor([&] {
                      for (auto *card : window.findChildren<QAbstractButton *>("lumenObjectCard"))
                          if (card->isVisible() && card->property("objectId").toString() == "tpl-book")
                              return true;
                      return false;
                  }),
                  "the template renders as a card in the view machinery");

    // 3. The tab's row menu offers the template actions; "New from this
    //    template" creates through the endpoint and opens the result.
    {
        QMenu *menu = window.buildTemplateMenu("tpl-book");
        pass &= check(menu && menuAction(menu, "New from this template") &&
                          menuAction(menu, "Unsave template") && menuAction(menu, "Open"),
                      "template row menu offers Open, New from this template and Unsave template");
        if (auto *action = menu ? menuAction(menu, "New from this template") : nullptr)
            action->trigger();
        if (menu)
            menu->deleteLater();
    }
    pass &= check(waitFor([&] { return fakeSaw(fake, "POST", "/v1/objects/from-template"); }),
                  "New from this template calls POST /v1/objects/from-template");
    {
        const auto &request = *lastRequest(fake, "POST", "/v1/objects/from-template");
        pass &= check(request.body.value("template_id").toString() == "tpl-book",
                      "from-template request carries the template id");
        pass &= check(!request.body.contains("title"),
                      "from-template request omits the title so the template's resolves");
    }
    pass &= check(waitFor([&] { return window.currentRoute() == "object" && pageTitle(window) == "Book template"; }),
                  "the from-template result opens with the resolved title");
    {
        const auto created = fake.objects.last().toObject();
        pass &= check(str(created, "type_id") == "book", "the created object has the template's type");
        pass &= check(!created.value("properties").toObject().contains("is_template"),
                      "the created object does not inherit the is_template flag");
        QStringList blockIds;
        for (auto v : created.value("properties").toObject().value("blocks").toArray())
            blockIds << v.toObject().value("id").toString();
        pass &= check(!blockIds.contains("tb1") && !blockIds.contains("tb2") && blockIds.size() == 2,
                      "copied blocks receive fresh ids");
    }

    // 4. Save/unsave template round-trip: the row menu flags the object, the
    //    page gains the Template cue, the object leaves the normal cache, and
    //    unflagging restores everything.
    window.openObject("b1");
    pass &= check(waitFor([&] { return window.currentRoute() == "object" && pageTitle(window) == "Meeting notes"; }),
                  "the object page opens");
    pass &= check(visibleChild<QPushButton>(window, "templateChip") == nullptr,
                  "a normal object has no template cue");
    {
        QMenu *menu = window.buildObjectMenu("b1", QString());
        pass &= check(menu && menuAction(menu, "Save as template") != nullptr,
                      "the object row menu offers Save as template");
        if (auto *action = menu ? menuAction(menu, "Save as template") : nullptr)
            action->trigger();
        if (menu)
            menu->deleteLater();
    }
    pass &= check(waitFor([&] { return lastRequest(fake, "PATCH", "/v1/objects/b1") != nullptr; }),
                  "Save as template PATCHes the object");
    {
        const auto &request = *lastRequest(fake, "PATCH", "/v1/objects/b1");
        const auto properties = request.body.value("properties").toObject();
        pass &= check(properties.value("is_template").toBool(),
                      "the PATCH sets properties.is_template=true");
        pass &= check(properties.value("author").toString() == "Grace",
                      "the PATCH sends the whole editable property map");
    }
    pass &= check(waitFor([&] { return visibleChild<QPushButton>(window, "templateChip") != nullptr; }),
                  "the flagged object page shows the Template chip");
    pass &= check(waitFor([&] { return window.objectFor("b1").isEmpty(); }),
                  "a saved template leaves the general listing cache");
    pass &= check(waitFor([&] {
                      for (const auto &v : window.templatesForType("book"))
                          if (str(v.toObject(), "id") == "b1")
                              return true;
                      return false;
                  }),
                  "a saved template enters the flagged mirror");
    window.setObjectTemplateFlag("b1", false);
    pass &= check(waitFor([&] {
                      const auto *request = lastRequest(fake, "PATCH", "/v1/objects/b1");
                      return request && !request->body.value("properties").toObject().contains("is_template");
                  }),
                  "Unsave template PATCHes the map without the flag (absent means false)");
    pass &= check(waitFor([&] { return visibleChild<QPushButton>(window, "templateChip") == nullptr; }),
                  "the Template chip disappears after unflagging");
    pass &= check(waitFor([&] { return !window.objectFor("b1").isEmpty(); }),
                  "an unsaved template rejoins the general cache");

    // 5. The create-dialog picker: templates of the selected type next to
    //    "Blank"; picking one and leaving the title empty lets the server
    //    resolve the template's title.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("newObjectDialog");
        auto *combo = dialog ? dialog->findChild<QComboBox *>("newObjectTemplate") : nullptr;
        const bool populated = combo && waitFor([&] { return combo->findData("tpl-book") >= 0; });
        if (populated)
            combo->setCurrentIndex(combo->findData("tpl-book"));
        if (auto *create = dialog ? dialog->findChild<QPushButton *>("newObjectCreate") : nullptr)
            create->click();
        else if (dialog)
            dialog->reject();
    });
    window.newObject("book");
    pass &= check(waitFor([&] { return countRequests(fake, "POST", "/v1/objects/from-template") == 2; }),
                  "picking a template in the create dialog calls from-template");
    pass &= check(waitFor([&] { return window.currentRoute() == "object" && pageTitle(window) == "Book template"; }),
                  "the created-from-template object opens with the resolved title");

    // 6. Blank creation is unchanged for a type without templates: the picker
    //    row hides and the normal POST /v1/objects runs.
    const int fromTemplateCalls = countRequests(fake, "POST", "/v1/objects/from-template");
    bool pickerHidden = true;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("newObjectDialog");
        auto *row = dialog ? dialog->findChild<QWidget *>("newObjectTemplateRow") : nullptr;
        pickerHidden = !row || !row->isVisible();
        if (auto *name = dialog ? dialog->findChild<QLineEdit *>("newObjectTitle") : nullptr)
            name->setText("Ada Lovelace");
        if (auto *create = dialog ? dialog->findChild<QPushButton *>("newObjectCreate") : nullptr)
            create->click();
        else if (dialog)
            dialog->reject();
    });
    window.newObject("person");
    pass &= check(waitFor([&] { return window.currentRoute() == "object" && pageTitle(window) == "Ada Lovelace"; }),
                  "blank creation still creates and opens the object");
    pass &= check(pickerHidden, "types without templates keep the create flow unchanged (no picker row)");
    pass &= check(countRequests(fake, "POST", "/v1/objects/from-template") == fromTemplateCalls,
                  "blank creation does not call from-template");
    {
        const auto &request = *lastRequest(fake, "POST", "/v1/objects");
        pass &= check(request.body.value("type_id").toString() == "person" &&
                          request.body.value("title").toString() == "Ada Lovelace",
                      "blank creation posts the normal object payload");
    }

    // 7. The type settings dialog picks the default template
    //    (PATCH /v1/types/{id} default_template_id).
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("typeSettingsDialog");
        auto *combo = dialog ? dialog->findChild<QComboBox *>("typeDefaultTemplate") : nullptr;
        const bool populated = combo && waitFor([&] { return combo->findData("tpl-journal") >= 0; });
        if (populated)
            combo->setCurrentIndex(combo->findData("tpl-journal"));
        if (auto *box = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr) {
            if (auto *save = box->button(QDialogButtonBox::Save))
                save->click();
        } else if (dialog)
            dialog->reject();
    });
    window.propertyDialog("journal");
    pass &= check(waitFor([&] { return lastRequest(fake, "PATCH", "/v1/types/journal") != nullptr; }),
                  "saving the type settings PATCHes the type");
    pass &= check(lastRequest(fake, "PATCH", "/v1/types/journal")->body.value("default_template_id").toString() ==
                      "tpl-journal",
                  "the PATCH sets default_template_id");
    pass &= check(waitFor([&] { return window.journalTypeHasDefaultTemplate(); }),
                  "the client sees the journal default template");

    // 8. Opening a day whose note does not exist yet creates it from the
    //    default template (the parameter only affects lazy creation).
    window.navigateJournal(QDate(2026, 10, 25));
    pass &= check(waitFor([&] { return fakeSaw(fake, "GET", "/v1/journal/2026-10-25?apply_default_template=true"); }),
                  "lazy journal creation asks for the default template");
    pass &= check(waitFor([&] { return window.currentRoute() == "journal"; }),
                  "the daily note opens");
    {
        const auto note = fake.findObject("journal-2026-10-25");
        pass &= check(!note.isEmpty() && str(note, "title") == "2026-10-25",
                      "the note keeps its canonical date title");
        pass &= check(note.value("content").toString() == "Journal template body",
                      "the note body comes from the default template");
        const auto properties = note.value("properties").toObject();
        pass &= check(properties.value("date").toString() == "2026-10-25" &&
                          !properties.contains("is_template"),
                      "the note keeps properties.date and drops the flag");
        QStringList blockIds;
        for (auto v : properties.value("blocks").toArray())
            blockIds << v.toObject().value("id").toString();
        pass &= check(blockIds.size() == 1 && !blockIds.contains("jtb1"),
                      "the template blocks are copied with fresh ids");
    }
    pass &= check(waitFor([&] {
                      auto *editor = visibleChild<QTextEdit>(window, "editor");
                      return editor && editor->toPlainText().contains("Journal template body");
                  }),
                  "the editor renders the template body");

    // 9. An existing note is never re-applied: navigating again does not send
    //    the parameter a second time.
    const int applied = countRequests(fake, "GET", "apply_default_template");
    window.navigateJournal(QDate(2026, 10, 25));
    pass &= check(waitFor([&] { return window.currentRoute() == "journal"; }), "the same day opens again");
    pass &= check(countRequests(fake, "GET", "apply_default_template") == applied,
                  "an existing note is not re-applied (no extra parameter)");

    // 10. Clearing the default: new days are plain notes again.
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("typeSettingsDialog");
        auto *combo = dialog ? dialog->findChild<QComboBox *>("typeDefaultTemplate") : nullptr;
        if (combo)
            combo->setCurrentIndex(0);  // "None"
        if (auto *box = dialog ? dialog->findChild<QDialogButtonBox *>() : nullptr) {
            if (auto *save = box->button(QDialogButtonBox::Save))
                save->click();
        } else if (dialog)
            dialog->reject();
    });
    window.propertyDialog("journal");
    pass &= check(waitFor([&] {
                      const auto *request = lastRequest(fake, "PATCH", "/v1/types/journal");
                      return request && request->body.value("default_template_id").toString().isEmpty();
                  }),
                  "clearing the picker PATCHes an empty default_template_id");
    pass &= check(waitFor([&] { return !window.journalTypeHasDefaultTemplate(); }),
                  "the journal default is cleared client-side");
    window.navigateJournal(QDate(2026, 11, 5));
    pass &= check(waitFor([&] { return fakeSaw(fake, "GET", "/v1/journal/2026-11-05"); }),
                  "the next day is requested");
    {
        const auto *request = lastRequest(fake, "GET", "/v1/journal/2026-11-05");
        pass &= check(request && request->path == "/v1/journal/2026-11-05",
                      "without a default the journal is requested without the parameter");
    }
    pass &= check(countRequests(fake, "GET", "apply_default_template") == applied,
                  "the apply parameter is only sent for lazy creation with a default");
    pass &= check(waitFor([&] { return !fake.findObject("journal-2026-11-05").isEmpty(); }),
                  "the plain note is created");
    {
        const auto note = fake.findObject("journal-2026-11-05");
        pass &= check(note.value("content").toString().isEmpty(),
                      "the note is created plain");
    }

    // 11. Template-less types show no tab by default; the "+" model can add
    //     it on demand and removing the active section falls back to All.
    window.showCollection("person");
    pass &= check(waitFor([&] { return sectionTab(window, "all") != nullptr; }),
                  "the person type page renders the dashboard");
    pass &= check(waitFor([&] { return sectionTab(window, "templates") == nullptr; }),
                  "template-less types show no Templates tab by default");
    window.toggleTypeSection("person", "templates", true);
    pass &= check(waitFor([&] { return sectionTab(window, "templates") != nullptr; }),
                  "the + model adds the Templates tab on demand");
    pass &= clickSection(window, "templates");
    pass &= check(waitFor([&] { return emptyTitle(window) == "No templates yet"; }),
                  "the on-demand tab shows its scoped empty state");
    window.toggleTypeSection("person", "templates", false);
    pass &= check(waitFor([&] { return sectionTab(window, "templates") == nullptr; }),
                  "removing the section hides the tab");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds().size() == 2; }),
                  "removing the active section falls back to All");

    if (pass)
        fprintf(stdout,
                "LUMEN_TEMPLATES_OK: save/unsave round-trip with cue, picker + from-template with resolved "
                "title and fresh block ids, type default set/clear, journal apply_default_template (on lazy "
                "creation only), Templates tab with flagged-only listing, blank creation unchanged\n");
    QDir(configHome).removeRecursively();
    return pass ? 0 : 1;
}
