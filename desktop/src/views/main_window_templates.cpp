#include "main_window.hpp"

#include <QSettings>
#include <QUrlQuery>

#include <algorithm>


namespace {
QJsonArray idArrayFromList(const QStringList &ids) {
    QJsonArray array;
    for (const auto &id : ids) array.append(id);
    return array;
}
}  // namespace

bool MainWindow::objectIsTemplate(const QJsonObject &object) {
    return object.value("properties").toObject().value("is_template").toBool();
}

QJsonObject MainWindow::templateById(const QString &id) const {
    for (const auto &v : templates)
        if (str(v.toObject(), "id") == id)
            return v.toObject();
    return {};
}

QJsonArray MainWindow::templatesForType(const QString &typeId) const {
    QList<QJsonObject> found;
    for (const auto &v : templates) {
        const auto t = v.toObject();
        if (str(t, "type_id") != typeId)
            continue;
        found << t;
    }
    // Deterministic display order for the tab, the pickers and the settings.
    std::sort(found.begin(), found.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString first = str(a, "title"), second = str(b, "title");
        return first == second ? str(a, "id") < str(b, "id")
                               : QString::localeAwareCompare(first, second) < 0;
    });
    QJsonArray result;
    for (const auto &t : found) result.append(t);
    return result;
}

// The data array handed to the Templates tab view: the normal listing cache
// plus the flagged mirror. The view's spec carries the template id set, so
// only flagged objects render; the merged array just makes them resolvable.
QJsonArray MainWindow::objectsWithTemplates() const {
    QSet<QString> seen;
    QJsonArray merged = objects;
    for (const auto &v : objects)
        seen.insert(str(v.toObject(), "id"));
    for (const auto &v : templates) {
        const auto t = v.toObject();
        if (!seen.contains(str(t, "id")))
            merged.append(t);
    }
    return merged;
}

// Fetches the type's templates the documented way: the listing with
// include_templates=true, filtered client-side to properties.is_template.
// This is the only GET /v1/objects call the desktop makes with the flag (the
// Templates tab, the create picker and the type-settings default picker all
// read the same cache); normal tabs and the workspace poll never set it.
void MainWindow::requestTemplates(const QString &typeId) {
    if (typeId.isEmpty() || templatesPending.contains(typeId))
        return;
    templatesPending.insert(typeId);
    QUrlQuery query;
    query.addQueryItem("type_id", typeId);
    query.addQueryItem("include_templates", "true");
    api.get("/v1/objects?" + query.toString(QUrl::FullyEncoded),
            [this, typeId](QJsonValue value, QString err) {
                templatesPending.remove(typeId);
                if (!err.isEmpty())
                    return;  // keep the previous mirror; the next poll retries
                QJsonArray fresh;
                for (const auto &v : arr(value)) {
                    const auto object = v.toObject();
                    if (objectIsTemplate(object) && str(object, "type_id") == typeId)
                        fresh.append(object);
                }
                // Replace the type's slice in the mirror, keeping other types.
                QJsonArray kept;
                for (const auto &v : templates)
                    if (str(v.toObject(), "type_id") != typeId)
                        kept.append(v);
                for (const auto &v : fresh)
                    kept.append(v);
                templates = kept;
                templatesFetched.insert(typeId);
                if (route == "gallery" && typeFilter == typeId) {
                    updateTypeDashboard(typeId);
                    if (typeSection == "templates" && activeViews) {
                        if (!templatesSectionVisible(typeId)) {
                            // The section is no longer offered — its last
                            // template went away and the user never asked for
                            // the tab — so the active section falls back to
                            // All. An on-demand empty tab (configured visible)
                            // keeps showing its scoped empty state.
                            typeSection = "all";
                            applyTypeSection(activeViews, typeId, "all");
                        } else {
                            // Re-apply only the id set over the live spec, so a
                            // search or sort the user just typed survives.
                            auto spec = activeViews->viewSpec();
                            QStringList ids;
                            for (const auto &v : fresh) ids << str(v.toObject(), "id");
                            spec["ids"] = idArrayFromList(ids);
                            activeViews->setScope(typeId, spec);
                        }
                    }
                }
                refreshTemplatePickers();
            });
}

// Repopulates every live template picker (the create dialog's "Start from
// template" combo and the type-settings "Default template" combo) after the
// mirror changes. The combos carry their purpose and type id as properties,
// which lets the async listing land inside exec()ed dialogs.
void MainWindow::refreshTemplatePickers() {
    for (auto *combo : findChildren<QComboBox *>()) {
        const QString purpose = combo->property("templatePicker").toString();
        const QString typeId = combo->property("templateTypeId").toString();
        if (purpose.isEmpty() || typeId.isEmpty())
            continue;
        fillTemplateCombo(combo, typeId, purpose, combo->currentData().toString());
    }
}

// Fills a picker combo with the "none" choice plus the type's templates.
// purpose "create": the row hides when the type has no templates, so the new
// object flow is unchanged for template-less types. purpose "default": the
// type-settings picker always shows ("None" + templates).
void MainWindow::fillTemplateCombo(QComboBox *combo, const QString &typeId, const QString &purpose,
                                   const QString &selectedId) {
    if (!combo)
        return;
    combo->setProperty("templatePicker", purpose);
    combo->setProperty("templateTypeId", typeId);
    const auto list = templatesForType(typeId);
    const auto typ = typeFor(typeId);
    const QString noneLabel = purpose == "create" ? "Blank" : "None";
    QSignalBlocker block(combo);
    combo->clear();
    combo->addItem(LumenIcons::icon("file-text"), noneLabel, "");
    for (const auto &v : list) {
        const auto t = v.toObject();
        combo->addItem(LumenIcons::typeIcon(typ, 20, dark), str(t, "title"), str(t, "id"));
    }
    const int index = combo->findData(selectedId);
    combo->setCurrentIndex(qMax(0, index));
    if (purpose == "create") {
        if (auto *row = combo->property("templateRow").value<QWidget *>())
            row->setVisible(!list.isEmpty());
    }
}

// "Save as template" / "Unsave template": PATCH is a full properties
// replacement (contracts/SYNC.md), so the whole editable map is sent with
// the reserved flag set (or removed — absent means false). The server also
// clears default_template_id references transactionally when a template
// stops being one; the client refreshes the mirror on the response.
void MainWindow::setObjectTemplateFlag(const QString &objectId, bool flagged) {
    auto source = objectFor(objectId);
    if (source.isEmpty())
        source = templateById(objectId);
    if (source.isEmpty()) {
        error("Could not save the template: object is not loaded.");
        return;
    }
    auto properties = source.value("properties").toObject();
    if (flagged)
        properties["is_template"] = true;
    else
        properties.remove("is_template");
    saveLabel->setText(flagged ? "Saving as template…" : "Unsaving template…");
    api.patch("/v1/objects/" + objectId, QJsonObject{{"properties", properties}, {"base_revision", source.value("revision")}},
              [this, objectId, flagged](QJsonValue v, QString err) {
                  if (!err.isEmpty()) {
                      error("Could not update the template flag: " + err);
                      return;
                  }
                  const auto updated = v.toObject();
                  if (updated.isEmpty())
                      return;
                  const QString id = str(updated, "id");
                  // The general cache and the flagged mirror swap sides: the
                  // next listing no longer returns a saved template, and an
                  // unsaved one rejoins it. rememberObject routes flagged
                  // objects to the mirror, so first drop the stale copy.
                  if (flagged) {
                      for (int i = 0; i < objects.size(); ++i)
                          if (str(objects[i].toObject(), "id") == id) {
                              objects.removeAt(i);
                              break;
                          }
                      rememberObject(updated);
                  } else {
                      for (int i = 0; i < templates.size(); ++i)
                          if (str(templates[i].toObject(), "id") == id) {
                              templates.removeAt(i);
                              break;
                          }
                      rememberObject(updated);
                  }
                  templatesFetched.remove(str(updated, "type_id"));
                  requestTemplates(str(updated, "type_id"));
                  if (str(current, "id") == id && !dirty && !saving) {
                      current = updated;
                      if (route == "object")
                          renderEditor(false);  // show/hide the Template chip
                  }
                  rebuildSidebar();
              });
}

// POST /v1/objects/from-template: the server deep-copies properties and
// blocks (fresh block ids) and resolves {date:...} variables. An empty title
// is deliberate: the server then uses the template's own resolved title.
// `date` follows the journal's convention when creating from a daily note.
void MainWindow::createFromTemplate(const QString &templateId, const QString &title, const QString &originId,
                                    const QString &originRoute, quint64 generation) {
    QJsonObject body{{"template_id", templateId}};
    if (!title.isEmpty())
        body["title"] = title;
    if (originRoute == "journal")
        body["date"] = day.toString(Qt::ISODate);
    saveLabel->setText("Creating from template…");
    api.requestEx("POST", "/v1/objects/from-template", body,
                  [this, originId, originRoute, generation](int status, QJsonValue v, QString err) {
                      if (status != 201 || !err.isEmpty()) {
                          error("Could not create from the template: " +
                                (err.isEmpty() ? QString("HTTP %1").arg(status) : err));
                          return;
                      }
                      createdObject(v.toObject(), originId, originRoute, generation);
                  });
}

bool MainWindow::journalTypeHasDefaultTemplate() const {
    return !typeFor("journal").value("default_template_id").toString().isEmpty();
}

// The Templates dashboard tab follows the D4 hidden-list persistence model
// with one data-driven default: it is visible when the type has templates,
// or whenever the user has configured sections at all (the "+" menu writes
// the hidden list, so an explicit choice always wins over the data).
bool MainWindow::templatesSectionConfigured(const QString &typeId) const {
    QSettings pref("Lumen", "Lumen");
    return pref.contains("typeDashboards/" + typeId + "/hidden");
}

bool MainWindow::templatesSectionVisible(const QString &typeId) const {
    if (!templatesSectionConfigured(typeId))
        return !templatesForType(typeId).isEmpty();
    QSettings pref("Lumen", "Lumen");
    return !pref.value("typeDashboards/" + typeId + "/hidden").toStringList().contains("templates");
}

// Row context menu inside the Templates tab: the flagged objects open like
// any object, spawn copies, or drop the flag.
QMenu *MainWindow::buildTemplateMenu(const QString &objectId) {
    auto *menu = new QMenu(this);
    menu->addAction("Open", [this, objectId] { openPreview(objectId); });
    menu->addAction("Open in new tab", [this, objectId] { newTab(objectId); });
    menu->addSeparator();
    menu->addAction(LumenIcons::icon("copy"), "New from this template", [this, objectId] {
        createFromTemplate(objectId, {}, str(current, "id"), route, navigationGeneration);
    });
    menu->addAction(LumenIcons::icon("x"), "Unsave template", [this, objectId] {
        setObjectTemplateFlag(objectId, false);
    });
    return menu;
}

void MainWindow::showTemplateMenu(const QString &objectId, const QPoint &globalPos) {
    auto *menu = buildTemplateMenu(objectId);
    menu->exec(globalPos);
    menu->deleteLater();
}
