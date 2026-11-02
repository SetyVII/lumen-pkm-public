#include "main_window.hpp"

#include <QButtonGroup>
#include <QSettings>
#include <QUrlQuery>


namespace {
// Display cap for the Recent section: the persisted recents snapshot and the
// per-type tab slice share this size. The in-session list keeps its longer
// 80-entry window for the overview sections.
constexpr int kRecentTabCap = 20;

bool blankValue(const QJsonValue &v) {
    return v.isNull() || v.isUndefined() || (v.isString() && v.toString().trimmed().isEmpty()) ||
           (v.isArray() && v.toArray().isEmpty());
}

QJsonArray idArray(const QStringList &ids) {
    QJsonArray array;
    for (const auto &id : ids) array.append(id);
    return array;
}
}  // namespace

QStringList MainWindow::dashboardSectionKeys() {
    return {"recent", "untagged", "no_backlinks", "collections", "templates"};
}

QString MainWindow::dashboardSectionLabel(const QString &section) {
    if (section == "recent") return "Recent";
    if (section == "untagged") return "Untagged";
    if (section == "no_backlinks") return "No backlinks";
    if (section == "collections") return "Collections";
    if (section == "templates") return "Templates";
    return "All";
}

// Built-in section visibility is a per-type client preference, persisted in
// QSettings as the list of HIDDEN sections (an empty stored list means "all
// visible", which keeps the default and the fully-toggled-off state apart).
// The D5 "templates" section is NOT part of this default-visible set: its
// visibility is data-driven (see templatesSectionVisible).
QStringList MainWindow::visibleTypeSections(const QString &typeId) const {
    if (typeId.isEmpty())
        return {};
    QSettings pref("Lumen", "Lumen");
    const QStringList hidden = pref.value("typeDashboards/" + typeId + "/hidden").toStringList();
    QStringList visible;
    for (const auto &key : dashboardSectionKeys())
        if (key != "templates" && !hidden.contains(key))
            visible << key;
    return visible;
}

void MainWindow::setVisibleTypeSections(const QString &typeId, const QStringList &sections) {
    if (typeId.isEmpty())
        return;
    QStringList hidden;
    for (const auto &key : dashboardSectionKeys())
        if (!sections.contains(key))
            hidden << key;
    QSettings pref("Lumen", "Lumen");
    pref.setValue("typeDashboards/" + typeId + "/hidden", hidden);
}

bool MainWindow::objectHasTags(const QJsonObject &object) {
    return !blankValue(object.value("properties").toObject().value("tags"));
}

QStringList MainWindow::recentTypeIds(const QString &typeId) const {
    QStringList ids;
    for (const auto &id : recentObjectIds) {
        if (ids.size() >= kRecentTabCap)
            break;
        if (str(objectFor(id), "type_id") == typeId)
            ids << id;
    }
    return ids;
}

QStringList MainWindow::untaggedTypeIds(const QString &typeId) const {
    QStringList ids;
    for (auto v : objects) {
        const auto object = v.toObject();
        if (str(object, "type_id") != typeId)
            continue;
        if (!objectHasTags(object))
            ids << str(object, "id");
    }
    return ids;
}

// Saved queries are ordinary objects of the type `query` whose properties.view
// spec carries the type scope. A query belongs to the type named in its spec;
// queries without a type scope stay on the sidebar only (documented choice).
QJsonArray MainWindow::queriesForType(const QString &typeId) const {
    QList<QJsonObject> found;
    for (auto v : objects) {
        const auto query = v.toObject();
        if (str(query, "type_id") != "query")
            continue;
        if (query.value("properties").toObject().value("view").toObject().value("type_id").toString() != typeId)
            continue;
        found << query;
    }
    std::sort(found.begin(), found.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString first = str(a, "title"), second = str(b, "title");
        return first == second ? str(a, "id") < str(b, "id")
                               : QString::localeAwareCompare(first, second) < 0;
    });
    QJsonArray result;
    for (const auto &query : found) result.append(query);
    return result;
}

// "No backlinks" is the one built-in section served by a server-side filter
// (contracts/SYNC.md "Object list filters"); everything else derives from the
// workspace cache the client already holds.
void MainWindow::requestNoBacklinkIds(const QString &typeId) {
    if (typeId.isEmpty() || noBacklinkPending.contains(typeId))
        return;
    noBacklinkPending.insert(typeId);
    QUrlQuery query;
    query.addQueryItem("type_id", typeId);
    query.addQueryItem("backlinked_count", "0");
    api.get("/v1/objects?" + query.toString(QUrl::FullyEncoded),
            [this, typeId](QJsonValue value, QString err) {
                noBacklinkPending.remove(typeId);
                if (!err.isEmpty())
                    return;  // keep the previous cache; the next poll retries
                QStringList ids;
                for (const auto &v : arr(value))
                    ids << str(v.toObject(), "id");
                noBacklinkIds[typeId] = ids;
                noBacklinkFetched.insert(typeId);
                updateTypeDashboard(typeId);
                // Re-apply only the id set over the live spec, so a search or
                // sort the user just typed in the tab survives the fetch.
                if (route == "gallery" && typeFilter == typeId && typeSection == "no_backlinks" && activeViews) {
                    auto spec = activeViews->viewSpec();
                    spec["ids"] = idArray(ids);
                    activeViews->setScope(typeId, spec);
                }
            });
}

int MainWindow::dashboardSectionCount(const QString &typeId, const QString &section) const {
    if (section == "recent")
        return recentTypeIds(typeId).size();
    if (section == "untagged")
        return untaggedTypeIds(typeId).size();
    if (section == "no_backlinks")
        return noBacklinkIds.value(typeId).size();
    if (section == "collections")
        return collectionsForType(typeId).size();
    if (section == "templates")
        return templatesForType(typeId).size();
    if (section.startsWith("query:")) {
        const auto query = objectFor(section.mid(6));
        const auto spec = query.value("properties").toObject().value("view").toObject();
        return LumenQuery::evaluate(objects, types, spec).size();
    }
    return LumenQuery::evaluate(objects, types, QJsonObject{{"type_id", typeId}}).size();
}

QWidget *MainWindow::buildTypeDashboard(const QString &typeId) {
    auto *bar = new QWidget;
    bar->setObjectName("typeDashboardTabs");
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    fillTypeDashboard(bar, row, typeId);
    return bar;
}

void MainWindow::fillTypeDashboard(QWidget *bar, QHBoxLayout *row, const QString &typeId) {
    // updateTypeDashboard() clears the row but not the bar's QObject children;
    // drop the previous exclusive group so rebuilds do not accumulate them.
    for (auto *old : bar->findChildren<QButtonGroup *>())
        old->deleteLater();
    auto *group = new QButtonGroup(bar);
    group->setExclusive(true);
    const QString labelSeparator = "  ·  ";
    auto makeTab = [bar, row, group, &labelSeparator](const QString &key, const QString &label,
                                                     const QString &text) {
        auto *tab = new QToolButton(bar);
        tab->setObjectName("dashboardTab");
        tab->setProperty("sectionKey", key);
        tab->setText(text);
        tab->setToolTip(label);
        tab->setAccessibleName(label + " section");
        tab->setCheckable(true);
        tab->setToolButtonStyle(Qt::ToolButtonTextOnly);
        tab->setCursor(Qt::PointingHandCursor);
        group->addButton(tab);
        row->addWidget(tab);
        return tab;
    };
    auto countText = [&labelSeparator](const QString &label, int count) {
        return count < 0 ? label : label + labelSeparator + QString::number(count);
    };
    // "All" is the unfiltered type view; it is always present and not removable.
    auto *allTab = makeTab("all", "All", countText("All", dashboardSectionCount(typeId, "all")));
    connect(allTab, &QToolButton::clicked, this, [this, typeId] { showTypeSection(typeId, "all"); });
    for (const auto &key : visibleTypeSections(typeId)) {
        // The no-backlinks count arrives from the server; until the first
        // response the badge stays empty rather than showing a wrong 0.
        const bool pending = key == "no_backlinks" && !noBacklinkFetched.contains(typeId);
        auto *tab = makeTab(key, dashboardSectionLabel(key),
                            countText(dashboardSectionLabel(key),
                                      pending ? -1 : dashboardSectionCount(typeId, key)));
        connect(tab, &QToolButton::clicked, this, [this, typeId, key] { showTypeSection(typeId, key); });
        tab->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tab, &QWidget::customContextMenuRequested, tab, [this, tab, typeId, key](const QPoint &pos) {
            QMenu menu(this);
            menu.addAction(QString("Remove \"%1\" from this dashboard").arg(dashboardSectionLabel(key)),
                           [this, typeId, key] { toggleTypeSection(typeId, key, false); });
            menu.exec(tab->mapToGlobal(pos));
        });
    }
    // D5: the "Templates" section follows the same tab machinery, but its
    // default visibility is data-driven — only types with templates show it
    // until the user picks a state in the "+" menu. The count comes from the
    // flagged mirror, so it stays empty until the first listing lands.
    if (templatesSectionVisible(typeId)) {
        const bool pending = !templatesFetched.contains(typeId);
        auto *tab = makeTab("templates", "Templates",
                            countText("Templates", pending ? -1 : dashboardSectionCount(typeId, "templates")));
        connect(tab, &QToolButton::clicked, this, [this, typeId] { showTypeSection(typeId, "templates"); });
        tab->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tab, &QWidget::customContextMenuRequested, tab, [this, tab, typeId](const QPoint &pos) {
            QMenu menu(this);
            menu.addAction("Remove \"Templates\" from this dashboard",
                           [this, typeId] { toggleTypeSection(typeId, "templates", false); });
            menu.exec(tab->mapToGlobal(pos));
        });
    }
    for (auto v : queriesForType(typeId)) {
        const auto query = v.toObject();
        const QString key = "query:" + str(query, "id");
        auto *tab = makeTab(key, str(query, "title"),
                            countText(str(query, "title"), dashboardSectionCount(typeId, key)));
        connect(tab, &QToolButton::clicked, this, [this, typeId, key] { showTypeSection(typeId, key); });
    }
    auto *add = new QToolButton(bar);
    add->setObjectName("dashboardTabAdd");
    add->setIcon(LumenIcons::icon("plus"));
    add->setToolButtonStyle(Qt::ToolButtonIconOnly);
    add->setToolTip("Add or remove sections");
    add->setAccessibleName("Add or remove sections");
    add->setCursor(Qt::PointingHandCursor);
    auto *picker = new QMenu(add);
    const QString typeName = str(typeFor(typeId), "name");
    for (const auto &key : dashboardSectionKeys()) {
        auto *action = picker->addAction(dashboardSectionLabel(key));
        action->setCheckable(true);
        const bool checked = key == "templates" ? templatesSectionVisible(typeId)
                                                 : visibleTypeSections(typeId).contains(key);
        action->setChecked(checked);
        action->setToolTip(QString("Show the \"%1\" section on %2 dashboards").arg(dashboardSectionLabel(key)).arg(typeName));
        connect(action, &QAction::triggered, this, [this, typeId, key, action] {
            toggleTypeSection(typeId, key, action->isChecked());
        });
    }
    add->setMenu(picker);
    add->setPopupMode(QToolButton::InstantPopup);
    row->addWidget(add);
    row->addStretch(1);
    for (auto *tab : group->buttons())
        tab->setChecked(tab->property("sectionKey").toString() == typeSection);
}

void MainWindow::updateTypeDashboard(const QString &typeId) {
    QWidget *bar = nullptr;
    for (auto *candidate : findChildren<QWidget *>("typeDashboardTabs"))
        if (candidate->isVisible())
            bar = candidate;
    if (!bar)
        return;
    auto *row = qobject_cast<QHBoxLayout *>(bar->layout());
    if (!row)
        return;
    clearLayout(row);
    fillTypeDashboard(bar, row, typeId);
}

// Poll-time refresh: counts always; the active section's id set is re-derived
// from the fresh cache without touching the user's live view tweaks (search,
// sort, presentation), which live in the view's own spec.
void MainWindow::refreshTypeDashboard(const QString &typeId) {
    requestNoBacklinkIds(typeId);
    requestTemplates(typeId);
    updateTypeDashboard(typeId);
    if (!activeViews || typeSection == "all" || typeSection.startsWith("query:"))
        return;
    auto spec = activeViews->viewSpec();
    const bool collectionsTab = typeSection == "collections";
    const bool templatesTab = typeSection == "templates";
    QStringList ids;
    if (collectionsTab) {
        for (auto v : collectionsForType(typeId)) ids << str(v.toObject(), "id");
    } else if (typeSection == "recent") {
        ids = recentTypeIds(typeId);
    } else if (typeSection == "untagged") {
        ids = untaggedTypeIds(typeId);
    } else if (typeSection == "no_backlinks") {
        ids = noBacklinkIds.value(typeId);
    } else if (templatesTab) {
        for (auto v : templatesForType(typeId)) ids << str(v.toObject(), "id");
    }
    spec["ids"] = idArray(ids);
    // The Templates tab renders flagged objects that the general cache
    // excludes, so its data array is the merged mirror.
    if (templatesTab)
        activeViews->setData(objectsWithTemplates(), types);
    activeViews->setScope(collectionsTab ? "collection" : typeId, spec);
}

// Configures the shared view for one dashboard section. `baseSpec` carries a
// restored history view (or is empty, in which case the section's remembered
// spec — or the type's, for "all" — is used).
void MainWindow::applyTypeSection(LumenViews *views, const QString &typeId, const QString &section,
                                  QJsonObject baseSpec) {
    if (!views)
        return;
    configureViews(views);
    if (section == "all") {
        QJsonObject spec = baseSpec.isEmpty() ? collectionSpecs.value(typeId) : baseSpec;
        views->setData(objects, types);
        views->setScope(typeId, spec);
        const bool overview = spec.contains("collection_mode")
                                  ? spec.value("collection_mode").toString() == "overview"
                                  : true;
        views->setCollectionMode(overview);
        return;
    }
    if (section.startsWith("query:")) {
        // The saved query is the source of truth and is read fresh, exactly
        // like the standalone query page; in-tab tweaks are session-only.
        const auto query = objectFor(section.mid(6));
        auto spec = query.value("properties").toObject().value("view").toObject();
        spec.remove("collection_mode");
        views->setData(objects, types);
        views->setScope(typeId, spec);
        views->clearCollectionMode();
        return;
    }
    const QString key = "dashboard:" + typeId + ":" + section;
    QJsonObject spec = baseSpec.isEmpty() ? collectionSpecs.value(key) : baseSpec;
    spec.remove("collection_mode");
    const QString typeName = str(typeFor(typeId), "name");
    if (section == "collections") {
        QStringList ids;
        for (auto v : collectionsForType(typeId)) ids << str(v.toObject(), "id");
        spec["type_id"] = "collection";
        spec["ids"] = idArray(ids);
        if (!spec.contains("presentation"))
            spec["presentation"] = "list";
        if (!spec.contains("visible_properties"))
            spec["visible_properties"] = QJsonArray{"title", "type_id"};
        spec["empty_title"] = "No collections yet";
        spec["empty_description"] =
            QString("Curated, ordered lists of %1 objects appear here.").arg(typeName);
        views->setData(objects, types);
        views->setScope("collection", spec);
        views->clearCollectionMode();
        // The cards are collection objects: open the D2 collection page, let
        // "New collection" create one of this type, and offer the D2 delete
        // confirmation from the card menu.
        views->onOpen = [this](const QString &id) { showCollectionPage(id); };
        views->onCreate = [this, typeId](const QString &) { newCollectionDialog(typeId); };
        views->onImport = nullptr;
        views->onObjectMenu = [this](const QString &id, const QPoint &globalPos) {
            const auto target = objectFor(id);
            QMenu menu(this);
            menu.addAction("Open collection", [this, id] { showCollectionPage(id); });
            menu.addAction(LumenIcons::icon("x"), "Delete collection", [this, id, target] {
                QMessageBox box(this);
                box.setWindowTitle("Delete collection");
                box.setText(QString("Delete \"%1\"? The objects themselves are kept.").arg(str(target, "title")));
                box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                box.setDefaultButton(QMessageBox::No);
                if (box.exec() == QMessageBox::Yes)
                    deleteCollection(id);
            });
            menu.exec(globalPos);
        };
        return;
    }
    // D5 "Templates" section: the flagged objects of the type, listed with
    // the same view machinery. The data array is the merged mirror (the
    // general cache excludes templates), and the row menu offers the
    // template-specific actions ("New from this template", "Unsave").
    if (section == "templates") {
        QStringList ids;
        for (auto v : templatesForType(typeId)) ids << str(v.toObject(), "id");
        spec["ids"] = idArray(ids);
        if (!spec.contains("presentation"))
            spec["presentation"] = "list";
        spec["empty_title"] = "No templates yet";
        spec["empty_description"] = QString(
            "Open any %1 object and use \"Save as template\" to turn it into a reusable starting point.")
            .arg(typeName);
        if (!templatesFetched.contains(typeId))
            requestTemplates(typeId);
        views->setData(objectsWithTemplates(), types);
        views->setScope(typeId, spec);
        views->clearCollectionMode();
        views->onCreate = [this, typeId](const QString &) { newObject(typeId); };
        views->onImport = nullptr;
        views->onObjectMenu = [this](const QString &id, const QPoint &globalPos) {
            showTemplateMenu(id, globalPos);
        };
        return;
    }
    QStringList ids;
    if (section == "recent") {
        ids = recentTypeIds(typeId);
        spec["order"] = "manual";  // recency order, newest first
        spec["empty_title"] = "Nothing opened recently";
        spec["empty_description"] = QString("%1 objects you open will appear here.").arg(typeName);
    } else {
        spec.remove("order");  // the normal listing order (updated_at desc)
        if (section == "untagged") {
            ids = untaggedTypeIds(typeId);
            spec["empty_title"] = QString("Every %1 has tags").arg(typeName);
            spec["empty_description"] =
                QString("%1 objects without any tag would appear here.").arg(typeName);
        } else if (section == "no_backlinks") {
            ids = noBacklinkIds.value(typeId);
            if (!noBacklinkFetched.contains(typeId))
                requestNoBacklinkIds(typeId);
            spec["empty_title"] = QString("Every %1 has backlinks").arg(typeName);
            spec["empty_description"] =
                QString("%1 objects that no other object links to would appear here.").arg(typeName);
        }
    }
    spec["ids"] = idArray(ids);
    views->setData(objects, types);
    views->setScope(typeId, spec);
    views->clearCollectionMode();
}

// Switches the open type page to another dashboard tab without rebuilding the
// page; the previous tab's live view tweaks are remembered per section.
void MainWindow::showTypeSection(const QString &typeId, const QString &section) {
    if (route != "gallery" || typeFilter != typeId || !activeViews || section == typeSection)
        return;
    rememberActiveViewSpec();
    typeSection = section;
    applyTypeSection(activeViews, typeId, section);
    recordNavigation();
}

void MainWindow::toggleTypeSection(const QString &typeId, const QString &section, bool visible) {
    if (typeId.isEmpty() || !dashboardSectionKeys().contains(section))
        return;
    QStringList sections = visibleTypeSections(typeId);
    // The templates section is not part of the default-visible list; always
    // include its current state so writing the hidden list never silently
    // changes it while the user toggles another section.
    if (templatesSectionVisible(typeId))
        sections << "templates";
    if (visible == sections.contains(section))
        return;
    if (visible)
        sections << section;
    else
        sections.removeAll(section);
    setVisibleTypeSections(typeId, sections);
    // Removing the active section falls back to the base type view.
    if (!visible && route == "gallery" && typeFilter == typeId && typeSection == section) {
        rememberActiveViewSpec();
        typeSection = "all";
        applyTypeSection(activeViews, typeId, "all");
    }
    updateTypeDashboard(typeId);
}
