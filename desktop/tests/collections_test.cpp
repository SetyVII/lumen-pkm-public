// Collections UI flows (desktop stage "collections-ui") against an in-process
// fake API (tests/fake_api.hpp): no server, no network beyond loopback inside
// the test process, entirely offscreen.
#include "fake_api.hpp"
#include "../src/app/main_window.hpp"
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
static QPushButton *chipWithText(MainWindow &window, const QString &part) {
    for (auto *chip : window.findChildren<QPushButton *>("collectionChip"))
        if (chip->isVisible() && chip->text().contains(part))
            return chip;
    return nullptr;
}
static bool fakeSaw(const FakeApi &fake, const QString &method, const QString &pathPart,
                    const QString &bodyPart = {}) {
    for (const auto &request : fake.log)
        if (request.method == method && request.path.contains(pathPart) &&
            (bodyPart.isEmpty() ||
             QJsonDocument(request.body).toJson(QJsonDocument::Compact).contains(bodyPart.toUtf8())))
            return true;
    return false;
}

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    bool pass = true;

    FakeApi fake;
    fake.types = QJsonArray{
        QJsonObject{{"id", "book"}, {"name", "Book"}, {"icon", "book"}, {"color", "#d6ba51"},
                    {"properties", QJsonArray{
                        QJsonObject{{"key", "status"}, {"type", "select"}, {"options", QJsonArray{"To read", "Reading", "Read"}}},
                        QJsonObject{{"key", "author"}, {"type", "text"}}}}},
        QJsonObject{{"id", "web"}, {"name", "Web link"}, {"icon", "world"}, {"color", "#6c9edf"},
                    {"properties", QJsonArray{QJsonObject{{"key", "url"}, {"type", "url"}}}}},
        QJsonObject{{"id", "collection"}, {"name", "Collection"}, {"icon", "view-list-details"}, {"color", "#8f7fb5"},
                    {"properties", QJsonArray{
                        QJsonObject{{"key", "item_type_id"}, {"type", "text"}},
                        QJsonObject{{"key", "items"}, {"type", "object"}, {"multiple", true}}}}}};
    auto seedObject = [](const QString &id, const QString &type, const QString &title) {
        return QJsonObject{{"id", id}, {"type_id", type}, {"title", title}, {"content", ""},
                           {"links", QJsonArray{}}, {"properties", QJsonObject{}}, {"revision", 1},
                           {"created_at", "2026-10-01T10:00:00.000Z"}, {"updated_at", "2026-10-01T10:00:00.000Z"},
                           {"sync_status", "synced"}};
    };
    fake.objects = QJsonArray{seedObject("b1", "book", "Alpha"), seedObject("b2", "book", "Beta"),
                              seedObject("b3", "book", "Gamma"), seedObject("w1", "web", "Docs site")};
    if (!fake.start()) {
        fprintf(stderr, "FAIL: fake API could not listen on loopback\n");
        return 1;
    }
    qputenv("LUMEN_API_URL", fake.baseUrl().toString().toUtf8());
    qputenv("LUMEN_TOKEN", "collections-test");

    MainWindow window;
    window.resize(1448, 900);
    window.show();
    pass &= check(waitFor([&] { return window.ready; }), "startup reached ready state against the fake API");
    pass &= check(window.objectFor("b1").value("type_id").toString() == "book", "workspace objects loaded");

    // 1. Type page: a type without collections shows just "All" plus "+".
    window.showCollection("book");
    pass &= check(waitFor([&] { return visibleChild<QWidget>(window, "typeCollectionsBar") != nullptr; }),
                  "type page renders the collections chip row");
    pass &= check(waitFor([&] {
                      return visibleChild<QPushButton>(window, "collectionChipAll") &&
                             visibleChild<QPushButton>(window, "collectionChipNew");
                  }),
                  "empty type shows the All chip and the new-collection affordance");
    pass &= check(window.findChildren<QPushButton *>("collectionChip").isEmpty(),
                 "type without collections renders no collection chips");

    // 2. Create-collection flow: "+" chip opens a name dialog (item type is
    //    fixed to the current type), POSTs the object, opens the page.
    QTimer::singleShot(60, &window, [&] {
        auto *dialog = window.findChild<QInputDialog *>();
        if (!check(dialog, "new collection dialog opened")) return;
        if (auto *input = dialog->findChild<QLineEdit *>()) input->setText("Reading list");
        dialog->accept();
    });
    QTest::mouseClick(visibleChild<QPushButton>(window, "collectionChipNew"), Qt::LeftButton);
    pass &= check(waitFor([&] { return window.currentRoute() == "collection"; }),
                  "created collection opens its collection page");
    QString collectionId;
    for (const auto &v : fake.objects)
        if (v.toObject().value("type_id").toString() == "collection")
            collectionId = v.toObject().value("id").toString();
    pass &= check(!collectionId.isEmpty(), "collection object was created through the fake API");
    pass &= check(fakeSaw(fake, "POST", "/v1/objects", "Reading list") &&
                      fakeSaw(fake, "POST", "/v1/objects", "item_type_id"),
                  "create-collection POST carries the title and the fixed item type");
    pass &= check(window.objectFor(collectionId).value("properties").toObject().value("item_type_id").toString() ==
                      "book",
                  "created collection holds the current type");
    pass &= check(waitFor([&] { return visibleChild<QPushButton>(window, "collectionAddObjects") != nullptr; }),
                  "collection page offers Add objects");
    pass &= check(waitFor([&] {
                      auto *count = visibleChild<QLabel>(window, "collectionCountLabel");
                      return count && count->text() == "0 objects · curated order";
                  }),
                  "empty collection page shows the curated-order count");

    // 3. Add objects through the picker (filtered to the type, excludes members).
    QTimer::singleShot(60, &window, [&] {
        auto *dialog = window.findChild<QDialog *>("collectionAddObjectsDialog");
        if (!check(dialog, "add objects dialog opened")) return;
        auto *list = dialog->findChild<QListWidget *>("collectionAddList");
        if (!check(list && list->count() == 3, "picker lists the type's objects")) return;
        list->item(0)->setCheckState(Qt::Checked);
        list->item(1)->setCheckState(Qt::Checked);
        dialog->accept();
    });
    QTest::mouseClick(visibleChild<QPushButton>(window, "collectionAddObjects"), Qt::LeftButton);
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() ==
                                      QStringList{"b1", "b2"}; }),
                  "added objects appear in the collection view in insertion order");
    pass &= check(fakeSaw(fake, "POST", "/v1/collections/" + collectionId + "/items"),
                  "add flow uses the collection items endpoint");

    // 4. Curated order: append, then move an item up (PUT replaces the list).
    window.addToCollection(collectionId, {"b3"});
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"b1", "b2", "b3"}; }),
                  "appended object lands at the end of the curated order");
    window.moveCollectionItem(collectionId, "b3", -2);
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"b3", "b1", "b2"}; }),
                  "move up reorders the curated list through the view evaluator");
    bool putSeen = false;
    for (const auto &request : fake.log)
        if (request.method == "PUT" && request.path.contains("/v1/collections/" + collectionId + "/items") &&
            QJsonDocument(request.body).toJson(QJsonDocument::Compact).contains("\"b3\"") &&
            QJsonDocument(request.body).toJson(QJsonDocument::Compact).contains("\"b1\""))
            putSeen = true;
    pass &= check(putSeen, "reorder persists the full ordered list with PUT items");

    // 5. Chips render with counts; a chip click filters the view in curated order.
    window.showCollection("book");
    pass &= check(waitFor([&] { return chipWithText(window, "Reading list  ·  3") != nullptr; }),
                  "type page chip shows the collection name and its item count");
    // "All" is the first chip and returns to the unfiltered type view.
    QTest::mouseClick(visibleChild<QPushButton>(window, "collectionChipAll"), Qt::LeftButton);
    pass &= check(waitFor([&] {
                      return window.currentRoute() == "gallery" && activeViews(window) &&
                             activeViews(window)->visibleIds() == QStringList{"b1", "b2", "b3"};
                  }),
                  "All chip returns to the type view without a collection scope");
    window.showCollection("book");
    pass &= check(waitFor([&] { return chipWithText(window, "Reading list") != nullptr; }),
                  "type page chips rebuild after returning from the All view");
    QTest::mouseClick(chipWithText(window, "Reading list"), Qt::LeftButton);
    pass &= check(waitFor([&] {
                      return window.currentRoute() == "collection" && activeViews(window) &&
                             activeViews(window)->visibleIds() == QStringList{"b3", "b1", "b2"};
                  }),
                  "chip click opens the collection page in curated order");
    pass &= check(activeViews(window) && activeViews(window)->viewSpec().value("order").toString() == "manual",
                  "collection view keeps the manual order marker in its spec");

    // 6. Context menu actions: move down and remove from collection.
    auto *menu = window.buildObjectMenu("b3", collectionId);
    QAction *moveDown = nullptr, *remove = nullptr;
    for (auto *action : menu->actions()) {
        if (action->text() == "Move down")
            moveDown = action;
        if (action->text() == "Remove from collection")
            remove = action;
    }
    pass &= check(moveDown && remove, "collection page context menu offers move and remove");
    if (moveDown)
        moveDown->trigger();
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"b1", "b3", "b2"}; }),
                  "move down reorders the curated list");
    if (remove)
        remove->trigger();
    menu->deleteLater();
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"b1", "b2"}; }),
                  "remove from collection preserves the order of the remaining items");
    pass &= check(fakeSaw(fake, "DELETE", "/v1/collections/" + collectionId + "/items/b3"),
                  "remove uses the item delete endpoint");

    // 7. Duplicate and wrong-type rejections surface as non-blocking notices.
    window.failure.clear();
    window.addToCollection(collectionId, {"b1"});
    pass &= check(waitFor([&] { return window.failure.contains("Already in this collection"); }),
                  "duplicate item (409) surfaces as a status message");
    bool noticeVisible = false;
    for (auto *note : window.findChildren<QLabel *>("notice"))
        if (note->isVisible() && !note->text().isEmpty())
            noticeVisible = true;
    pass &= check(noticeVisible, "rejection notice is visible and non-blocking");
    window.failure.clear();
    window.addToCollection(collectionId, {"w1"});
    pass &= check(waitFor([&] { return window.failure.contains("collection type"); }),
                  "wrong-type item (400) surfaces as a status message");
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() == QStringList{"b1", "b2"}; }),
                  "rejected items never enter the curated order");

    // 8. Object page shows the collections that contain the object.
    window.openObject("b1");
    pass &= check(waitFor([&] {
                      auto *section = visibleChild<QWidget>(window, "objectCollectionsSection");
                      if (!section)
                          return false;
                      for (auto *chip : section->findChildren<QPushButton *>("objectChip"))
                          if (chip->text() == "Reading list")
                              return true;
                      return false;
                  }),
                  "object page lists containing collections");
    pass &= check(waitFor([&] { return fakeSaw(fake, "GET", "/v1/objects/b1/backlinks?type_id=collection"); }),
                  "containing collections use the backlinks?type_id=collection lookup");
    if (auto *section = visibleChild<QWidget>(window, "objectCollectionsSection"))
        for (auto *chip : section->findChildren<QPushButton *>("objectChip"))
            if (chip->text() == "Reading list") {
                QTest::mouseClick(chip, Qt::LeftButton);
                break;
            }
    pass &= check(waitFor([&] { return window.currentRoute() == "collection"; }),
                  "object page collection chip opens the collection page");

    // 9. "Add to collection…" on a type-page object row.
    window.showCollection("book");
    auto *typeMenu = window.buildObjectMenu("b3", {});
    QMenu *addMenu = nullptr;
    for (auto *action : typeMenu->actions())
        if (action->text() == "Add to collection…")
            addMenu = action->menu();
    bool addOffered = false;
    if (addMenu)
        for (auto *action : addMenu->actions())
            if (action->text() == "Reading list") {
                addOffered = true;
                action->trigger();
            }
    pass &= check(addOffered, "object row menu offers the type's collections");
    typeMenu->deleteLater();
    pass &= check(waitFor([&] { return chipWithText(window, "Reading list") != nullptr; }),
                  "type page chips are visible before opening the collection");
    QTest::mouseClick(chipWithText(window, "Reading list"), Qt::LeftButton);
    pass &= check(waitFor([&] { return activeViews(window) && activeViews(window)->visibleIds() ==
                                      QStringList{"b1", "b2", "b3"}; }),
                  "context-menu add appends the object to the curated order");

    // 10. Sidebar rows under the type + delete flow.
    QPushButton *sidebarRow = nullptr;
    pass &= check(waitFor([&] {
                      for (auto *row : window.findChildren<QPushButton *>("sidebarCollectionRow"))
                          if (row->isVisible() && row->text().startsWith("Reading list")) {
                              sidebarRow = row;
                              return true;
                          }
                      return false;
                  }),
                  "sidebar renders the collection under its type with the item count");
    pass &= check(sidebarRow && sidebarRow->text() == "Reading list  ·  3",
                  "sidebar collection row shows the item count");
    if (sidebarRow)
        QTest::mouseClick(sidebarRow, Qt::LeftButton);
    pass &= check(waitFor([&] { return window.currentRoute() == "collection"; }),
                  "sidebar collection row opens the collection page");
    window.deleteCollection(collectionId);
    pass &= check(waitFor([&] {
                      for (auto *row : window.findChildren<QPushButton *>("sidebarCollectionRow"))
                          if (row->isVisible() && row->text().startsWith("Reading list"))
                              return false;
                      return window.currentRoute() == "gallery";
                  }),
                  "deleted collection leaves the sidebar and returns to the type page");
    pass &= check(waitFor([&] {
                      return visibleChild<QWidget>(window, "typeCollectionsBar") &&
                             visibleChild<QPushButton>(window, "collectionChip") == nullptr;
                  }),
                  "type page chips rebuild after deletion");
    pass &= check(window.objectFor(collectionId).isEmpty(), "deleted collection is gone from the workspace cache");

    // 11. "New collection…" from an object's row menu creates the collection
    //     and adds that object in one flow.
    auto *newMenu = window.buildObjectMenu("b1", {});
    QMenu *newAddMenu = nullptr;
    for (auto *action : newMenu->actions())
        if (action->text() == "Add to collection…")
            newAddMenu = action->menu();
    QAction *newCollection = nullptr;
    if (newAddMenu)
        for (auto *action : newAddMenu->actions())
            if (action->text() == "New collection…")
                newCollection = action;
    pass &= check(newCollection != nullptr, "object row menu offers New collection…");
    QTimer::singleShot(60, &window, [&] {
        auto *dialog = window.findChild<QInputDialog *>();
        if (!check(dialog, "new collection dialog opened from the object menu")) return;
        if (auto *input = dialog->findChild<QLineEdit *>()) input->setText("Favorites");
        dialog->accept();
    });
    if (newCollection)
        newCollection->trigger();
    newMenu->deleteLater();
    QString favoritesId;
    pass &= check(waitFor([&] {
                      for (const auto &v : fake.objects) {
                          const auto c = v.toObject();
                          if (c.value("type_id").toString() == "collection" &&
                              c.value("title").toString() == "Favorites") {
                              favoritesId = c.value("id").toString();
                              return true;
                          }
                      }
                      return false;
                  }),
                  "New collection… creates the collection object");
    pass &= check(waitFor([&] {
                      return window.currentRoute() == "collection" && activeViews(window) &&
                             activeViews(window)->visibleIds() == QStringList{"b1"};
                  }),
                  "New collection… from the object menu adds that object immediately");
    pass &= check(waitFor([&] { return fakeSaw(fake, "POST", "/v1/collections/" + favoritesId + "/items", "b1"); }),
                  "New collection… uses the collection items endpoint for the source object");
    if (!favoritesId.isEmpty())
        window.deleteCollection(favoritesId);
    pass &= check(waitFor([&] { return window.currentRoute() == "gallery"; }),
                  "cleanup returns to the type page");

    if (pass)
        fprintf(stdout,
                "LUMEN_COLLECTIONS_OK: create dialog, add picker, curated order, chip counts and click, All chip, "
                "move/remove menu, 409/400 notices, object page lookup, sidebar rows, delete, new-from-menu\n");
    return pass ? 0 : 1;
}
