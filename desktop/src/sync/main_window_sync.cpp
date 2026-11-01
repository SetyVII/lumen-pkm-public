#include "main_window.hpp"

QJsonObject MainWindow::typeFor(const QString &id) const {
        for (auto v : types)
            if (str(v.toObject(), "id") == id)
                return v.toObject();
        return {{"id", id}, {"name", id}, {"color", "#6b8d83"}, {"icon", "text-x-generic"}};
    }

void MainWindow::refresh() {
        if (!canLeave([this] {
                refresh();
            }))
            return;
        failure.clear();
        api.get("/v1/types", [this](QJsonValue v, QString e) {
            if (!e.isEmpty()) {
                error(e);
                connection->setText("Sin conexión");
                return;
            }
            types = arr(v, "types");
            api.get("/v1/objects", [this](QJsonValue ov, QString oe) {
                if (!oe.isEmpty()) {
                    error(oe);
                    return;
                }
                objects = arr(ov);
                knownObjectIds.clear();
                for (auto value : objects)
                    knownObjectIds.insert(str(value.toObject(), "id"));
                connection->setText("Servidor conectado · sesión privada");
                notice->hide();
                ready = true;
                failure.clear();
                rebuildSidebar();
                if (route == "journal")
                    showJournal();
                else if (route == "object")
                    openObject(str(current, "id"));
                else if (route == "timeline")
                    showTimeline(calendarMode == "week" ? 7 : calendarMode == "three" ? 3 : 31);
                else if (route == "query")
                    openObject(str(current, "id"));
                else if (route == "collection")
                    showCollectionPage(str(current, "id"));
                else if (route == "gallery")
                    showCollection(typeFilter);
                else if (route == "graph")
                    showGraph();
                else if (route == "tasks")
                    showTasks(activeTasks ? activeTasks->activeTab() : "inbox");
            });
        });
    }

void MainWindow::connectDialog() {
        if(!canLeave([this]{connectDialog();}))return;
        QDialog d(this);
        d.setWindowTitle("Conectar tu espacio");
        d.setMinimumWidth(440);
        auto *l = new QVBoxLayout(&d);
        l->addWidget(label("Tu conocimiento, en tu servidor", "dialogTitle"));
        l->addWidget(
            label("La clave de acceso solo vive en esta sesión. El cliente no guarda tus notas en el disco.",
                  "muted"));
        auto *url = new QLineEdit(api.base);
        auto *token = new QLineEdit;
        token->setEchoMode(QLineEdit::Password);
        token->setPlaceholderText("Clave de acceso al servidor");
        l->addWidget(label("Servidor"));
        l->addWidget(url);
        l->addWidget(label("Clave de acceso"));
        l->addWidget(token);
        auto *b = button("Conectar", "network-connect", "primary");
        l->addWidget(b);
        connect(b, &QPushButton::clicked, &d, &QDialog::accept);
        if (d.exec() == QDialog::Accepted) {
            api.base = url->text().trimmed();
            api.token = token->text().toUtf8();
            api.clearAssets();
            objects = {};
            knownObjectIds.clear();
            current = {};
            loadRecentIds();
            refresh();
        }
    }

void MainWindow::rememberRecent(const QString &id) {
        if (id.isEmpty() || str(objectFor(id), "type_id") == "journal")
            return;
        recentObjectIds.removeAll(id);
        recentObjectIds.prepend(id);
        while (recentObjectIds.size() > 80)
            recentObjectIds.removeLast();
        saveRecentIds();
        // Navigation history belongs to this private session, never QSettings.
        for (auto *widget : findChildren<QWidget *>()) {
            if (auto *view = dynamic_cast<LumenViews *>(widget))
                view->setRecentIds(recentObjectIds);
            if (auto *edit = dynamic_cast<RichEditor *>(widget))
                edit->setRecentIds(recentObjectIds);
        }
    }

// D4 "Recently opened" persistence. The session list stays in RAM (its longer
// 80-entry window feeds the overview sections); a snapshot of the newest ids
// is persisted so the dashboard "Recent" section survives a restart. Only
// object ids are stored, never note content, and the key is per workspace
// (server authority), so switching servers switches recents.
QString MainWindow::recentSettingsKey() const {
        const QUrl base(api.base);
        QString authority = base.authority();
        if (authority.isEmpty())
            authority = "local";
        return "recentObjects/" + authority;
    }

void MainWindow::loadRecentIds() {
        QSettings pref("Lumen", "Lumen");
        recentObjectIds = pref.value(recentSettingsKey()).toStringList();
        recentObjectIds.removeAll(QString());
        while (recentObjectIds.size() > 80)
            recentObjectIds.removeLast();
        for (auto *widget : findChildren<QWidget *>()) {
            if (auto *view = dynamic_cast<LumenViews *>(widget))
                view->setRecentIds(recentObjectIds);
            if (auto *edit = dynamic_cast<RichEditor *>(widget))
                edit->setRecentIds(recentObjectIds);
        }
    }

void MainWindow::saveRecentIds() const {
        QSettings pref("Lumen", "Lumen");
        pref.setValue(recentSettingsKey(), recentObjectIds.mid(0, 20));
    }

void MainWindow::rememberObject(const QJsonObject &object) {
        const auto id = str(object, "id");
        if (id.isEmpty())
            return;
        // D5: flagged objects never join the general listing cache — the
        // server keeps them out of GET /v1/objects and the client must not
        // fight it. They are mirrored separately (the Templates tab, the
        // pickers and direct opens read the mirror), and a stale mirror entry
        // is dropped when the object lost its flag.
        if (objectIsTemplate(object)) {
            bool found = false;
            for (int i = 0; i < templates.size(); ++i)
                if (str(templates[i].toObject(), "id") == id) {
                    templates[i] = object;
                    found = true;
                    break;
                }
            if (!found)
                templates.append(object);
            for (int i = 0; i < objects.size(); ++i)
                if (str(objects[i].toObject(), "id") == id) {
                    objects.removeAt(i);
                    break;
                }
            knownObjectIds.insert(id);
            if(!derivedRefreshPending){derivedRefreshPending=true;QTimer::singleShot(0,this,[this]{derivedRefreshPending=false;refreshDerivedViews();});}
            return;
        }
        for (int i = 0; i < templates.size(); ++i)
            if (str(templates[i].toObject(), "id") == id) {
                templates.removeAt(i);
                break;
            }
        bool found = false;
        for (int i = 0; i < objects.size(); ++i)
            if (str(objects[i].toObject(), "id") == id) {
                objects[i] = object;
                found = true;
                break;
            }
        if (!found)
            objects.append(object);
        knownObjectIds.insert(id);
        if(!derivedRefreshPending){derivedRefreshPending=true;QTimer::singleShot(0,this,[this]{derivedRefreshPending=false;refreshDerivedViews();});}
    }

void MainWindow::refreshDerivedViews() {
        for(auto *widget:findChildren<QWidget*>()){
            auto *view=dynamic_cast<LumenViews*>(widget);if(!view)continue;
            const auto kind=view->property("lumenRelated").toString();if(kind.isEmpty())continue;
            const auto target=view->property("lumenTarget").toString();QJsonArray ids;
            for(auto value:objects){auto object=value.toObject();bool include=kind=="created"?
                str(object,"type_id")!="journal"&&QDateTime::fromString(str(object,"created_at"),Qt::ISODateWithMs).toLocalTime().date().toString(Qt::ISODate)==target:allLinks(object).contains(target);
                if(include)ids.append(str(object,"id"));}
            auto spec=view->viewSpec();
            if(spec.value("ids").toArray()!=ids){spec["ids"]=ids;view->setScope("",spec);}
            view->setData(objects,types);
        }
    }

QJsonObject MainWindow::objectFor(const QString &id) const {
        for (const auto &v : objects)
            if (str(v.toObject(), "id") == id)
                return v.toObject();
        return {};
    }

QJsonArray MainWindow::allLinks(const QJsonObject &object) const {
        auto result = object.value("links").toArray();
        for (auto id : object.value("property_links").toArray())
            if (!result.contains(id))
                result.append(id);
        return result;
    }

void MainWindow::pollWorkspace() {
        if (!ready || polling || saving || api.token.isEmpty())
            return;
        bool pending = false;
        for (auto v : objects)
            if (str(v.toObject(), "sync_status") == "pending") {
                pending = true;
                break;
            }
        if (!pending && ++pollTick % 5 != 0)
            return;
        polling = true;
        api.get("/v1/objects", [this](QJsonValue value, QString err) {
            polling = false;
            if (!err.isEmpty()) {
                connection->setText("Sin conexión · cambios conservados");
                return;
            }
            auto incoming = arr(value);
            const bool workspaceChanged = incoming != objects;
            objects = incoming;
            knownObjectIds.clear();
            for (auto v : objects)
                knownObjectIds.insert(str(v.toObject(), "id"));
            const auto remote = objectFor(str(current, "id"));
            if (!remote.isEmpty()) {
                current["sync_status"] = remote.value("sync_status");
                current["sync_error"] = remote.value("sync_error");
                if (!dirty && !saving && !navigationPending &&
                    remote.value("revision").toInteger() >= current.value("revision").toInteger()) {
                    const bool bodyChanged = current.value("content") != remote.value("content") ||
                        current.value("properties") != remote.value("properties") ||
                        current.value("title") != remote.value("title") ||
                        current.value("links") != remote.value("links");
                    if (bodyChanged && editor && (route == "journal" || route == "object")) {
                        const auto cursor = editor->textCursor();
                        const bool editorFocused = editor->hasFocus();
                        rememberPosition();
                        current = remote;
                        renderEditor(route == "journal");
                        auto restored = editor->textCursor();
                        const int end = editor->document()->characterCount() - 1;
                        restored.setPosition(qMin(cursor.anchor(), end));
                        restored.setPosition(qMin(cursor.position(), end), QTextCursor::KeepAnchor);
                        editor->setTextCursor(restored);
                        if (editorFocused) editor->setFocus(Qt::OtherFocusReason);
                    } else if (!bodyChanged) {
                        current = remote;
                    }
                    updateStatusLabel();
                }
            } else if (current.isEmpty()) {
                updateStatusLabel();
            }
            if (!workspaceChanged)
                return;
            if (editor)
                editor->setReferenceData(objects, types);
            if (activeViews) {
                // D5: the Templates dashboard section renders flagged objects
                // that the listing excludes, so its data array carries the
                // merged mirror; every other section keeps the plain cache.
                activeViews->setData(route == "gallery" && typeSection == "templates"
                                         ? objectsWithTemplates()
                                         : objects,
                                     types);
                // Another client may have changed this collection's items:
                // re-read the curated order from the refreshed object cache.
                if (route == "collection" && str(current, "type_id") == "collection") {
                    const auto refreshed = objectFor(str(current, "id"));
                    applyCollectionScope(activeViews, refreshed);
                    for (auto *count : findChildren<QLabel *>("collectionCountLabel"))
                        if (count->isVisible())
                            count->setText(collectionCountText(refreshed));
                }
                // D4: the open type dashboard refreshes with the same poll —
                // counts, the server-filtered "No backlinks" id set, and the
                // active section's ids, without touching live view tweaks.
                if (route == "gallery" && !typeFilter.isEmpty())
                    refreshTypeDashboard(typeFilter);
            }
            if (activeTasks)
                activeTasks->setData(objects, types);
            if (miniCalendar)
                miniCalendar->setWorkspaceObjects(objects);
            if (calendarOverview)
                calendarOverview->setData(objects, types);
            connection->setText("Servidor conectado · sesión privada");
        });
    }

void MainWindow::ensureImageLoaded(RichEditor *edit, const QString &objectId) {
        if (!edit || objectId.isEmpty()) return;
        const QUrl resUrl("lumen://image/" + objectId);
        if (imageMemoryCache_.contains(objectId)) {
            const auto &px = imageMemoryCache_.value(objectId);
            edit->document()->addResource(QTextDocument::ImageResource, resUrl, px);
            edit->document()->markContentsDirty(0, edit->document()->characterCount());
            edit->viewport()->update();
            return;
        }
        auto loadWithAsset = [this, edit = QPointer<RichEditor>(edit), objectId, resUrl](const QString &assetId) {
            if (assetId.isEmpty()) return;
            api.bytes("/v1/assets/" + assetId + "/content", [this, edit, objectId, resUrl](QByteArray bytes, QString e) {
                if (!edit || !e.isEmpty() || bytes.isEmpty()) return;
                QPixmap px;
                if (px.loadFromData(bytes)) {
                    imageMemoryCache_[objectId] = px;
                    edit->document()->addResource(QTextDocument::ImageResource, resUrl, px);
                    edit->document()->markContentsDirty(0, edit->document()->characterCount());
                    edit->viewport()->update();
                }
            });
        };
        const auto localObj = objectFor(objectId);
        if (!localObj.isEmpty()) {
            const auto p = localObj.value("properties").toObject();
            QString assetId = p.value("image_asset_id").toString();
            if (assetId.isEmpty()) assetId = p.value("asset_id").toString();
            if (assetId.isEmpty()) {
                for (auto av : p.value("assets").toArray()) {
                    if (av.toObject().value("mime").toString().startsWith("image/")) {
                        assetId = str(av.toObject(), "id");
                        break;
                    }
                }
            }
            if (!assetId.isEmpty()) {
                loadWithAsset(assetId);
                return;
            }
        }
        api.get("/v1/objects/" + objectId, [this, edit, loadWithAsset](QJsonValue val, QString err) {
            if (!edit || !err.isEmpty()) return;
            const auto obj = val.toObject();
            const auto p = obj.value("properties").toObject();
            QString assetId = p.value("image_asset_id").toString();
            if (assetId.isEmpty()) assetId = p.value("asset_id").toString();
            if (assetId.isEmpty()) {
                for (auto av : p.value("assets").toArray()) {
                    if (av.toObject().value("mime").toString().startsWith("image/")) {
                        assetId = str(av.toObject(), "id");
                        break;
                    }
                }
            }
            loadWithAsset(assetId);
        });
    }

QJsonArray MainWindow::attachedAssetIds(const QJsonObject &obj) const {
        QJsonArray result;
        auto assets = obj.value("properties").toObject().value("assets").toArray();
        for (auto v : assets) {
            auto a = v.toObject();
            if (a.value("mime").toString().startsWith("image/"))
                continue;
            auto mid = a.value("media_object_id").toString();
            if (!mid.isEmpty() && !result.contains(mid))
                result.append(mid);
        }
        return result;
    }

QJsonArray MainWindow::documentReferences() const {
        QJsonArray result;
        if (!editor)
            return result;
        for (auto value : editor->documentReferenceIds())
            if (knownObjectIds.contains(value.toString()) && !result.contains(value))
                result.append(value);
        return result;
    }

void MainWindow::reconcileReferences() {
        auto refs = documentReferences();
        for (auto v : attachedAssetIds(current))
            if (!refs.contains(v))
                refs.append(v);
        if (refs == linked)
            return;
        linked = refs;
        renderLinkedObjects();
    }

void MainWindow::unlinkObject(const QString &id) {
        if (editor) {
            QTextCursor cursor(editor->document());
            while (!cursor.isNull() && !cursor.atEnd()) {
                cursor = editor->document()->find(QRegularExpression("\\S+"), cursor);
                if (!cursor.isNull()) {
                    auto format = cursor.charFormat();
                    QUrl url(format.anchorHref());
                    if (format.isAnchor() && url.scheme() == "lumen" && (url.path().mid(1) == id || url.path() == "/" + id))
                        cursor.removeSelectedText();
                }
            }
            for (auto block = editor->document()->begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    const auto fragment = it.fragment();
                    const auto format = fragment.charFormat();
                    const QUrl url(format.anchorHref());
                    if (format.isAnchor() && url.scheme() == "lumen" && (url.path().mid(1) == id || url.path() == "/" + id)) {
                        QTextCursor c(editor->document());
                        c.setPosition(fragment.position());
                        c.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                        c.removeSelectedText();
                    }
                }
            }
        }
        auto p = current.value("properties").toObject();
        auto assets = p.value("assets").toArray();
        QJsonArray newAssets;
        bool assetsChanged = false;
        for (auto v : assets) {
            auto a = v.toObject();
            if (a.value("media_object_id").toString() == id || a.value("id").toString() == id) {
                assetsChanged = true;
            } else {
                newAssets.append(v);
            }
        }
        if (assetsChanged) {
            p["assets"] = newAssets;
            current["properties"] = p;
        }
        if (p.value("image_asset_id").toString() == id) {
            p.remove("image_asset_id");
            current["properties"] = p;
            assetsChanged = true;
        }
        QJsonArray newLinked;
        for (auto v : linked)
            if (v.toString() != id)
                newLinked.append(v);
        if (newLinked != linked || assetsChanged) {
            linked = newLinked;
            renderLinkedObjects();
            markDirty();
            saveCurrent();
        }
    }

QString MainWindow::workspaceSyncState() const {
        for (const auto &v : objects) {
            const auto o = v.toObject();
            if (str(o, "sync_status") == "pending")
                return str(o, "sync_error").isEmpty() ? "Guardando…" : "Guardado · pendiente";
        }
        return "Guardado";
    }

void MainWindow::updateStatusLabel() {
        if (!current.isEmpty()) {
            saveLabel->setText(saveState(current));
        } else {
            saveLabel->setText(workspaceSyncState());
        }
    }

QString MainWindow::saveState(const QJsonObject &o) const {
        if (str(o, "sync_status") == "pending")
            return str(o, "sync_error").isEmpty() ? "Guardando…" : "Guardado · pendiente";
        return "Guardado";
    }

void MainWindow::markDirty() {
        if (loading)
            return;
        dirty = true;
        saveLabel->setText("Guardando…");
        saveTimer.start();
    }

void MainWindow::saveCurrent(std::function<void()> after) {
        if (saving) {
            if (after)
                pendingNavigation = after;
            return;
        }
        if (!dirty || current.isEmpty()) {
            if (after)
                after();
            return;
        }
        saveTimer.stop();
        const auto id = str(current, "id");
        if (editor) {
            reconcileReferences();
            const auto blocks=editor->structuredBlocks();
            current["content"] = editor->exportText();
            auto p = current.value("properties").toObject();
            p["rich_text"] = editor->toHtml();
            p["blocks"] = blocks;
            if (route == "journal") {
                p.remove("image_asset_id");
                auto assets = p.value("assets").toArray();
                QJsonArray cleanAssets;
                for (auto av : assets) {
                    if (!av.toObject().value("mime").toString().startsWith("image/"))
                        cleanAssets.append(av);
                }
                p["assets"] = cleanAssets;
            }
            current["properties"] = p;
        }
        if (title && route != "journal")
            current["title"] = title->text().trimmed();
        current["links"] = linked;
        auto sent = current;
        if (sent.contains("revision"))
            sent["base_revision"] = sent.value("revision");
        dirty = false;
        saving = true;
        saveLabel->setText("Guardando…");
        api.patchEx("/v1/objects/" + id, sent, [this, id, sent, after](int status, QJsonValue v, QString err) {
            saving = false;
            if (status == 409) {
                const auto remote = v.toObject().value("current").toObject();
                if (!remote.isEmpty() && str(current, "id") == id) {
                    // Keep the local editor content and move the base revision
                    // forward. The next explicit save is a deliberate decision,
                    // not a silent overwrite of an unknown remote state.
                    current["revision"] = remote.value("revision");
                }
                dirty = true;
                error("Conflicto: el objeto cambió en otra ventana. Se conserva tu versión local; revisa antes de guardar de nuevo.");
                pendingNavigation = {};
                return;
            }
            if (!err.isEmpty()) {
                dirty = true;
                error("No se guardó: " + err);
                pendingNavigation = {};
                return;
            }
            auto returned = v.toObject();
            if (returned.isEmpty())
                returned = sent;
            rememberObject(returned);
            if (str(current, "id") == id && !dirty) {
                current = returned;
                saveLabel->setText(saveState(current));
                notice->hide();
            }
            if (after)
                after();
            if (pendingNavigation) {
                auto next = std::move(pendingNavigation);
                pendingNavigation = {};
                next();
            } else if (dirty)
                saveTimer.start();
        });
    }
