#include "main_window.hpp"

void MainWindow::renderLinkedObjects() {
        if (!linkedSection)
            return;
        auto *l = qobject_cast<QVBoxLayout *>(linkedSection->layout());
        clearLayout(l);
        if (linked.isEmpty())
            return;
        l->addWidget(label("Objetos enlazados", "section"));
        auto *flow = new QGridLayout;
        int i = 0;
        for (auto id : linked) {
            auto o = objectFor(id.toString());
            if (o.isEmpty())
                continue;
            auto *b = button(str(o, "title"), str(typeFor(str(o, "type_id")), "icon"), "objectChip");
            b->setIcon(LumenIcons::typeIcon(typeFor(str(o, "type_id")), 24, dark));
            b->setIconSize(QSize(24, 24));
            b->setToolTip(str(typeFor(str(o, "type_id")), "name") + " · Clic para abrir · Ctrl+clic para nueva pestaña · Clic derecho para opciones");
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(b, &QPushButton::clicked, this, [this, id] {
                openPreview(id.toString());
            });
            connect(b, &QPushButton::customContextMenuRequested, this, [this, id, b](const QPoint &pos) {
                QMenu menu(this);
                menu.addAction("Abrir previsualización", [this, id] { openPreview(id.toString()); });
                menu.addAction("Abrir en pestaña nueva", [this, id] { openObject(id.toString()); });
                auto o = objectFor(id.toString());
                if (str(o, "type_id") == "image") {
                    menu.addSeparator();
                    menu.addAction(LumenIcons::icon("photo"), "Establecer como portada", [this, o] {
                        auto rProps = o.value("properties").toObject();
                        QString aId = rProps.value("image_asset_id").toString();
                        if (aId.isEmpty()) aId = rProps.value("asset_id").toString();
                        if (aId.isEmpty()) {
                            for (auto av : rProps.value("assets").toArray()) {
                                if (av.toObject().value("mime").toString().startsWith("image/")) {
                                    aId = str(av.toObject(), "id");
                                    break;
                                }
                            }
                        }
                        if (!aId.isEmpty()) {
                            setCoverAsset(aId);
                        }
                    });
                }
                menu.addSeparator();
                menu.addAction("Desenlazar de esta nota", [this, id] { unlinkObject(id.toString()); });
                menu.exec(b->mapToGlobal(pos));
            });
            flow->addWidget(b, i / 3, i % 3);
            ++i;
        }
        l->addLayout(flow);
    }

void MainWindow::renderJournalTasks(QVBoxLayout *layout, QDate targetDay) {
        auto *tasksSection = new QWidget;
        tasksSection->setObjectName("journalTasksSection");
        auto *vl = new QVBoxLayout(tasksSection);
        vl->setContentsMargins(0, 10, 0, 10);
        vl->setSpacing(8);

        const QString dateStr = targetDay.toString(Qt::ISODate);
        const bool isToday = (targetDay == QDate::currentDate());
        QList<QJsonObject> dayTasks, overdueTasks, doneTasks;

        for (const auto &v : objects) {
            const auto o = v.toObject();
            if (str(o, "type_id") != "task") continue;
            const auto p = o.value("properties").toObject();
            const QString st = p.value("status").toString().toLower();
            const bool isDone = (st == "done" || st == "completed");
            const QString dStr = p.value("due_date").toString();
            const QDate d = QDate::fromString(dStr, Qt::ISODate);

            if (isToday) {
                if (isDone && d == targetDay) doneTasks.append(o);
                else if (!isDone) {
                    if (d < targetDay && d.isValid()) overdueTasks.append(o);
                    else if (d == targetDay || (!d.isValid() && (st == "in progress" || st == "in_progress"))) dayTasks.append(o);
                }
            } else if (targetDay < QDate::currentDate()) {
                if (isDone && (d == targetDay || p.value("completed_at").toString().startsWith(dateStr))) {
                    doneTasks.append(o);
                } else if (!isDone && d == targetDay) {
                    overdueTasks.append(o);
                }
            } else {
                if (d == targetDay) {
                    if (isDone) doneTasks.append(o); else dayTasks.append(o);
                }
            }
        }

        auto *head = new QHBoxLayout;
        auto *iconLabel = new QLabel;
        iconLabel->setPixmap(LumenIcons::badge("circle-check", QColor("#e07a5f"), 24, dark));
        head->addWidget(iconLabel);
        auto *titleLabel = label(isToday ? "Tareas de hoy" : "Tareas del día", "subheading");
        head->addWidget(titleLabel);
        const int pendingCount = dayTasks.size() + overdueTasks.size();
        auto *countBadge = label(QString("%1 pendientes · %2 hechas").arg(pendingCount).arg(doneTasks.size()), "muted");
        head->addWidget(countBadge);
        head->addStretch();

        if (isToday && pendingCount > 0) {
            auto *postpone = button("Posponer a mañana →", "arrow-forward", "quiet");
            postpone->setToolTip("Mover todas las tareas pendientes de hoy a mañana");
            connect(postpone, &QPushButton::clicked, this, [this, dayTasks, overdueTasks] {
                const QDate tomorrow = QDate::currentDate().addDays(1);
                for (const auto &list : {dayTasks, overdueTasks}) {
                    for (const auto &t : list) {
                        const QString id = str(t, "id");
                        auto p = t.value("properties").toObject();
                        p["due_date"] = tomorrow.toString(Qt::ISODate);
                        api.patch("/v1/objects/" + id, QJsonObject{{"properties", p}, {"base_revision", t.value("revision")}}, [this](QJsonValue v, QString) {
                            if (!v.isNull()) rememberObject(v.toObject());
                        });
                    }
                }
                QTimer::singleShot(150, this, [this] { renderEditor(true); });
            });
            head->addWidget(postpone);
        }
        vl->addLayout(head);

        auto *inputRow = new QHBoxLayout;
        auto *quickAdd = new QLineEdit;
        quickAdd->setPlaceholderText(isToday ? "Añadir tarea para hoy… (Enter)" : "Añadir tarea para esta fecha… (Enter)");
        connect(quickAdd, &QLineEdit::returnPressed, this, [this, quickAdd, targetDay] {
            const QString text = quickAdd->text().trimmed();
            if (text.isEmpty()) return;
            auto parsed = LumenTasksParser::parse(text, targetDay);
            QJsonObject props{{"status", "todo"}, {"due_date", targetDay.toString(Qt::ISODate)}};
            if (parsed.priority > 0) props.insert("priority", parsed.priority);
            if (parsed.hasTime) props.insert("time", parsed.time.toString("HH:mm"));
            QJsonObject task{
                {"type_id", "task"},
                {"title", parsed.title.isEmpty() ? text : parsed.title},
                {"properties", props}
            };
            saveLabel->setText("Guardando tarea…");
            api.post("/v1/objects", task, [this, quickAdd](QJsonValue v, QString err) {
                if (err.isEmpty()) {
                    quickAdd->clear();
                    rememberObject(v.toObject());
                    renderEditor(true);
                    updateStatusLabel();
                } else {
                    error("No se pudo crear la tarea: " + err);
                }
            });
        });
        inputRow->addWidget(quickAdd);
        vl->addLayout(inputRow);

        if (!overdueTasks.isEmpty()) {
            auto *ovLabel = label(QString("Vencidas (%1)").arg(overdueTasks.size()), "eyebrow");
            ovLabel->setStyleSheet("color:#e5484d; font-weight:bold; margin-top:4px;");
            vl->addWidget(ovLabel);
            for (const auto &t : overdueTasks) renderTaskItemRow(vl, t);
        }

        for (const auto &t : dayTasks) renderTaskItemRow(vl, t);

        if (!doneTasks.isEmpty()) {
            auto *doneLabel = label(QString("Completadas (%1)").arg(doneTasks.size()), "muted");
            doneLabel->setStyleSheet("color:#8a8a92; font-weight:600; margin-top:6px;");
            vl->addWidget(doneLabel);
            for (const auto &t : doneTasks) renderTaskItemRow(vl, t);
        }

        if (dayTasks.isEmpty() && overdueTasks.isEmpty() && doneTasks.isEmpty()) {
            auto *empty = label("No hay tareas registradas para esta fecha.", "muted");
            empty->setStyleSheet("color:#8a8a92; font-style:italic; padding:4px 0;");
            vl->addWidget(empty);
        }

        layout->addWidget(tasksSection);
    }

void MainWindow::renderObjectAssociatedTasks(QVBoxLayout *layout, const QString &objectId) {
        QList<QJsonObject> associated;
        int doneCount = 0;
        for (const auto &v : objects) {
            const auto o = v.toObject();
            if (str(o, "type_id") == "task" && o.value("properties").toObject().value("context_id").toString() == objectId) {
                associated.append(o);
                const QString st = o.value("properties").toObject().value("status").toString().toLower();
                if (st == "done" || st == "completed") ++doneCount;
            }
        }
        if (associated.isEmpty()) return;

        auto *card = new QWidget;
        auto *vl = new QVBoxLayout(card);
        vl->setContentsMargins(0, 10, 0, 10);
        vl->setSpacing(6);

        auto *head = new QHBoxLayout;
        auto *ico = new QLabel;
        ico->setPixmap(LumenIcons::badge("circle-check", QColor("#e07a5f"), 22, dark));
        head->addWidget(ico);
        head->addWidget(label("Tareas asociadas", "subheading"));
        const int pct = associated.isEmpty() ? 0 : (doneCount * 100 / associated.size());
        head->addWidget(label(QString("%1 de %2 hechas (%3%)").arg(doneCount).arg(associated.size()).arg(pct), "muted"));
        head->addStretch();
        vl->addLayout(head);

        for (const auto &t : associated) renderTaskItemRow(vl, t);
        layout->addWidget(card);
    }

void MainWindow::renderTaskItemRow(QVBoxLayout *layout, const QJsonObject &t) {
        auto *row = new QWidget;
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(4, 2, 4, 2);
        rl->setSpacing(8);
        const QString id = str(t, "id");
        const auto p = t.value("properties").toObject();
        const QString st = p.value("status").toString().toLower();
        const bool isDone = (st == "done" || st == "completed");
        const int prio = p.value("priority").toInt();

        auto *check = new QToolButton(row);
        check->setAutoRaise(true);
        check->setIcon(isDone ? LumenIcons::icon("circle-check", QColor("#48c08a"), 18)
                              : LumenIcons::icon("circle", QColor("#8e8e93"), 18));
        connect(check, &QToolButton::clicked, this, [this, id, isDone] {
            saveLabel->setText("Guardando…");
            auto existing = objectFor(id);
            auto p = existing.value("properties").toObject();
            p["status"] = isDone ? "todo" : "done";
            api.patch("/v1/objects/" + id, QJsonObject{{"properties", p}, {"base_revision", existing.value("revision")}}, [this](QJsonValue v, QString err) {
                if (err.isEmpty()) {
                    rememberObject(v.toObject());
                    renderEditor(route == "journal");
                    updateStatusLabel();
                } else {
                    error("Error al actualizar la tarea: " + err);
                }
            });
        });
        rl->addWidget(check);

        auto *titleBtn = new QPushButton(str(t, "title"), row);
        titleBtn->setCursor(Qt::PointingHandCursor);
        titleBtn->setFlat(true);
        QFont f("Inter"); f.setPixelSize(13);
        if (isDone) f.setStrikeOut(true);
        titleBtn->setFont(f);
        titleBtn->setStyleSheet(isDone ? "color:#8e8e93; text-align:left; border:none;"
                                      : (dark ? "color:#e5e5e7; text-align:left; border:none;" : "color:#1c1c1e; text-align:left; border:none;"));
        connect(titleBtn, &QPushButton::clicked, this, [this, id] { openPreview(id); });
        rl->addWidget(titleBtn, 1);

        if (prio > 0) {
            auto *pr = label(QString("!").repeated(prio));
            pr->setStyleSheet(QString("color:%1; font-weight:bold; font-size:12px;")
                .arg(prio == 3 ? "#e5484d" : (prio == 2 ? "#f76b15" : "#3e63dd")));
            rl->addWidget(pr);
        }

        const QString ctxId = p.value("context_id").toString();
        if (!ctxId.isEmpty()) {
            auto ctxObj = objectFor(ctxId);
            if (!ctxObj.isEmpty()) {
                auto *ctxBtn = button(str(ctxObj, "title"), "link", "quiet");
                ctxBtn->setStyleSheet("font-size:11px; color:#8b5cf6; padding:1px 4px; border:none;");
                connect(ctxBtn, &QPushButton::clicked, this, [this, ctxId] { openPreview(ctxId); });
                rl->addWidget(ctxBtn);
            }
        }
        layout->addWidget(row);
    }

void MainWindow::configureViews(LumenViews *views) {
        views->setTheme(dark, reduceMotion);
        views->setRecentIds(recentObjectIds);
        views->onObjectMenu = [this](QString id, QPoint globalPos) {
            showObjectMenu(id, globalPos);
        };
        views->onImport = [this, guard = QPointer<LumenViews>(views)] {
            if (guard) importCollectionFiles(guard->viewSpec().value("type_id").toString());
        };
        views->onOpen = [this](QString id) {
            if (QApplication::keyboardModifiers() & Qt::ControlModifier)
                newTab(id);
            else
                openPreview(id);
        };
        views->onCreate = [this](QString tid) {
            newObject(tid);
        };
        views->onPatch = [this, guard = QPointer<LumenViews>(views)](QString id, QJsonObject patch) {
            patch["base_revision"] = objectFor(id).value("revision");
            api.patch("/v1/objects/" + id, patch, [this, guard](QJsonValue v, QString e) {
                if (!e.isEmpty()) {
                    error(e);
                    if (guard)
                        guard->setData(objects, types);
                    return;
                }
                rememberObject(v.toObject());
            });
        };
        views->onAssetRequest = [this](QString id, std::function<void(QByteArray, QString)> cb) {
            api.bytes("/v1/assets/" + id + "/content", std::move(cb));
        };
        views->onSaveView = [this](QString name, QJsonObject spec) {
            saveQuery(name, spec);
        };
    }

void MainWindow::saveQuery(QString name, QJsonObject spec) {
        QJsonObject props{{"view", spec}};
        const bool updating = route == "query" && name == str(current, "title");
        auto path = updating ? "/v1/objects/" + str(current, "id") : "/v1/objects";
        QJsonObject data{{"title", name},
                         {"type_id", "query"},
                         {"content", ""},
                         {"properties", props},
                         {"links", QJsonArray{}}};
        api.request(updating ? "PATCH" : "POST", path, data, [this](QJsonValue v, QString e) {
            if (!e.isEmpty()) {
                error(e);
                return;
            }
            rememberObject(v.toObject());
            rebuildSidebar();
            openObject(str(v.toObject(), "id"));
        });
    }

void MainWindow::showCollection(QString tid, QJsonObject spec, QString queryId, QString section) {
        if (!canLeave([this, tid, spec, queryId, section] {
                showCollection(tid, spec, queryId, section);
            }))
            return;
        rememberPosition();
        if (activeViews)
            rememberActiveViewSpec();
        route = queryId.isEmpty() ? "gallery" : "query";
        typeFilter = tid;
        typeSection = queryId.isEmpty() ? section : QString("all");
        current = queryId.isEmpty() ? QJsonObject{} : objectFor(queryId);
        resetBody();
        setInfoVisible(false);
        updateStatusLabel();
        auto *head = new QHBoxLayout;
        auto titleText = queryId.isEmpty()
                             ? (tid.isEmpty() ? QString("Todos los objetos") : str(typeFor(tid), "name"))
                             : str(current, "title");
        auto typ = typeFor(tid);
        if (!tid.isEmpty()) {
            auto *badge = new QLabel;
            badge->setPixmap(LumenIcons::badge(str(typ, "icon"), QColor(str(typ, "color")), 32, dark));
            head->addWidget(badge);
        }
        auto *heading = label(titleText, "collectionTitle");
        head->addWidget(heading);
        head->addStretch();
        auto *create = button("Nuevo", "plus", "collectionCreate");
        create->setProperty("primaryAction", true);
        connect(create, &QPushButton::clicked, this, [this, tid] { newObject(tid); });
        head->addWidget(create);
        body->addLayout(head);
        // Collections of this type render as chips under the title: the "All"
        // chip first, one chip per curated collection, then the "+" affordance.
        // The synthetic `collection` type has no item type of its own, so it
        // gets no chip row (collections are surfaced under their item type).
        if (queryId.isEmpty() && !tid.isEmpty() && tid != "collection") {
            body->addWidget(buildCollectionChips(tid));
            // D4: the dashboard section tabs ("All" + built-in sections + the
            // saved queries of this type) sit under the chip row, which stays
            // the type header. The server-filtered count warms up with the
            // page so the "No backlinks" badge is ready before the click.
            body->addWidget(buildTypeDashboard(tid));
            requestNoBacklinkIds(tid);
            // D5: warm the flagged-objects mirror with the page, so the
            // Templates tab (and its count badge) is ready before the click.
            requestTemplates(tid);
        }
        activeViews = new LumenViews;
        activeViews->setObjectName("workspaceViews");
        configureViews(activeViews);
        activeViews->setData(objects, types);
        if (queryId.isEmpty() && !tid.isEmpty() && tid != "collection") {
            applyTypeSection(activeViews, tid, typeSection, spec);
        } else {
            if (spec.isEmpty())
                spec = collectionSpecs.value(tid);
            activeViews->setScope(tid, spec);
            const bool overview = spec.contains("collection_mode")
                                      ? spec.value("collection_mode").toString() == "overview"
                                      : !tid.isEmpty() && queryId.isEmpty();
            activeViews->setCollectionMode(overview);
        }
        body->addWidget(activeViews, 1);
        inspect->addWidget(label("Vistas de tu conocimiento", "subheading"));
        inspect->addWidget(label(
            "Filtra, ordena y agrupa los mismos objetos. Guarda una consulta para volver a ella.", "muted"));
        inspect->addStretch();
        crumb->setText(titleText);
        rebuildSidebar();
        recordNavigation();
        // The type/query page is a fresh read-only view until the user edits a
        // property or saves a view; do not inherit a stale dirty flag.
        dirty = false;
    }

void MainWindow::showTasks(const QString &tab) {
        if (!canLeave([this, tab] { showTasks(tab); })) return;
        rememberPosition();
        route = "tasks";
        current = {};
        resetBody();
        setInfoVisible(false);
        updateStatusLabel();
        crumb->setText("Tareas");
        updateStatusLabel();

        auto *tasksView = new LumenTasks;
        activeTasks = tasksView;
        tasksView->setTheme(dark);
        tasksView->setData(objects, types);
        tasksView->setActiveTab(tab);

        tasksView->onCreateTask = [this](QJsonObject task) {
            saveLabel->setText("Guardando tarea…");
            api.post("/v1/objects", task, [this](QJsonValue v, QString err) {
                if (err.isEmpty()) {
                    auto created = v.toObject();
                    rememberObject(created);
                    rebuildSidebar();
                    if (activeTasks) {
                        activeTasks->setData(objects, types);
                    }
                    updateStatusLabel();
                } else {
                    error("No se pudo crear la tarea: " + err);
                }
            });
        };

        tasksView->onUpdateTask = [this](QString id, QJsonObject patch) {
            saveLabel->setText("Guardando…");
            auto existing = objectFor(id);
            auto p = existing.value("properties").toObject();
            for (auto it = patch.begin(); it != patch.end(); ++it)
                p.insert(it.key(), it.value());
            api.patch("/v1/objects/" + id, QJsonObject{{"properties", p}, {"base_revision", existing.value("revision")}}, [this](QJsonValue v, QString err) {
                if (err.isEmpty()) {
                    auto updated = v.toObject();
                    rememberObject(updated);
                    if (activeTasks) {
                        activeTasks->setData(objects, types);
                    }
                    updateStatusLabel();
                } else {
                    error("Error al actualizar la tarea: " + err);
                }
            });
        };

        tasksView->onOpenObject = [this](QString objectId) {
            openPreview(objectId);
        };

        tasksView->onDeleteTask = [this](QString taskId) {
            saveLabel->setText("Eliminando tarea…");
            api.request("DELETE", "/v1/objects/" + taskId, {}, [this, taskId](QJsonValue, QString err) {
                if (err.isEmpty()) {
                    for (int i = 0; i < objects.size(); ++i) {
                        if (str(objects[i].toObject(), "id") == taskId) {
                            objects.removeAt(i);
                            break;
                        }
                    }
                    rebuildSidebar();
                    if (activeTasks) {
                        activeTasks->setData(objects, types);
                    }
                    updateStatusLabel();
                } else {
                    error("Error al eliminar la tarea: " + err);
                }
            });
        };

        tasksView->onBulkReschedule = [this](QList<QString> taskIds, QDate newDate) {
            saveLabel->setText("Reprogramando tareas…");
            for (const auto &id : taskIds) {
                auto existing = objectFor(id);
                auto p = existing.value("properties").toObject();
                p["due_date"] = newDate.toString(Qt::ISODate);
                api.patch("/v1/objects/" + id, QJsonObject{{"properties", p}, {"base_revision", existing.value("revision")}}, [this](QJsonValue v, QString) {
                    if (!v.isNull()) {
                        rememberObject(v.toObject());
                        if (activeTasks) {
                            activeTasks->setData(objects, types);
                        }
                        updateStatusLabel();
                    }
                });
            }
        };

        body->addWidget(tasksView, 1);
        rebuildSidebar();
        recordNavigation();
    }

void MainWindow::showGraph() {
        if (!canLeave([this] {
                showGraph();
            }))
            return;
        rememberPosition();
        route = "graph";
        current = {};
        resetBody();
        setInfoVisible(false);
        updateStatusLabel();
        auto *head = new QHBoxLayout;
        head->addWidget(label("Grafo", "collectionTitle"));
        head->addStretch();
        auto *search = new QLineEdit;
        search->setObjectName("graphSearch");
        search->setPlaceholderText("Buscar en el grafo…");
        search->setMaximumWidth(260);
        head->addWidget(search);
        auto *filter = new QComboBox;
        filter->addItem("Todos los tipos", "");
        for (auto v : types)
            filter->addItem(str(v.toObject(), "name"), str(v.toObject(), "id"));
        head->addWidget(filter);
        body->addLayout(head);
        graphCanvas = new LumenGraph;
        graphCanvas->setObjectName("workspaceGraph");
        graphCanvas->setTheme(dark, reduceMotion);
        graphCanvas->setData(objects, types);
        graphCanvas->onOpen = [this](QString id) {
            openPreview(id);
        };
        body->addWidget(graphCanvas, 1);
        connect(search, &QLineEdit::textChanged, graphCanvas, [g = graphCanvas](QString s) {
            if (g)
                g->setSearch(s);
        });
        connect(filter, &QComboBox::currentTextChanged, graphCanvas, [g = graphCanvas, filter] {
            if (g)
                g->setTypeFilter(filter->currentData().toString());
        });
        inspect->addWidget(label("Objetos y relaciones", "subheading"));
        inspect->addWidget(label("Arrastra para mover, usa la rueda para acercarte y abre un objeto con "
                                 "Enter. Las relaciones de propiedades conservan sus nombres.",
                                 "muted"));
        inspect->addStretch();
        crumb->setText("Grafo");
        rebuildSidebar();
        recordNavigation();
    }

void MainWindow::showTimeline(int days) {
        if (!canLeave([this, days] {
                showTimeline(days);
            }))
            return;
        rememberPosition();
        route = "timeline";
        current = {};
        calendarMode = days == 7 ? "week" : days == 3 ? "three" : "month";
        resetBody();
        setInfoVisible(false);
        updateStatusLabel();
        body->addWidget(calendarToolbar(calendarMode));
        calendarOverview = new LumenCalendarOverview;
        calendarOverview->setObjectName("calendarOverview");
        calendarOverview->setTheme(dark, reduceMotion);
        calendarOverview->setData(objects, types);
        calendarOverview->setDate(day);
        calendarOverview->setMode(calendarMode);
        calendarOverview->onOpenDate = [this](QDate date) {
            navigateJournal(date);
        };
        calendarOverview->onOpenObject = [this](QString id) {
            openPreview(id);
        };
        body->addWidget(calendarOverview, 1);
        crumb->setText("Calendario");
        rebuildSidebar();
        recordNavigation();
    }
