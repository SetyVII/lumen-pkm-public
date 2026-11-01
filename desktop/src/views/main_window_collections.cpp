#include "main_window.hpp"

// Desktop stage "collections-ui": curated, ordered lists of objects of ONE
// type (Capacities: https://docs.capacities.io/reference/collections).
// A collection is an ordinary object of the default type `collection`; every
// mutation goes through the collection endpoints of the backend contract.

namespace {
// A collection chip accepts objects dragged from list/gallery/kanban cards
// (mime "application/x-lumen-object", produced by LumenViews::NativeCard).
class CollectionChip final : public QPushButton {
  public:
    std::function<void(const QString &)> onDropObject;
    explicit CollectionChip(const QString &text, QWidget *parent = nullptr) : QPushButton(text, parent) {
        setObjectName("collectionChip");
        setAcceptDrops(true);
        setCursor(Qt::PointingHandCursor);
        setMinimumHeight(30);
        setAccessibleName(text);
    }

  protected:
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasFormat("application/x-lumen-object"))
            e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (e->mimeData()->hasFormat("application/x-lumen-object"))
            e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        const QString objectId = QString::fromUtf8(e->mimeData()->data("application/x-lumen-object"));
        if (!objectId.isEmpty() && onDropObject)
            onDropObject(objectId);
        e->acceptProposedAction();
    }
};
}  // namespace

QJsonArray MainWindow::collectionsForType(const QString &typeId) const {
    QList<QJsonObject> found;
    for (auto v : objects) {
        auto c = v.toObject();
        if (str(c, "type_id") != "collection")
            continue;
        if (c.value("properties").toObject().value("item_type_id").toString() != typeId)
            continue;
        found << c;
    }
    // Deterministic display order for chips and sidebar rows.
    std::sort(found.begin(), found.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString first = str(a, "title"), second = str(b, "title");
        return first == second ? str(a, "id") < str(b, "id")
                                : QString::localeAwareCompare(first, second) < 0;
    });
    QJsonArray result;
    for (const auto &c : found) result.append(c);
    return result;
}

QStringList MainWindow::collectionItemIds(const QJsonObject &collection) {
    QStringList ids;
    for (auto v : collection.value("properties").toObject().value("items").toArray())
        ids << v.toString();
    return ids;
}

QString MainWindow::collectionCountText(const QJsonObject &collection) {
    const int count = collection.value("properties").toObject().value("items").toArray().size();
    return QString("%1 %2 · curated order").arg(count).arg(count == 1 ? "object" : "objects");
}

void MainWindow::rememberActiveViewSpec() {
    if (!activeViews)
        return;
    if (route == "collection" && str(current, "type_id") == "collection")
        collectionSpecs["collection:" + str(current, "id")] = activeViews->viewSpec();
    else if (route == "gallery" && !typeFilter.isEmpty() && typeSection != "all")
        // Dashboard sections remember their view tweaks separately from the
        // type's base "All" spec, so switching tabs cannot leak one section's
        // id set into another.
        collectionSpecs["dashboard:" + typeFilter + ":" + typeSection] = activeViews->viewSpec();
    else
        collectionSpecs[typeFilter] = activeViews->viewSpec();
}

QWidget *MainWindow::buildCollectionChips(const QString &typeId) {
    auto *bar = new QWidget;
    bar->setObjectName("typeCollectionsBar");
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);
    fillCollectionChips(bar, row, typeId);
    return bar;
}

void MainWindow::fillCollectionChips(QWidget *bar, QHBoxLayout *row, const QString &typeId) {
    auto *all = button("All", "layout-grid", "collectionChipAll");
    all->setToolTip("Every object of this type");
    connect(all, &QPushButton::clicked, this, [this, typeId] { showCollection(typeId); });
    row->addWidget(all);
    for (auto v : collectionsForType(typeId)) {
        const auto c = v.toObject();
        const QString id = str(c, "id");
        const int count = c.value("properties").toObject().value("items").toArray().size();
        auto *chip = new CollectionChip(QString("%1  ·  %2").arg(str(c, "title")).arg(count), bar);
        chip->setIcon(LumenIcons::icon(str(typeFor("collection"), "icon"), dark ? QColor("#c9bce2") : QColor("#73519b"), 16));
        chip->setIconSize(QSize(16, 16));
        chip->setToolTip("Open collection · " + collectionCountText(c));
        chip->setProperty("collectionId", id);
        chip->onDropObject = [this, id](const QString &objectId) { addToCollection(id, {objectId}); };
        connect(chip, &QPushButton::clicked, this, [this, id] { showCollectionPage(id); });
        chip->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(chip, &QWidget::customContextMenuRequested, chip, [this, chip, id, c](const QPoint &pos) {
            QMenu menu(this);
            menu.addAction("Open collection", [this, id] { showCollectionPage(id); });
            menu.addAction("Add objects…", [this, id] { pickObjectsForCollection(id); });
            menu.addSeparator();
            menu.addAction(LumenIcons::icon("x"), "Delete collection", [this, id, c] {
                QMessageBox box(this);
                box.setWindowTitle("Delete collection");
                box.setText(QString("Delete \"%1\"? The objects themselves are kept.").arg(str(c, "title")));
                box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                box.setDefaultButton(QMessageBox::No);
                if (box.exec() == QMessageBox::Yes)
                    deleteCollection(id);
            });
            menu.exec(chip->mapToGlobal(pos));
        });
        row->addWidget(chip);
    }
    auto *add = button("", "plus", "collectionChipNew");
    add->setToolTip("New collection…");
    add->setAccessibleName("New collection");
    connect(add, &QPushButton::clicked, this, [this, typeId] { newCollectionDialog(typeId); });
    row->addWidget(add);
    row->addStretch(1);
}

void MainWindow::updateCollectionChips(const QString &typeId) {
    QWidget *bar = nullptr;
    for (auto *candidate : findChildren<QWidget *>("typeCollectionsBar"))
        if (candidate->isVisible())
            bar = candidate;
    if (!bar)
        return;
    auto *row = qobject_cast<QHBoxLayout *>(bar->layout());
    if (!row)
        return;
    clearLayout(row);
    fillCollectionChips(bar, row, typeId);
}

void MainWindow::applyCollectionScope(LumenViews *views, const QJsonObject &collection) {
    auto spec = views->viewSpec();
    spec["ids"] = collection.value("properties").toObject().value("items").toArray();
    spec["order"] = "manual";
    views->setScope(collection.value("properties").toObject().value("item_type_id").toString(), spec);
}

void MainWindow::showCollectionPage(const QString &collectionId) {
    auto collection = objectFor(collectionId);
    if (str(collection, "type_id") != "collection") {
        // Not cached yet (e.g. opened from a fresh backlink): load, then retry.
        api.get("/v1/objects/" + collectionId, [this, collectionId](QJsonValue v, QString e) {
            if (!e.isEmpty()) {
                error("Could not open the collection: " + e);
                return;
            }
            rememberObject(v.toObject());
            showCollectionPage(collectionId);
        });
        return;
    }
    if (!canLeave([this, collectionId] {
            showCollectionPage(collectionId);
        }))
        return;
    rememberPosition();
    rememberActiveViewSpec();
    const QString itemTypeId = collection.value("properties").toObject().value("item_type_id").toString();
    current = collection;
    typeFilter = itemTypeId;
    linked = collection.value("links").toArray();
    resetBody();
    setInfoVisible(false);
    updateStatusLabel();
    const auto typ = typeFor(itemTypeId);
    auto *head = new QHBoxLayout;
    auto *badge = new QLabel;
    badge->setPixmap(LumenIcons::badge(str(typ, "icon"), QColor(str(typ, "color")), 32, dark));
    head->addWidget(badge);
    // Collections are objects: the title is the normal rename flow (edit +
    // save), exactly like the object page.
    title = new DocumentTitle(str(collection, "title"));
    title->setObjectName("documentTitle");
    loading = true;
    connect(title, &QTextEdit::textChanged, this, [this] {
        if (!loading)
            markDirty();
    });
    head->addWidget(title, 1);
    auto *addObjects = button("Add objects", "plus", "collectionAddObjects");
    addObjects->setToolTip("Pick objects of this type to append to the collection");
    connect(addObjects, &QPushButton::clicked, this, [this, collectionId] { pickObjectsForCollection(collectionId); });
    head->addWidget(addObjects);
    auto *moreBtn = new QToolButton;
    moreBtn->setIcon(LumenIcons::icon("dots", dark ? QColor("#e4e3e2") : QColor("#222224"), 20));
    moreBtn->setAutoRaise(true);
    moreBtn->setToolTip("Collection options");
    moreBtn->setMinimumSize(30, 30);
    connect(moreBtn, &QToolButton::clicked, moreBtn, [this, moreBtn, collectionId, collection, typ, itemTypeId] {
        QMenu menu(this);
        menu.addAction("Rename", [this] {
            if (title) title->setFocus();
        });
        menu.addAction("Show all " + str(typ, "name") + " objects", [this, itemTypeId] { showCollection(itemTypeId); });
        menu.addSeparator();
        menu.addAction(LumenIcons::icon("x"), "Delete collection", [this, collectionId, collection] {
            QMessageBox box(this);
            box.setWindowTitle("Delete collection");
            box.setText(QString("Delete \"%1\"? The objects themselves are kept.").arg(str(collection, "title")));
            box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            box.setDefaultButton(QMessageBox::No);
            if (box.exec() == QMessageBox::Yes)
                deleteCollection(collectionId);
        });
        menu.exec(moreBtn->mapToGlobal(QPoint(0, moreBtn->height())));
    });
    head->addWidget(moreBtn);
    body->addLayout(head);
    auto *meta = label(collectionCountText(collection), "muted");
    meta->setObjectName("collectionCountLabel");
    body->addWidget(meta);
    activeViews = new LumenViews;
    activeViews->setObjectName("workspaceViews");
    configureViews(activeViews);
    auto spec = collectionSpecs.value("collection:" + collectionId);
    if (!spec.contains("presentation"))
        spec["presentation"] = "list";
    spec["ids"] = collection.value("properties").toObject().value("items").toArray();
    spec["order"] = "manual";
    activeViews->setData(objects, types);
    activeViews->setScope(itemTypeId, spec);
    // On the collection page the empty state's "New" button means "add
    // existing objects", not "create a new object".
    activeViews->onCreate = [this, collectionId](const QString &) { pickObjectsForCollection(collectionId); };
    body->addWidget(activeViews, 1);
    inspect->addWidget(label("Collections", "subheading"));
    inspect->addWidget(label("A curated, ordered list of one object type. Right-click an object to "
                              "reorder or remove it; drag cards from the type view onto a chip to add them.",
                              "muted"));
    inspect->addStretch();
    crumb->setText(str(collection, "title"));
    // Set the route only when the page is complete: navigation waits and tests
    // observe currentRoute() as "the page is ready", not "the build started".
    route = "collection";
    rememberRecent(collectionId);
    rebuildSidebar();
    recordNavigation();
    loading = false;
    dirty = false;
    saveLabel->setText(saveState(current));
}

void MainWindow::collectionUpdated(const QJsonObject &collection) {
    const QString id = str(collection, "id");
    if (id.isEmpty())
        return;
    rememberObject(collection);
    if (route == "collection" && str(current, "id") == id) {
        if (!dirty && !saving) {
            current = collection;
        } else {
            // The user is editing the title (or a save is in flight). Merge the
            // server's item list and revision into the local object so a later
            // title save cannot overwrite the item mutation we just performed.
            auto properties = current.value("properties").toObject();
            const auto remote = collection.value("properties").toObject();
            properties["items"] = remote.value("items");
            properties["item_type_id"] = remote.value("item_type_id");
            current["properties"] = properties;
            current["revision"] = collection.value("revision");
        }
        if (activeViews) {
            applyCollectionScope(activeViews, collection);
            activeViews->setData(objects, types);
        }
        for (auto *count : findChildren<QLabel *>("collectionCountLabel"))
            if (count->isVisible())
                count->setText(collectionCountText(collection));
    }
    if (route == "gallery" || route == "query")
        updateCollectionChips(typeFilter);
    // A collection mutation changes the Collections section and every count
    // that depends on membership (backlinks included); refresh the dashboard.
    if (route == "gallery" && !typeFilter.isEmpty()) {
        updateTypeDashboard(typeFilter);
        if (typeSection == "collections")
            refreshTypeDashboard(typeFilter);
    }
    rebuildSidebar();
}

void MainWindow::newCollectionDialog(const QString &typeId, const QString &objectId) {
    if (typeId.isEmpty() || typeId == "collection")
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, "New collection", "Collection name", QLineEdit::Normal, "", &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    createCollection(typeId, name, objectId);
}

void MainWindow::createCollection(const QString &typeId, const QString &name, const QString &objectId) {
    if (typeId.isEmpty() || typeId == "collection")
        return;
    saveLabel->setText("Creating collection…");
    api.requestEx("POST", "/v1/objects",
                  QJsonObject{{"type_id", "collection"}, {"title", name}, {"content", ""},
                              {"properties", QJsonObject{{"item_type_id", typeId}, {"items", QJsonArray{}}}},
                              {"links", QJsonArray{}}},
                  [this, objectId](int status, QJsonValue v, QString e) {
                      if (status != 201 || !e.isEmpty()) {
                          error("Could not create the collection: " + (e.isEmpty() ? QString("HTTP %1").arg(status) : e));
                          return;
                      }
                      auto collection = v.toObject();
                      const QString collectionId = str(collection, "id");
                      rememberObject(collection);
                      rebuildSidebar();
                      updateStatusLabel();
                      // "New collection…" from an object's context menu adds
                      // that object immediately; the "+" chip creates an empty
                      // collection and just opens it.
                      if (!objectId.isEmpty() && !collectionId.isEmpty())
                          addToCollection(collectionId, {objectId});
                      showCollectionPage(collectionId);
                  });
}

void MainWindow::addToCollection(const QString &collectionId, const QStringList &objectIds) {
    if (objectIds.isEmpty())
        return;
    auto queue = std::make_shared<QStringList>(objectIds);
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, collectionId, queue, step] {
        if (queue->isEmpty())
            return;
        const QString objectId = queue->takeFirst();
        saveLabel->setText("Adding to collection…");
        api.requestEx("POST", "/v1/collections/" + collectionId + "/items", QJsonObject{{"object_id", objectId}},
                      [this, collectionId, queue, step](int status, QJsonValue v, QString e) {
                          if (status == 200) {
                              collectionUpdated(v.toObject());
                          } else if (status == 409) {
                              error("Already in this collection.");
                          } else {
                              error("Could not add to the collection: " +
                                    (e.isEmpty() ? QString("HTTP %1").arg(status) : e));
                          }
                          (*step)();
                      });
    };
    (*step)();
}

void MainWindow::removeFromCollection(const QString &collectionId, const QString &objectId) {
    saveLabel->setText("Removing from collection…");
    auto resync = [this, collectionId] {
        api.get("/v1/objects/" + collectionId, [this](QJsonValue v, QString e) {
            if (e.isEmpty())
                collectionUpdated(v.toObject());
        });
    };
    api.requestEx("DELETE", "/v1/collections/" + collectionId + "/items/" + objectId, {},
                  [this, resync](int status, QJsonValue v, QString e) {
                      if (status == 200) {
                          collectionUpdated(v.toObject());
                      } else if (status == 404) {
                          // Contract: 404 means "not in the collection"; a
                          // retrying client treats it as already removed.
                          resync();
                      } else {
                          error("Could not remove from the collection: " +
                                (e.isEmpty() ? QString("HTTP %1").arg(status) : e));
                      }
                  });
}

void MainWindow::moveCollectionItem(const QString &collectionId, const QString &objectId, int delta) {
    if (delta == 0)
        return;
    QStringList items = collectionItemIds(objectFor(collectionId));
    const int from = items.indexOf(objectId);
    const int to = from + delta;
    if (from < 0 || to < 0 || to >= items.size())
        return;
    items.removeAt(from);
    items.insert(to, objectId);
    reorderCollection(collectionId, items);
}

void MainWindow::reorderCollection(const QString &collectionId, const QStringList &itemIds) {
    QJsonArray items;
    for (const auto &id : itemIds) items.append(id);
    saveLabel->setText("Reordering collection…");
    api.requestEx("PUT", "/v1/collections/" + collectionId + "/items", QJsonObject{{"items", items}},
                  [this](int status, QJsonValue v, QString e) {
                      if (status == 200) {
                          collectionUpdated(v.toObject());
                      } else {
                          error("Could not reorder the collection: " +
                                (e.isEmpty() ? QString("HTTP %1").arg(status) : e));
                      }
                  });
}

void MainWindow::deleteCollection(const QString &collectionId) {
    const QString itemTypeId = objectFor(collectionId).value("properties").toObject().value("item_type_id").toString();
    api.requestEx("DELETE", "/v1/objects/" + collectionId, {}, [this, collectionId, itemTypeId](int status, QJsonValue, QString e) {
        if (status != 200) {
            error("Could not delete the collection: " + (e.isEmpty() ? QString("HTTP %1").arg(status) : e));
            return;
        }
        for (int i = 0; i < objects.size(); ++i)
            if (str(objects[i].toObject(), "id") == collectionId) {
                objects.removeAt(i);
                break;
            }
        knownObjectIds.remove(collectionId);
        collectionSpecs.remove("collection:" + collectionId);
        scrollPositions.remove(collectionId);
        // Never PATCH a deleted object: drop any pending title edit for it.
        if (str(current, "id") == collectionId) {
            dirty = false;
            pendingNavigation = {};
        }
        rebuildSidebar();
        if (route == "collection" && str(current, "id") == collectionId) {
            // Do not let rememberActiveViewSpec() persist the deleted page's
            // ids/order under the type key: clear the page pointer first.
            activeViews = nullptr;
            current = {};
            showCollection(itemTypeId);
        } else if (route == "gallery" || route == "query") {
            updateCollectionChips(typeFilter);
        }
    });
}

void MainWindow::pickObjectsForCollection(const QString &collectionId) {
    const auto collection = objectFor(collectionId);
    if (str(collection, "type_id") != "collection")
        return;
    const QString itemTypeId = collection.value("properties").toObject().value("item_type_id").toString();
    const QStringList existing = collectionItemIds(collection);
    QDialog dialog(this);
    dialog.setObjectName("collectionAddObjectsDialog");
    dialog.setWindowTitle("Add objects");
    dialog.resize(520, 460);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(label(QString("Add %1 objects to \"%2\"").arg(str(typeFor(itemTypeId), "name")).arg(str(collection, "title")), "subheading"));
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search objects…");
    search->setClearButtonEnabled(true);
    search->setObjectName("collectionAddSearch");
    layout->addWidget(search);
    auto *list = new QListWidget;
    list->setObjectName("collectionAddList");
    layout->addWidget(list, 1);
    for (auto v : objects) {
        auto o = v.toObject();
        if (str(o, "type_id") != itemTypeId || existing.contains(str(o, "id")))
            continue;
        auto *item = new QListWidgetItem(str(o, "title"), list);
        item->setData(Qt::UserRole, str(o, "id"));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }
    if (list->count() == 0)
        layout->addWidget(label("No more objects of this type to add.", "muted"));
    connect(search, &QLineEdit::textChanged, &dialog, [list](const QString &text) {
        int first = -1;
        for (int i = 0; i < list->count(); ++i) {
            const bool hidden = !list->item(i)->text().contains(text, Qt::CaseInsensitive);
            list->item(i)->setHidden(hidden);
            if (!hidden && first < 0)
                first = i;
        }
        list->setCurrentRow(first);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Add");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    search->setFocus();
    if (dialog.exec() != QDialog::Accepted)
        return;
    QStringList ids;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            ids << list->item(i)->data(Qt::UserRole).toString();
    addToCollection(collectionId, ids);
}

QMenu *MainWindow::buildObjectMenu(const QString &objectId, const QString &collectionId) {
    auto *menu = new QMenu(this);
    menu->addAction("Open", [this, objectId] { openPreview(objectId); });
    menu->addAction("Open in new tab", [this, objectId] { newTab(objectId); });
    menu->addSeparator();
    if (!collectionId.isEmpty()) {
        menu->addAction("Move up", [this, collectionId, objectId] { moveCollectionItem(collectionId, objectId, -1); });
        menu->addAction("Move down", [this, collectionId, objectId] { moveCollectionItem(collectionId, objectId, 1); });
        menu->addSeparator();
        menu->addAction("Remove from collection", [this, collectionId, objectId] {
            removeFromCollection(collectionId, objectId);
        });
        return menu;
    }
    const auto object = objectFor(objectId);
    const QString typeId = str(object, "type_id");
    // Collections cannot contain collections (backend invariant), so there is
    // nothing useful to offer on a collection object's own menu.
    if (typeId == "collection")
        return menu;
    // D5 (Capacities templates): rows can save their object as a template of
    // its type, or drop the flag when it already is one.
    menu->addAction(LumenIcons::icon("package-open"),
                    objectIsTemplate(object) ? "Unsave template" : "Save as template",
                    [this, objectId, object] {
                        setObjectTemplateFlag(objectId, !objectIsTemplate(object));
                    });
    menu->addSeparator();
    auto *add = menu->addMenu("Add to collection…");
    const auto collections = collectionsForType(typeId);
    if (collections.isEmpty()) {
        auto *create = add->addAction("New collection…");
        connect(create, &QAction::triggered, this, [this, typeId, objectId] { newCollectionDialog(typeId, objectId); });
    } else {
        for (auto v : collections) {
            const auto c = v.toObject();
            const QString id = str(c, "id");
            const bool alreadyIn = c.value("properties").toObject().value("items").toArray().contains(objectId);
            auto *action = add->addAction(str(c, "title"), [this, id, objectId] { addToCollection(id, {objectId}); });
            if (alreadyIn) {
                action->setCheckable(true);
                action->setChecked(true);
                action->setEnabled(false);
            }
        }
        add->addSeparator();
        auto *create = add->addAction("New collection…");
        connect(create, &QAction::triggered, this, [this, typeId, objectId] { newCollectionDialog(typeId, objectId); });
    }
    return menu;
}

void MainWindow::showObjectMenu(const QString &objectId, const QPoint &globalPos) {
    const QString collectionId = route == "collection" ? str(current, "id") : QString();
    auto *menu = buildObjectMenu(objectId, collectionId);
    menu->exec(globalPos);
    menu->deleteLater();
}

void MainWindow::fillObjectCollections(QWidget *panel, const QJsonArray &collections, const QString &objectId) {
    Q_UNUSED(objectId);
    auto *layout = qobject_cast<QVBoxLayout *>(panel->layout());
    if (!layout)
        return;
    clearLayout(layout);
    if (collections.isEmpty()) {
        panel->hide();
        return;
    }
    panel->show();
    layout->addWidget(label("Collections", "subheading"));
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    for (auto v : collections) {
        const auto c = v.toObject();
        const QString id = str(c, "id");
        auto *chip = button(str(c, "title"), str(typeFor("collection"), "icon"), "objectChip");
        chip->setToolTip(collectionCountText(c));
        connect(chip, &QPushButton::clicked, this, [this, id] { showCollectionPage(id); });
        row->addWidget(chip);
    }
    row->addStretch();
    layout->addLayout(row);
}

void MainWindow::renderObjectCollections(QWidget *panel, const QString &objectId) {
    QJsonArray containing;
    for (auto v : objects) {
        auto c = v.toObject();
        if (str(c, "type_id") != "collection")
            continue;
        if (c.value("properties").toObject().value("items").toArray().contains(objectId))
            containing.append(c);
    }
    fillObjectCollections(panel, containing, objectId);
    // Authoritative lookup per the contract: backlinks filtered to collections.
    QPointer<QWidget> guard(panel);
    api.get("/v1/objects/" + objectId + "/backlinks?type_id=collection", [this, guard, objectId](QJsonValue v, QString e) {
        if (!guard || !e.isEmpty())
            return;
        if (route != "object" || str(current, "id") != objectId)
            return;
        QJsonArray sorted = arr(v);
        QList<QJsonObject> listed;
        for (auto item : sorted) listed << item.toObject();
        std::sort(listed.begin(), listed.end(), [](const QJsonObject &a, const QJsonObject &b) {
            const QString first = str(a, "title"), second = str(b, "title");
            return first == second ? str(a, "id") < str(b, "id")
                                   : QString::localeAwareCompare(first, second) < 0;
        });
        sorted = QJsonArray();
        for (const auto &c : listed) sorted.append(c);
        fillObjectCollections(guard, sorted, objectId);
    });
}
