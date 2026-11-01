// Type dashboards (desktop stage "type-dashboards", D4) against the in-process
// fake API (tests/fake_api.hpp): section tabs on the type page (All, Recent,
// Untagged, No backlinks, Collections, saved queries), per-tab id sets and
// counts, the server-side no-backlinks filter, QSettings persistence of
// recents and tab visibility, scoped empty states, and the D2 chip row
// coexisting with the tab bar. No server, no network beyond loopback inside
// the test process, entirely offscreen.
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
static bool fakeSaw(const FakeApi &fake, const QString &method, const QString &pathPart) {
    for (const auto &request : fake.log)
        if (request.method == method && request.path.contains(pathPart))
            return true;
    return false;
}
static QString emptyTitle(MainWindow &window) {
    auto *title = visibleChild<QLabel>(window, "viewEmpty");
    return title ? title->text() : QString();
}
// Waits for the section tab to be visible before clicking it.
static bool clickSection(MainWindow &window, const QString &key) {
    if (!waitFor([&] { return sectionTab(window, key) != nullptr; }))
        return false;
    QTest::mouseClick(sectionTab(window, key), Qt::LeftButton);
    return true;
}
static QString bookId(int number) {
    return QString("b%1").arg(number, 2, 10, QChar('0'));
}
// Books b01..b22 with ascending updated_at: the default listing order is
// newest first, so "All" is b22..b01. b01 is the only tagged book and links
// to the web object (so the web type has a backlinked object); b04 is the
// only backlinked book (the collection contains it).
static QStringList allBooks() {
    QStringList ids;
    for (int number = 22; number >= 1; --number) ids << bookId(number);
    return ids;
}
static QStringList untaggedBooks() {
    QStringList ids;
    for (int number = 22; number >= 1; --number)
        if (number != 1) ids << bookId(number);
    return ids;
}
static QStringList noBacklinkBooks() {
    QStringList ids;
    for (int number = 22; number >= 1; --number)
        if (number != 4) ids << bookId(number);
    return ids;
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    // The dashboard persists recents and per-type tab visibility in
    // QSettings; point XDG_CONFIG_HOME at a throwaway directory so the test
    // neither reads nor pollutes the developer's own configuration.
    const QString configHome =
        QDir::temp().filePath(QString("lumen-dashboards-test-%1").arg(QCoreApplication::applicationPid()));
    QDir().mkpath(configHome);
    qputenv("XDG_CONFIG_HOME", configHome.toUtf8());
    bool pass = true;

    FakeApi fake;
    fake.types = QJsonArray{
        QJsonObject{{"id", "book"}, {"name", "Book"}, {"icon", "book"}, {"color", "#d6ba51"},
                    {"properties", QJsonArray{
                        QJsonObject{{"key", "tags"}, {"type", "tags"}},
                        QJsonObject{{"key", "author"}, {"type", "text"}}}}},
        QJsonObject{{"id", "web"}, {"name", "Web link"}, {"icon", "world"}, {"color", "#6c9edf"},
                    {"properties", QJsonArray{QJsonObject{{"key", "url"}, {"type", "url"}}}}},
        QJsonObject{{"id", "person"}, {"name", "Person"}, {"icon", "user"}, {"color", "#e0a458"},
                    {"properties", QJsonArray{QJsonObject{{"key", "tags"}, {"type", "tags"}}}}},
        QJsonObject{{"id", "collection"}, {"name", "Collection"}, {"icon", "view-list-details"}, {"color", "#8f7fb5"},
                    {"properties", QJsonArray{
                        QJsonObject{{"key", "item_type_id"}, {"type", "text"}},
                        QJsonObject{{"key", "items"}, {"type", "object"}, {"multiple", true}}}}},
        QJsonObject{{"id", "query"}, {"name", "Query"}, {"icon", "filter"}, {"color", "#7f8fb5"},
                    {"properties", QJsonArray{}}}};
    auto seed = [](const QString &id, const QString &type, const QString &title, int day,
                   const QJsonObject &properties, const QJsonArray &links = {}) {
        const QString stamp = QString("2026-10-%1T09:00:00.000Z").arg(day, 2, 10, QChar('0'));
        return QJsonObject{{"id", id}, {"type_id", type}, {"title", title}, {"content", ""},
                            {"links", links}, {"property_links", QJsonArray{}},
                            {"properties", properties}, {"revision", 1},
                            {"created_at", stamp}, {"updated_at", stamp}, {"sync_status", "synced"}};
    };
    fake.objects = QJsonArray{
        seed(bookId(1), "book", "Book 1", 1, QJsonObject{{"tags", QJsonArray{"tagged"}}}, QJsonArray{"w1"}),
    };
    for (int number = 2; number <= 22; ++number)
        fake.objects.append(seed(bookId(number), "book", QString("Book %1").arg(number), number,
                                 QJsonObject{{"tags", QJsonArray{}}}));
    fake.objects.append(seed("w1", "web", "Docs site", 23, QJsonObject{{"url", "https://example.com"}}));
    fake.objects.append(seed("p1", "person", "Ada", 24, QJsonObject{{"tags", QJsonArray{"team"}}}));
    fake.objects.append(seed("p2", "person", "Grace", 25, QJsonObject{{"tags", QJsonArray{"team"}}}));
    fake.objects.append(seed("col1", "collection", "Reading list", 26,
                             QJsonObject{{"item_type_id", "book"}, {"items", QJsonArray{bookId(4)}}}));
    fake.objects.append(seed("q1", "query", "Tagged books", 27,
                             QJsonObject{{"view", QJsonObject{
                                 {"type_id", "book"},
                                 {"filter", QJsonObject{{"mode", "and"}, {"rules", QJsonArray{
                                     QJsonObject{{"field", "properties.tags"}, {"op", "equals"}, {"value", "tagged"}}}}}}}}}));
    if (!fake.start()) {
        fprintf(stderr, "FAIL: fake API could not listen on loopback\n");
        return 1;
    }
    qputenv("LUMEN_API_URL", fake.baseUrl().toString().toUtf8());
    qputenv("LUMEN_TOKEN", "dashboards-test");

    MainWindow window;
    window.resize(1448, 900);
    window.show();
    pass &= check(waitFor([&] { return window.ready; }), "startup reached ready state against the fake API");

    // 1. The type page renders the section tab bar next to the D2 chip row,
    //    with the built-in tabs and one tab per saved query of the type.
    window.showCollection("book");
    pass &= check(waitFor([&] { return visibleChild<QWidget>(window, "typeDashboardTabs") != nullptr; }),
                  "type page renders the dashboard tab bar");
    pass &= check(waitFor([&] { return visibleChild<QWidget>(window, "typeCollectionsBar") != nullptr; }),
                  "D2 collection chip row stays on the type page");
    pass &= check(sectionTab(window, "all") && sectionTab(window, "recent") &&
                      sectionTab(window, "untagged") && sectionTab(window, "no_backlinks") &&
                      sectionTab(window, "collections"),
                  "built-in dashboard tabs render for the type");
    pass &= check(waitFor([&] { return sectionTab(window, "query:q1") != nullptr; }),
                  "saved queries of the type render as dashboard tabs");
    pass &= check(sectionTabText(window, "query:q1").startsWith("Tagged books"),
                  "query tab carries the saved query title");

    // 2. "All" is unchanged: the whole type in the default listing order and
    //    the previous overview/all internal toggle.
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == allBooks(); }),
                  "All tab shows every book, newest first");
    pass &= check(visibleChild<QToolButton>(window, "collectionOverview") != nullptr,
                  "All tab keeps the overview/all internal toggle");

    // 3. Counts match the fake data.
    pass &= check(sectionTabText(window, "all") == "All  ·  22", "All count badge");
    pass &= check(sectionTabText(window, "recent") == "Recent  ·  0", "Recent starts empty");
    pass &= check(sectionTabText(window, "untagged") == "Untagged  ·  21", "Untagged count badge");
    pass &= check(waitFor([&] { return sectionTabText(window, "no_backlinks") == "No backlinks  ·  21"; }),
                  "No backlinks count arrives from the filtered listing");
    pass &= check(sectionTabText(window, "collections") == "Collections  ·  1", "Collections count badge");
    pass &= check(sectionTabText(window, "query:q1") == "Tagged books  ·  1", "query tab count badge");

    // 4. "No backlinks" asks the server with the contract's filter and shows
    //    exactly the objects without backlinks.
    pass &= check(fakeSaw(fake, "GET", "/v1/objects?type_id=book&backlinked_count=0"),
                  "No backlinks uses the API filter type_id + backlinked_count=0");
    pass &= clickSection(window, "no_backlinks");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == noBacklinkBooks(); }),
                  "No backlinks tab lists only the objects no other object links to");

    // 5. "Untagged" excludes every tagged object (client-side id set).
    pass &= clickSection(window, "untagged");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == untaggedBooks(); }),
                  "Untagged tab lists only objects without tags");
    pass &= check(sectionTab(window, "untagged")->isChecked(), "active tab is checked");

    // 6. "Recent": scoped empty state, then order and the ~20 cap after
    //    opening every book (oldest to newest).
    pass &= clickSection(window, "recent");
    pass &= check(waitFor([&] { return emptyTitle(window) == "Nothing opened recently"; }),
                  "Recent tab shows its scoped empty state");
    for (int number = 1; number <= 22; ++number)
        window.rememberRecent(bookId(number));
    window.refreshTypeDashboard("book");
    QStringList expectedRecent;
    for (int number = 22; number >= 3; --number) expectedRecent << bookId(number);
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == expectedRecent; }),
                  "Recent tab shows recents newest first, capped at 20");
    pass &= check(waitFor([&] { return sectionTabText(window, "recent") == "Recent  ·  20"; }),
                  "Recent count badge reflects the cap");

    // 7. "Collections" lists the type's collections; a card opens the D2
    //    collection page.
    pass &= clickSection(window, "collections");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"col1"}; }),
                  "Collections tab lists the type's collections");
    QAbstractButton *collectionCard = nullptr;
    pass &= check(waitFor([&] {
                      for (auto *card : window.findChildren<QAbstractButton *>("lumenObjectCard"))
                          if (card->isVisible() && card->property("objectId").toString() == "col1") {
                              collectionCard = card;
                              return true;
                          }
                      return false;
                  }),
                  "collection renders as a card in the view machinery");
    if (collectionCard)
        QTest::mouseClick(collectionCard, Qt::LeftButton);
    pass &= check(waitFor([&] { return window.currentRoute() == "collection"; }),
                  "collection card opens the D2 collection page");
    window.showCollection("book");
    pass &= check(waitFor([&] { return visibleChild<QWidget>(window, "typeDashboardTabs") != nullptr; }),
                  "type page rebuilds after returning from the collection page");

    // 8. A saved query tab renders the query's own view spec.
    pass &= clickSection(window, "query:q1");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{bookId(1)}; }),
                  "query tab evaluates the saved query for this type");

    // 9. Back on "All" the type page behaves exactly as before the stage.
    pass &= clickSection(window, "all");
    pass &= check(waitFor([&] {
                      return activeViews(window) && activeViews(window)->visibleIds() == allBooks() &&
                             visibleChild<QToolButton>(window, "collectionOverview") != nullptr;
                  }),
                  "All tab restores the full type view with its internal toggle");

    // 10. Scoped empty states per section.
    window.showCollection("person");
    pass &= check(waitFor([&] { return sectionTab(window, "untagged") != nullptr; }),
                  "person type page renders the dashboard");
    pass &= clickSection(window, "untagged");
    pass &= check(waitFor([&] { return emptyTitle(window) == "Every Person has tags"; }),
                  "Untagged empty state names the type");
    window.showCollection("web");
    pass &= check(waitFor([&] { return sectionTab(window, "no_backlinks") != nullptr; }),
                  "web type page renders the dashboard");
    pass &= clickSection(window, "no_backlinks");
    pass &= check(waitFor([&] { return emptyTitle(window) == "Every Web link has backlinks"; }),
                  "No backlinks empty state names the type");
    pass &= check(waitFor([&] { return sectionTabText(window, "no_backlinks") == "No backlinks  ·  0"; }),
                  "No backlinks count is zero for the fully backlinked type");
    window.showCollection("person");
    pass &= clickSection(window, "collections");
    pass &= check(waitFor([&] { return emptyTitle(window) == "No collections yet"; }),
                  "Collections empty state");

    // 11. The "+" menu removes a built-in section per type, persists the
    //     choice in QSettings and falls back to All when removing the active
    //     section. (Capacities: "+" adds, "Remove" hides for this type.)
    window.showCollection("book");
    pass &= clickSection(window, "untagged");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == untaggedBooks(); }),
                  "untagged is active before removing it");
    QToolButton *addButton = nullptr;
    for (auto *button : window.findChildren<QToolButton *>("dashboardTabAdd"))
        if (button->isVisible())
            addButton = button;
    QAction *removeUntagged = nullptr;
    if (addButton)
        for (auto *action : addButton->menu()->actions())
            if (action->text() == "Untagged")
                removeUntagged = action;
    pass &= check(addButton && removeUntagged, "the + menu lists the built-in sections");
    if (removeUntagged)
        removeUntagged->trigger();
    pass &= check(waitFor([&] { return sectionTab(window, "untagged") == nullptr; }),
                  "removed section leaves the tab bar");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == allBooks(); }),
                  "removing the active section falls back to All");
    {
        QSettings pref("Lumen", "Lumen");
        pref.sync();
        pass &= check(pref.value("typeDashboards/book/hidden").toStringList().contains("untagged"),
                      "hidden section persists in QSettings");
    }
    window.showCollection("book");
    pass &= check(waitFor([&] { return visibleChild<QWidget>(window, "typeDashboardTabs") != nullptr; }),
                  "tab bar rebuilds without the hidden section");
    pass &= check(sectionTab(window, "untagged") == nullptr, "hidden section stays hidden after rebuild");

    // 12. Restart persistence: a second window reads the same settings —
    //     the hidden section and the recently-opened ids survive. The
    //     persisted snapshot is capped at 20: opening the collection page in
    //     step 7 put col1 at the head of the recents, so the newest 19 books
    //     survive and the Recent tab filters col1 out by type.
    MainWindow second;
    second.resize(1448, 900);
    second.show();
    pass &= check(waitFor([&] { return second.ready; }), "second window starts against the fake API");
    second.showCollection("book");
    pass &= check(waitFor([&] { return visibleChild<QWidget>(second, "typeDashboardTabs") != nullptr; }),
                  "second window renders the dashboard");
    pass &= check(sectionTab(second, "untagged") == nullptr,
                  "hidden section persists across restarts (QSettings)");
    pass &= check(sectionTabText(second, "recent") == "Recent  ·  19",
                  "recently opened ids persist across restarts (QSettings)");
    QStringList expectedRestartRecent;
    for (int number = 22; number >= 4; --number) expectedRestartRecent << bookId(number);
    pass &= clickSection(second, "recent");
    pass &= check(waitFor([&] {
                      return activeViews(second) && activeViews(second)->visibleIds() == expectedRestartRecent;
                  }),
                  "second window's Recent tab restores the persisted recents in order");
    // Restore the section for the remaining checks and verify "+" re-adds it.
    second.toggleTypeSection("book", "untagged", true);
    pass &= check(waitFor([&] { return sectionTab(second, "untagged") != nullptr; }),
                  "the + menu re-adds a removed section");

    if (pass)
        fprintf(stdout,
                "LUMEN_DASHBOARDS_OK: tab bar with All/Recent/Untagged/No backlinks/Collections + saved queries, "
                "counts, API-filtered no-backlinks, recent order and cap, empty states, + menu persistence, "
                "restart persistence, D2 chip row coexistence\n");
    QDir(configHome).removeRecursively();
    return pass ? 0 : 1;
}
