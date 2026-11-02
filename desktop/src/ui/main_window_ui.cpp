#include "lumen_document.hpp"
#include "main_window.hpp"
#include "lumen_aliases.hpp"

void MainWindow::rebuildSidebar() {
        clearLayout(nav);
        auto *quick = new QHBoxLayout;
        auto *create = button("Nuevo", "plus", "sidebarNew");
        connect(create, &QPushButton::clicked, this, [this] {
            newObject(typeFilter);
        });
        quick->addWidget(create, 1);
        auto *search = button("", "search", "quiet");
        search->setFixedSize(32, 32);
        search->setToolTip("Buscar · Ctrl+P");
        connect(search, &QPushButton::clicked, this, [this] {
            commandPalette();
        });
        quick->addWidget(search);
        nav->addLayout(quick);
        nav->addSpacing(12);
        auto add = [this](QString text, QString ico, bool active, std::function<void()> action) {
            auto *b = button(text, ico, active ? "activeNav" : "nav");
            b->setMinimumHeight(30);
            b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            b->setToolTip(text);
            b->setIconSize(QSize(17, 17));
            connect(b, &QPushButton::clicked, this, action);
            nav->addWidget(b);
        };
        add("Calendario", "calendar", route == "journal" || route == "timeline", [this] {
            navigateJournal(day);
        });
        add("Tareas", "circle-check", route == "tasks", [this] {
            showTasks();
        });
        add("Todos los objetos", "layout-grid", route == "gallery" && typeFilter.isEmpty(), [this] {
            showCollection("");
        });
        add("Grafo", "network", route == "graph", [this] {
            showGraph();
        });
        nav->addSpacing(20);
        nav->addWidget(label("Tipos de objeto", "section"));
        for (auto v : types) {
            auto t = v.toObject();
            auto id = str(t, "id");
            // Collections are curated lists inside a type, not a user-created
            // type page: they render under their item type instead.
            if (id == "collection")
                continue;
            int count = 0;
            for (auto o : objects)
                if (str(o.toObject(), "type_id") == id)
                    ++count;
            QFont navFont("Inter"); navFont.setPixelSize(13);
            const auto shortName = QFontMetrics(navFont).elidedText(str(t, "name"), Qt::ElideRight, 178);
            auto *b = button(shortName + "   " + QString::number(count), str(t, "icon"),
                             route == "gallery" && typeFilter == id ? "activeNav" : "nav");
            b->setMinimumHeight(29);
            b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            b->setToolTip(str(t, "name"));
            b->setIconSize(QSize(17, 17));
            b->setIconSize(QSize(24, 24));
            b->setIcon(LumenIcons::typeIcon(t, 24, dark));
            connect(b, &QPushButton::clicked, this, [this, id] {
                showCollection(id);
            });
            b->setContextMenuPolicy(Qt::CustomContextMenu);
            // The type's curated collections render as indented rows below the
            // type, following the saved-queries pattern; the type's context
            // menu collapses them.
            const auto typeCollections = collectionsForType(id);
            const bool hidden = hiddenTypeCollections.contains(id);
            connect(b, &QPushButton::customContextMenuRequested, this, [this, id, b, typeCollections, hidden](QPoint point) {
                QMenu m;
                m.addAction("Editar tipo", [this, id] {
                    propertyDialog(id);
                });
                if (!typeCollections.isEmpty())
                    m.addAction(hidden ? "Show collections" : "Hide collections", [this, id, hidden] {
                        if (hidden)
                            hiddenTypeCollections.remove(id);
                        else
                            hiddenTypeCollections.insert(id);
                        rebuildSidebar();
                    });
                m.exec(b->mapToGlobal(point));
            });
            nav->addWidget(b);
            if (!hidden)
                for (auto cv : typeCollections) {
                    const auto c = cv.toObject();
                    const QString cid = str(c, "id");
                    const int items = c.value("properties").toObject().value("items").toArray().size();
                    auto *row = button(str(c, "title") + "  ·  " + QString::number(items),
                                       str(typeFor("collection"), "icon"),
                                       route == "collection" && str(current, "id") == cid ? "activeNav" : "nav");
                    row->setObjectName("sidebarCollectionRow");
                    row->setStyleSheet("padding:5px 8px 5px 26px;");
                    row->setMinimumHeight(29);
                    row->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
                    row->setToolTip(str(c, "title") + " · " + collectionCountText(c));
                    row->setIconSize(QSize(17, 17));
                    connect(row, &QPushButton::clicked, this, [this, cid] {
                        showCollectionPage(cid);
                    });
                    nav->addWidget(row);
                }
        }
        auto *addType = button("Añadir un tipo", "plus", "quiet");
        connect(addType, &QPushButton::clicked, this, [this] {
            newType();
        });
        nav->addWidget(addType);
        bool section = false;
        for (auto v : objects) {
            auto o = v.toObject();
            if (str(o, "type_id") != "query")
                continue;
            if (!section) {
                nav->addSpacing(16);
                nav->addWidget(label("Consultas guardadas", "section"));
                section = true;
            }
            add(str(o, "title"), "filter", route == "query" && str(current, "id") == str(o, "id"), [this, o] {
                openObject(str(o, "id"));
            });
        }
        nav->addStretch(1);
        auto *capture = button("Guardar un enlace", "link", "nav");
        connect(capture, &QPushButton::clicked, this, [this] {
            captureUrl();
        });
        nav->addWidget(capture);
        auto *settings = button("Apariencia y atajos", "settings", "nav");
        connect(settings, &QPushButton::clicked, this, [this] {
            preferences();
        });
        nav->addWidget(settings);
    }

void MainWindow::wireEditor(RichEditor *edit, const QString &origin, bool preview) {
        edit->setDocumentObjectId(origin);
        edit->setAppearance(dark, reduceMotion);
        edit->setReferenceData(objects, types);
        edit->setRecentIds(recentObjectIds);
        // Find-in-page session: restore the last query/options on the new
        // object and keep the window's copy in sync while the user types.
        // Preview editors get their own transient session: they must not
        // overwrite the main page's query.
        edit->setFindSession(findQuery_, findMatchCase_, findWholeWord_, findReplaceVisible_);
        if (!preview) {
            edit->findStateChanged = [this, guard = QPointer<RichEditor>(edit)] {
                if (!guard || guard.data() != editor) return;
                findQuery_ = guard->findQueryText();
                findMatchCase_ = guard->findMatchCase();
                findWholeWord_ = guard->findWholeWord();
                findReplaceVisible_ = guard->findReplaceVisible();
            };
        }
        // The editor is as tall as the document inside a QScrollArea, so its
        // own ensureCursorVisible() cannot scroll the page. Walk up to the
        // editor's own scroll area (main page or preview dialog) and keep the
        // match inside the visible pane.
        edit->revealFindMatch = [guard = QPointer<RichEditor>(edit)](const QRect &rect) {
            if (!guard) return;
            QScrollArea *scroll = nullptr;
            for (QWidget *widget = guard->parentWidget(); widget; widget = widget->parentWidget()) {
                if (auto *candidate = qobject_cast<QScrollArea *>(widget)) {
                    scroll = candidate;
                    break;
                }
            }
            if (!scroll) return;
            QWidget *viewport = scroll->viewport();
            const QPoint top = guard->viewport()->mapTo(viewport, rect.topLeft());
            const QPoint bottom = guard->viewport()->mapTo(viewport, rect.bottomLeft());
            const int margin = qMax(72, guard->findBarHeight() + 24);
            int value = scroll->verticalScrollBar()->value();
            if (top.y() < margin) value += top.y() - margin;
            else if (bottom.y() > viewport->height() - margin)
                value += bottom.y() - (viewport->height() - margin);
            scroll->verticalScrollBar()->setValue(
                qBound(0, value, scroll->verticalScrollBar()->maximum()));
        };
        edit->resourceProvider = [this, edit = QPointer<RichEditor>(edit)](int type, const QUrl &name) -> QVariant {
            if (type == QTextDocument::ImageResource) {
                if (name.scheme() == "lumen" && name.host() == "asset") {
                    const auto assetId = name.path().mid(1); const auto key = "asset/" + assetId;
                    if (imageMemoryCache_.contains(key)) return imageMemoryCache_.value(key);
                    if (edit) ensureAssetImageLoaded(edit, assetId);
                }
                if (name.scheme() == "lumen" && name.host() == "image") {
                    QString objId = name.path();
                    while (objId.startsWith('/')) objId.remove(0, 1);
                    if (!objId.isEmpty()) {
                        if (imageMemoryCache_.contains(objId)) {
                            return imageMemoryCache_.value(objId);
                        }
                        if (edit) ensureImageLoaded(edit, objId);
                    }
                }
            }
            return {};
        };
        edit->openReference = [this](QString id) {
            openPreview(id);
        };
        edit->slashRequested = [guard = QPointer<RichEditor>(edit)] {
            if (guard)
                guard->showBlockMenu();
        };
        edit->convertBlockRequested = [this, origin, guard = QPointer<RichEditor>(edit)](
            const QString &type, const QString &text, RichEditor::ReferenceCompletion done) {
            QJsonObject props;
            if (type == "task") props["status"] = "todo";
            api.post("/v1/objects", {{"type_id", type}, {"title", text.isEmpty() ? "Sin título" : text}, {"document", LumenDocument::empty()},
                                     {"properties", props}, {"links", QJsonArray{}}},
                     [this, origin, guard, done](QJsonValue v, QString err) {
                         if (!err.isEmpty()) {
                             error(err);
                             if (done) done({});
                             return;
                         }
                         auto o = v.toObject();
                         rememberObject(o);
                         rebuildSidebar();
                         if (done) done(o);
                     });
        };
        edit->blockResolveRequested = [this](QString blockID, QString owner, RichEditor::ReferenceCompletion done) {
            QUrlQuery query; if (!owner.isEmpty()) query.addQueryItem("object_id", owner);
            api.get("/v1/blocks/" + QString::fromLatin1(QUrl::toPercentEncoding(blockID)) + "?" + query.toString(QUrl::FullyEncoded),
                [this, done](QJsonValue response, QString errorMessage) { if (!errorMessage.isEmpty()) { error(errorMessage); done({}); } else done(response.toObject()); });
        };
        edit->blockSearchRequested = [this](QString query, std::function<void(QJsonArray)> done) {
            QUrlQuery params; params.addQueryItem("q", query); params.addQueryItem("limit", "50");
            api.get("/v1/blocks?" + params.toString(QUrl::FullyEncoded), [done](QJsonValue response, QString error) {
                done(error.isEmpty() ? response.toObject().value("hits").toArray() : QJsonArray());
            });
        };
        edit->referenceCreateRequested =
            [this, origin, guard = QPointer<RichEditor>(edit)](QString type, QString name,
                                                               RichEditor::ReferenceCompletion done) {
                if (type == "image" || type == "pdf") {
                    const auto path = QFileDialog::getOpenFileName(guard ? static_cast<QWidget *>(guard.data()) : static_cast<QWidget *>(this),
                        type == "image" ? "Añadir imagen" : "Añadir PDF", {},
                        type == "image" ? "Imágenes (*.png *.jpg *.jpeg *.webp *.gif)" : "PDF (*.pdf)");
                    if (path.isEmpty()) { done({}); return; }
                    api.upload("", path, [this, done](QJsonValue response, QString errorMessage) {
                        if (!errorMessage.isEmpty()) { error(errorMessage); done({}); return; }
                        const auto object = response.toObject().value("media_object").toObject();
                        if (object.isEmpty()) { error("El archivo no generó un objeto de medio válido."); done({}); return; }
                        rememberObject(object); rebuildSidebar(); done(object);
                    });
                    return;
                }
                Callback complete = [this, origin, guard, done](QJsonValue v, QString err) {
                    if (!err.isEmpty()) {
                        error(err);
                        done({});
                        return;
                    }
                    auto o = v.toObject();
                    rememberObject(o);
                    rebuildSidebar();
                    if (guard && guard->isVisible())
                        done(o);
                    else if (!origin.isEmpty())
                        api.patch("/v1/objects/" + origin, {{"add_links", QJsonArray{str(o, "id")}}},
                                  [this](QJsonValue value, QString e) {
                                      if (e.isEmpty()) rememberObject(value.toObject());
                                      else error(e);
                                  });
                };
                if (type == "journal") {
                    const auto date = QDate::fromString(name, Qt::ISODate);
                    if (!date.isValid()) { done({}); return; }
                    api.get("/v1/journal/" + date.toString(Qt::ISODate), std::move(complete));
                } else {
                    auto typ = typeFor(type);
                    auto *d = new GuardedDialog(this);
                    d->setObjectName("creationModal");
                    d->setAttribute(Qt::WA_DeleteOnClose);
                    d->setWindowModality(Qt::WindowModal);
                    d->resize(620, 390);
                    d->setStyleSheet(QString(
                        "#creationModal { background: %1; border: 1px solid %2; border-radius: 12px; }"
                        "QLineEdit { background: transparent; border: none; font-size: 24px; font-weight: 600; color: %3; padding: 4px 0; }"
                        "QLineEdit:focus { border: none; }"
                        "QTextEdit { background: transparent; border: none; font-size: 15px; color: %4; padding: 4px 0; line-height: 1.5; }"
                    ).arg(dark ? "#1b1b1f" : "#ffffff", dark ? "#323238" : "#e5e5e7", dark ? "#f4f4f5" : "#18181b", dark ? "#d4d4d8" : "#3f3f46"));

                    auto *outer = new QVBoxLayout(d);
                    outer->setContentsMargins(28, 22, 28, 22);
                    outer->setSpacing(12);

                    auto *topRow = new QHBoxLayout;
                    auto *typeBadge = new QPushButton(str(typ, "name"), d);
                    typeBadge->setObjectName("typeBadge");
                    typeBadge->setIcon(LumenIcons::typeIcon(typ, 20, dark));
                    typeBadge->setIconSize(QSize(20, 20));
                    typeBadge->setFocusPolicy(Qt::NoFocus);
                    const QString badgeColor = str(typ, "color").isEmpty() ? "#9584b3" : str(typ, "color");
                    typeBadge->setStyleSheet(QString(
                        "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 10px; font-size: 12px; font-weight: 600; text-align: left; }"
                    ).arg(dark ? "#262220" : "#fdf6e2", badgeColor, dark ? "#3d3224" : "#f3e1b0"));
                    topRow->addWidget(typeBadge);
                    topRow->addStretch();
                    outer->addLayout(topRow);

                    auto *titleEdit = new QLineEdit(d);
                    const QString initialTitle = (name == str(typ, "name")) ? QString() : name;
                    titleEdit->setText(initialTitle);
                    titleEdit->setPlaceholderText("Título");
                    titleEdit->setAccessibleName("Título del nuevo objeto");
                    outer->addWidget(titleEdit);

                    auto *contentEdit = new RichEditor(d);
                    wireEditor(contentEdit, {}, true);
                    contentEdit->setDocumentObjectId(QUuid::createUuid().toString(QUuid::WithoutBraces));
                    contentEdit->setPlaceholderText("Text");
                    contentEdit->setAccessibleName("Contenido del nuevo objeto");
                    outer->addWidget(contentEdit, 1);

                    auto *bottomRow = new QHBoxLayout;
                    bottomRow->addStretch();

                    auto *cancelBtn = new QPushButton("Cancelar", d);
                    cancelBtn->setStyleSheet(QString("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 6px 14px; font-size: 13px; font-weight: 500; } QPushButton:hover { background: %4; }")
                        .arg(dark ? "#27272a" : "#f4f4f5", dark ? "#a1a1aa" : "#71717a", dark ? "#3f3f46" : "#e4e4e7", dark ? "#3f3f46" : "#e4e4e7"));

                    auto *confirmBtn = new QPushButton("Crear " + str(typ, "name"), d);
                    confirmBtn->setDefault(true);
                    confirmBtn->setStyleSheet(QString("QPushButton { background: %1; color: #ffffff; border: none; border-radius: 6px; padding: 6px 16px; font-size: 13px; font-weight: 600; } QPushButton:hover { background: %2; }")
                        .arg("#8b5cf6", "#7c3aed"));

                    bottomRow->addWidget(cancelBtn);
                    bottomRow->addWidget(confirmBtn);
                    outer->addLayout(bottomRow);

                    auto commitCreate = [this, d, type, titleEdit, contentEdit, complete = std::move(complete)]() mutable {
                        QString finalTitle = titleEdit->text().trimmed();
                        if (finalTitle.isEmpty()) finalTitle = "Sin título";
                        const auto finalDocument = contentEdit->canonicalDocument();
                        const auto finalID = contentEdit->documentObjectId();
                        QJsonObject props;
                        if (type == "task") props["status"] = "todo";
                        d->close();
                        api.post("/v1/objects", {{"id", finalID}, {"type_id", type}, {"title", finalTitle}, {"document", finalDocument},
                                               {"properties", props}, {"links", QJsonArray{}}},
                                 std::move(complete));
                    };

                    connect(confirmBtn, &QPushButton::clicked, d, commitCreate);
                    connect(cancelBtn, &QPushButton::clicked, d, [d, done] {
                        d->close();
                        done({});
                    });
                    connect(titleEdit, &QLineEdit::returnPressed, d, [contentEdit] {
                        contentEdit->setFocus();
                    });

                    d->show();
                    if (titleEdit->text().isEmpty()) titleEdit->setFocus();
                    else contentEdit->setFocus();
                }
            };
        edit->webEmbedRequested = [this, origin, guard = QPointer<RichEditor>(edit)](
                                      QString url, RichEditor::ReferenceCompletion done) {
            api.post("/v1/capture", {{"url", url}, {"source", "desktop"}, {"type_id", "web"}},
                     [this, origin, guard, done](QJsonValue v, QString err) {
                         if (!err.isEmpty()) {
                             error(err);
                             done({});
                             return;
                         }
                         auto o = v.toObject();
                         if (o.contains("object"))
                             o = o.value("object").toObject();
                         rememberObject(o);
                         rebuildSidebar();
                         if (guard && guard->isVisible())
                             done(o);
                         else if (!origin.isEmpty())
                             api.patch("/v1/objects/" + origin, {{"add_links", QJsonArray{str(o, "id")}}},
                                       [](QJsonValue, QString) {
                                       });
                     });
        };
        edit->imagePasted = [this, origin, guard = QPointer<RichEditor>(edit)](QImage image) {
            if (image.isNull()) return;
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (!image.save(&buffer, "PNG")) return;
            saveLabel->setText("Guardando imagen…");
            const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss");
            const QString filename = QString("Captura %1.png").arg(timestamp);
            api.uploadBytes("", filename, bytes, "image/png", [this, guard, image, filename](QJsonValue val, QString err) {
                if (!err.isEmpty()) {
                    error(err);
                    return;
                }
                saveLabel->setText(dirty ? "Sin guardar" : "Guardado");
                const auto res = val.toObject();
                auto mediaObj = res.value("media_object").toObject();
                const QString mediaId = res.value("media_object_id").toString();
                auto insertRef = [this, guard, image, filename](const QJsonObject &obj) {
                    const QString objId = str(obj, "id");
                    rememberObject(obj);
                    rebuildSidebar();
                    const auto pixmap = QPixmap::fromImage(image);
                    imageMemoryCache_[objId] = pixmap;
                    if (guard && guard->isVisible()) {
                        guard->insertImageBlock(objId, pixmap, str(obj, "title").isEmpty() ? filename : str(obj, "title"));
                    }
                };
                if (!mediaObj.isEmpty()) {
                    insertRef(mediaObj);
                } else if (!mediaId.isEmpty()) {
                    api.get("/v1/objects/" + mediaId, [insertRef, this](QJsonValue v, QString e) {
                        if (e.isEmpty()) insertRef(v.toObject());
                        else error(e);
                    });
                }
            });
        };
    }

void MainWindow::loadEditor(RichEditor *edit, const QJsonObject &object) {
        edit->setDocumentObjectId(object.value("id").toString());
        const auto doc = object.value("document").toObject();
        edit->setCanonicalDocument(doc.value("schema_version").toInt() == 2 ? doc : LumenDocument::empty());
        std::function<void(const QJsonArray &)> loadImages = [&](const QJsonArray &nodes) {
            for (const auto &value : nodes) {
                const auto node = value.toObject(); const auto attrs = node.value("attrs").toObject();
                if (node.value("type") == "image") {
                    if (!attrs.value("object_id").toString().isEmpty()) ensureImageLoaded(edit, attrs.value("object_id").toString());
                    else ensureAssetImageLoaded(edit, attrs.value("asset_id").toString());
                }
                loadImages(node.value("children").toArray());
            }
        };
        loadImages(doc.value("blocks").toArray());
        (void)edit->structuredBlocks();
    }

QWidget *MainWindow::calendarToolbar(const QString &selected) {
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(3);
        l->addStretch();
        for (const auto &p : QList<QPair<QString, QString>>{
                 {"Mes", "month"}, {"Semana", "week"}, {"Tres días", "three"}, {"Día", "day"}}) {
            auto *b = button(p.first, "", p.second == selected ? "pillActive" : "quiet");
            b->setMinimumHeight(28);
            connect(b, &QPushButton::clicked, this, [this, p] {
                if(calendarOverview)day=calendarOverview->date();
                if (p.second == "day")
                    navigateJournal(day);
                else
                    showTimeline(p.second == "week" ? 7 : p.second == "three" ? 3 : 31);
            });
            l->addWidget(b);
        }
        l->addStretch();
        if(selected!="day")return w;
        auto *prev = button("", "chevron-left", "quiet"), *today = button("Hoy", "", "quiet"),
             *next = button("", "chevron-right", "quiet");
        for (auto *b : {prev, today, next})
            b->setMinimumHeight(28);
        prev->setFixedWidth(27);
        next->setFixedWidth(27);
        connect(prev, &QPushButton::clicked, this, [this] {
            navigateJournal(day.addDays(-1));
        });
        connect(next, &QPushButton::clicked, this, [this] {
            navigateJournal(day.addDays(1));
        });
        connect(today, &QPushButton::clicked, this, [this] {
            navigateJournal(QDate::currentDate());
        });
        l->addWidget(prev);
        l->addWidget(today);
        l->addWidget(next);
        auto *calendar = button("", "calendar", "quiet");
        calendar->setFixedSize(29, 29);
        calendar->setToolTip("Minicalendario");
        connect(calendar, &QPushButton::clicked, this, [this] {
            if (auto *p = findChild<QScrollArea *>("informationScroll"))
                p->setVisible(!p->isVisible());
        });
        l->addWidget(calendar);
        return w;
    }

void MainWindow::renderEditor(bool journal) {
        resetBody();
        loading = true;
        bool sanitized = false;
        if (journal) {
            auto p = current.value("properties").toObject();
            if (p.contains("image_asset_id")) {
                p.remove("image_asset_id");
                sanitized = true;
            }
            auto assets = p.value("assets").toArray();
            QJsonArray cleanAssets;
            for (auto av : assets) {
                if (av.toObject().value("mime").toString().startsWith("image/")) {
                    sanitized = true;
                } else {
                    cleanAssets.append(av);
                }
            }
            if (sanitized) {
                p["assets"] = cleanAssets;
                current["properties"] = p;
            }
        }
        linked = current.value("links").toArray();
        const auto objectId = str(current, "id");
        const auto typ = typeFor(str(current, "type_id"));
        if (journal)
            body->addWidget(calendarToolbar("day"));
        documentScroll = new QScrollArea;
        documentScroll->setObjectName("documentScroll");
        documentScroll->setWidgetResizable(true);
        documentScroll->setFrameShape(QFrame::NoFrame);
        auto *host = new QWidget;
        host->setObjectName("documentHost");
        auto *hl = new QHBoxLayout(host);
        hl->setContentsMargins(20, journal ? 28 : 48, 20, 50);
        hl->setSpacing(0);
        hl->addStretch();
        auto *canvas = new QWidget;
        canvas->setObjectName("documentCanvas");
        canvas->setMaximumWidth(760);
        canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        documentPane = canvas;
        auto *content = new QVBoxLayout(canvas);
        content->setContentsMargins(6, 0, 6, 0);
        content->setSpacing(12);
        hl->addWidget(canvas, 1);
        hl->addStretch();
        documentScroll->setWidget(host);
        body->addWidget(documentScroll, 1);
        auto *headerPanel = new QWidget;
        auto *headerLayout = new QVBoxLayout(headerPanel);
        headerLayout->setContentsMargins(32, 0, 32, 0);
        headerLayout->setSpacing(12);
        content->addWidget(headerPanel);
        if (journal) {
            auto *dateLine = new QHBoxLayout;
            auto *weekday = label(QLocale(QLocale::Spanish).dayName(day.dayOfWeek()), "eyebrow");
            const QStringList colors = {"#d6ba51", "#d17ac2", "#48c08a", "#dea344",
                                        "#6c9edf", "#a28bdd", "#d97d80"};
            weekday->setStyleSheet("color:" + colors.value(day.dayOfWeek() - 1) + ";");
            dateLine->addWidget(weekday);
            if (day == QDate::currentDate())
                dateLine->addWidget(label("Hoy", "todayBadge"));
            dateLine->addStretch();
            headerLayout->addLayout(dateLine);
        } else {
            // WrapBar (aliases stage): the header chips wrap instead of
            // clipping when the window gets narrow, and a chip alone wider
            // than the row elides its text. Leading chips are the context
            // (type badge, Template, Colecciones); the trailing group
            // (cover, attach, properties, "…") stays right-aligned while
            // everything fits on one line.
            auto *tools = new WrapBar;
            tools->setObjectName("headerChipsBar");
            auto *badge = button(str(typ, "name"), "", "typeBadge");
            badge->setIcon(LumenIcons::typeIcon(typ, 24, dark));
            badge->setIconSize(QSize(24, 24));
            badge->setToolTip("Ver todos los objetos de este tipo");
            connect(badge, &QPushButton::clicked, this, [this, typ] { showCollection(str(typ, "id")); });
            badge->setStyleSheet(QString("color:%1;").arg(str(typ, "color")));
            tools->addChip(badge);
            // D5 cue: flagged objects carry a Template chip on their page;
            // clicking it opens the type's Templates dashboard section.
            if (objectIsTemplate(current)) {
                auto *chip = button("Template", "package-open", "templateChip");
                chip->setToolTip("This object is a template. Click to open this type's templates.");
                connect(chip, &QPushButton::clicked, this, [this, typ] {
                    showCollection(str(typ, "id"), {}, {}, "templates");
                });
                tools->addChip(chip);
            }
            auto *colBtn = button("Colecciones", "network", "quiet");
            colBtn->setToolTip("Ver colecciones de este tipo");
            connect(colBtn, &QPushButton::clicked, this, [this, typ] { showCollection(str(typ, "id")); });
            tools->addChip(colBtn);
            const QString currentCoverId = current.value("properties").toObject().value("image_asset_id").toString();
            auto *coverButton = button(!currentCoverId.isEmpty() ? "Cambiar portada" : "Añadir portada", "photo", "quiet");
            connect(coverButton, &QPushButton::clicked, this, [this, coverButton] {
                showCoverMenu(coverButton);
            });
            tools->addTrailingChip(coverButton);
            auto *attachButton = button("Adjuntar", "file-upload", "quiet");
            connect(attachButton, &QPushButton::clicked, this, [this] {
                attach();
            });
            tools->addTrailingChip(attachButton);
            auto *typeButton = button("Propiedades", "adjustments-horizontal", "quiet");
            connect(typeButton, &QPushButton::clicked, this, [this] {
                propertyDialog(str(current, "type_id"));
            });
            tools->addTrailingChip(typeButton);
            auto *moreBtn = new QToolButton;
            moreBtn->setIcon(LumenIcons::icon("dots", dark ? QColor("#e4e3e2") : QColor("#222224"), 20));
            moreBtn->setAutoRaise(true);
            moreBtn->setToolTip("Opciones del objeto");
            moreBtn->setMinimumSize(30, 30);
            connect(moreBtn, &QToolButton::clicked, this, [this, moreBtn, canvas] {
                showObjectOptionsMenu(moreBtn, nullptr, nullptr, nullptr, canvas);
            });
            tools->addTrailingChip(moreBtn);
            headerLayout->addWidget(tools);
        }
        auto props = current.value("properties").toObject();
        const QString imageId = props.value("image_asset_id").toString();
        const bool isImageObj = str(current, "type_id") == "image";
        if (!journal && !imageId.isEmpty()) {
            auto *preview = new QLabel;
            preview->setObjectName("ogCover");
            preview->setAlignment(Qt::AlignCenter);
            preview->setMinimumHeight(120);
            preview->setMaximumHeight(isImageObj ? 350 : 200);
            preview->setStyleSheet("border-radius:8px;");
            headerLayout->addWidget(preview);
            QPointer<QLabel> guard = preview;
            api.bytes("/v1/assets/" + imageId + "/content", [guard](QByteArray data, QString err) {
                QPixmap p;
                if (guard && err.isEmpty() && p.loadFromData(data))
                    guard->setPixmap(p.scaled(qMax(200, guard->width()), guard->maximumHeight(),
                                              Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
            });
        }
        title = new DocumentTitle(journal ? QLocale(QLocale::Spanish).toString(day, "d 'de' MMMM 'de' yyyy")
                                          : str(current, "title"));
        title->setObjectName("documentTitle");
        title->setReadOnly(journal);
        auto *tagRow = new QWidget;
        tagRow->setObjectName("tagChipsRow");
        auto *tagRowLayout = new QHBoxLayout(tagRow);
        tagRowLayout->setContentsMargins(0, 0, 0, 0);
        tagRowLayout->setSpacing(6);
        tagRowLayout->addStretch();
        QHBoxLayout *journalFilters = nullptr;
        if (journal) {
            auto *titleRow = new QHBoxLayout;
            titleRow->setSpacing(12);
            QFont headingFont("Inter");
            headingFont.setPixelSize(31);
            headingFont.setBold(true);
            title->setFixedWidth(qMin(590, QFontMetrics(headingFont).horizontalAdvance(title->text()) + 14));
            titleRow->addWidget(title);
            titleRow->addWidget(label("Semana " + QString::number(day.weekNumber()), "muted"));
            titleRow->addStretch();
            headerLayout->addLayout(titleRow);
            headerLayout->addWidget(tagRow);
            journalFilters = new QHBoxLayout;
            journalFilters->setSpacing(4);
            headerLayout->addLayout(journalFilters);
        } else {
            headerLayout->addWidget(title);
            headerLayout->addWidget(tagRow);
            auto *aliasesLabel = label("", "aliasesLabel");
            aliasesLabel->setStyleSheet(QString("color:%1;font-size:13px;padding:0 1px;")
                                            .arg(dark ? "#919097" : "#75747b"));
            aliasesLabel->setToolTip("Aliases — alternate names. Search matches them; "
                                     "picking an alias result inserts the alias as the link text.");
            const QStringList objectAliases = LumenAliases::of(current);
            aliasesLabel->setText(objectAliases.join(", "));
            aliasesLabel->setVisible(!objectAliases.isEmpty());
            headerLayout->addWidget(aliasesLabel);
        }
        LumenProperties *form = nullptr;
        if (!journal) {
            form = new LumenProperties;
            form->setObjectName("objectProperties");
            form->setTheme(dark);
            form->setData(current, typ, objects, types);
            headerLayout->addWidget(form);
        }
        QPointer<LumenProperties> formGuard(form);
        std::function<void()> refreshTags = [this, guard = QPointer<QWidget>(tagRow), formGuard] {
            if (!guard) return;
            refreshTagChips(guard, editor, [this] { return current; },
                            [this, formGuard](const QJsonObject &o) {
                                current = o;
                                if (formGuard) formGuard->setData(o, typeFor(str(o, "type_id")), objects, types);
                            },
                            [this] { markDirty(); });
        };
        if (form) {
            form->onChange = [this, refreshTags](QString key, QJsonValue value) {
                auto p = current.value("properties").toObject();
                p[key] = value;
                if (key == "summary") p.remove("description");
                current["properties"] = p;
                refreshTags();
                markDirty();
            };
            form->onAddProperty = [this] {
                propertyDialog(str(current, "type_id"));
            };
            form->onEditType = form->onAddProperty;
        }
        if (journal || str(current, "type_id") == "note") {
            auto assets = props.value("assets").toArray();
            QJsonArray cleanAssets;
            bool changed = false;
            for (auto av : assets) {
                if (av.toObject().value("mime").toString().startsWith("image/")) {
                    changed = true;
                } else {
                    cleanAssets.append(av);
                }
            }
            if (changed) {
                auto p = current.value("properties").toObject();
                p["assets"] = cleanAssets;
                current["properties"] = p;
                markDirty();
            }
        }
        if (props.contains("url")) {
            auto *url = button(QUrl(props.value("url").toString()).host(), "external-link", "quiet");
            url->setToolTip(props.value("url").toString());
            connect(url, &QPushButton::clicked, this, [props] {
                auto u = QUrl(props.value("url").toString());
                if (u.scheme() == "https" || u.scheme() == "http")
                    QDesktopServices::openUrl(u);
            });
            headerLayout->addWidget(url);
        }
        if (str(current, "type_id") == "pdf" || str(current, "type_id") == "image") {
            auto assets = props.value("assets").toArray();
            if (!assets.isEmpty()) {
                const auto a = assets.first().toObject();
                auto *view = button("Abrir " + str(a, "name"),
                                    str(current, "type_id") == "pdf" ? "file-type-pdf" : "photo", "primary");
                connect(view, &QPushButton::clicked, this, [this, a] {
                    previewAsset(str(a, "id"), a);
                });
                auto *mediaActions = new QHBoxLayout;
                mediaActions->setSpacing(8);
                mediaActions->addWidget(view);
                if (str(current, "type_id") == "image") {
                    auto *copyBtn = button("Copiar imagen", "copy", "quiet");
                    connect(copyBtn, &QPushButton::clicked, this, [this, a] {
                        api.bytes("/v1/assets/" + str(a, "id") + "/content", [this](QByteArray bytes, QString err) {
                            QPixmap px;
                            if (err.isEmpty() && px.loadFromData(bytes)) {
                                QApplication::clipboard()->setPixmap(px);
                                QToolTip::showText(QCursor::pos(), "Imagen copiada al portapapeles", this);
                            }
                        });
                    });
                    mediaActions->addWidget(copyBtn);
                }
                mediaActions->addStretch();
                headerLayout->addLayout(mediaActions);
            }
        }
        auto *notesPanel = new QWidget;
        auto *noteLayout = new QVBoxLayout(notesPanel);
        noteLayout->setContentsMargins(0, 0, 0, 0);
        noteLayout->setSpacing(0);
        content->addWidget(notesPanel);
        auto *heading = new QHBoxLayout;
        heading->setContentsMargins(32, 0, 32, 0);
        heading->addWidget(label(journal ? "Nota diaria" : "Notas", "subheading"));
        heading->addStretch();
        auto *block = button("", "plus", "quiet");
        block->setToolTip("Insertar bloque · /");
        block->setFixedSize(28, 28);
        connect(block, &QPushButton::clicked, this, [this] {
            if (editor)
                editor->showBlockMenu();
        });
        heading->addWidget(block);
        noteLayout->addLayout(heading);
        editor = new RichEditor;
        editor->setObjectName("editor");
        editor->setPlaceholderText("Escribe aquí… Usa @ para enlazar o / para crear un bloque");
        wireEditor(editor, objectId);
        QFont f("Inter");
        f.setPixelSize(fontSize);
        editor->setFont(f);
        editor->document()->setDefaultStyleSheet(
            QString("p{margin-bottom:10px;line-height:150%;}a{color:%1;text-decoration:underline;}")
                .arg(dark ? "#c4c4ca" : "#48546d"));
        loadEditor(editor, current);
        editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        editor->setMinimumHeight(200);
        auto resizeEditor = [guard = QPointer<RichEditor>(editor)] {
            if (guard)
                guard->setFixedHeight(qBound(200, qCeil(guard->document()->size().height()) + 28, 100000));
        };
        connect(editor->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                editor, [resizeEditor] {
                    resizeEditor();
                });
        resizeEditor();
        noteLayout->addWidget(editor);
        reconcileReferences();
        // Rebuilding derived reference chips is a read, not a document edit.
        connect(editor, &QTextEdit::textChanged, this, [this] {
            if (!loading) {
                reconcileReferences();
                markDirty();
            }
        });
        connect(editor, &QTextEdit::textChanged, this, refreshTags);
        refreshTags();
        connect(title, &QTextEdit::textChanged, this, [this, journal] {
            if (!journal && !loading)
                markDirty();
        });
        auto *notesFooter = new QWidget;
        auto *notesFooterLayout = new QVBoxLayout(notesFooter);
        notesFooterLayout->setContentsMargins(32, 0, 32, 0);
        notesFooterLayout->setSpacing(12);
        noteLayout->addWidget(notesFooter);
        linkedSection = new QWidget;
        auto *ll = new QVBoxLayout(linkedSection);
        ll->setContentsMargins(0, 0, 0, 0);
        ll->setSpacing(8);
        renderLinkedObjects();
        notesFooterLayout->addWidget(linkedSection);
        if (journal) {
            renderJournalTasks(notesFooterLayout, day);
        } else {
            renderObjectAssociatedTasks(notesFooterLayout, objectId);
        }
        auto *createdPanel = new QWidget;
        auto *createdLayout = new QVBoxLayout(createdPanel);
        createdLayout->setContentsMargins(32, 0, 32, 0);
        createdLayout->setSpacing(12);
        content->addWidget(createdPanel);
        createdLayout->addSpacing(18);
        if (!journal) {
            // "Collections that contain this object", per the contract's
            // recommended lookup (backlinks filtered to the collection type).
            auto *collectionsPanel = new QWidget;
            collectionsPanel->setObjectName("objectCollectionsSection");
            collectionsPanel->hide();
            auto *collectionsLayout = new QVBoxLayout(collectionsPanel);
            collectionsLayout->setContentsMargins(0, 0, 0, 0);
            collectionsLayout->setSpacing(8);
            createdLayout->addWidget(collectionsPanel);
            renderObjectCollections(collectionsPanel, objectId);
        }
        createdLayout->addWidget(label(journal ? "Creado en este día" : "Enlaces entrantes", "subheading"));
        QJsonArray related;
        for (auto value : objects) {
            auto o = value.toObject();
            if (str(o, "type_id") == "journal" && journal)
                continue;
            bool include =
                journal
                    ? QDateTime::fromString(str(o, "created_at"), Qt::ISODateWithMs).toLocalTime().date() ==
                          day
                    : allLinks(o).contains(objectId);
            if (include)
                related.append(o);
        }
        {
            auto *views = new LumenViews;
            views->setObjectName("relatedObjects");
            views->setProperty("lumenRelated", journal ? "created" : "backlinks");
            views->setProperty("lumenTarget", journal ? day.toString(Qt::ISODate) : objectId);
            configureViews(views);
            QJsonArray ids;
            for (auto value : related)
                ids.append(value.toObject().value("id"));
            views->setData(objects, types);
            views->setScope("", {{"ids", ids}, {"presentation", journal ? "wall" : "embed"}});
            views->setMinimumHeight(240);
            createdLayout->addWidget(views);
        }
        if (journal) {
            QJsonArray dated;
            for (auto value : objects) {
                auto o = value.toObject();
                if (str(o, "type_id") != "journal" &&
                    o.value("properties").toObject().value("date").toString() == day.toString(Qt::ISODate))
                    dated.append(o);
            }
            if (!dated.isEmpty()) {
                createdLayout->addWidget(label("Referencias a esta fecha", "subheading"));
                auto *views = new LumenViews;
                configureViews(views);
                QJsonArray ids;
                for (auto value : dated)
                    ids.append(value.toObject().value("id"));
                views->setData(objects, types);
                views->setScope("", {{"ids", ids}, {"presentation", "list"}});
                views->setMinimumHeight(140);
                createdLayout->addWidget(views);
            }
        }
        if (journalFilters) {
            auto buttons = std::make_shared<QList<QPushButton *>>();
            for (auto value : QStringList{"Todo", "Nota diaria", "Creado hoy"}) {
                auto *b = button(value, "", buttons->isEmpty() ? "pillActive" : "quiet");
                b->setMinimumHeight(30);
                buttons->append(b);
                journalFilters->addWidget(b);
                connect(b, &QPushButton::clicked, notesPanel, [notesPanel, createdPanel, buttons, b, value] {
                    notesPanel->setVisible(value != "Creado hoy");
                    createdPanel->setVisible(value != "Nota diaria");
                    for (auto *item : *buttons) {
                        item->setObjectName(item == b ? "pillActive" : "quiet");
                        item->style()->unpolish(item);
                        item->style()->polish(item);
                    }
                });
            }
            journalFilters->addStretch();
        }
        renderInspector(journal);
        loading = false;
        if (sanitized || current.value("links").toArray() != linked) {
            markDirty();
            saveCurrent();
        } else {
            dirty = false;
            saveLabel->setText(saveState(current));
        }
        QTimer::singleShot(0, documentScroll, [this, objectId, resizeEditor] {
            resizeEditor();
            if (documentScroll && str(current, "id") == objectId)
                documentScroll->verticalScrollBar()->setValue(scrollPositions.value(objectId));
        });
    }

void MainWindow::renderInspector(bool journal) {
        clearLayout(inspect);
        if (journal) {
            auto *cal = new LumenMiniCalendar;
            cal->setObjectName("miniCalendar");
            miniCalendar = cal;
            cal->setLocale(QLocale(QLocale::Spanish));
            cal->setTheme(dark);
            cal->setWorkspaceObjects(objects);
            cal->setSelectedDate(day);
            cal->onDateRequested = [this](QDate date) {
                navigateJournal(date);
            };
            inspect->addWidget(cal);
            inspect->addWidget(label("Elige un día para continuar", "muted"));
        } else
            inspect->addWidget(label("Información", "subheading"));
        auto assets = current.value("properties").toObject().value("assets").toArray();
        if (!assets.isEmpty())
            inspect->addWidget(label("Archivos", "section"));
        for (auto value : assets) {
            auto a = value.toObject();
            auto id = value.isString() ? value.toString() : str(a, "id");
            auto *row = new QHBoxLayout;
            auto *b =
                button(str(a, "name").isEmpty() ? "Ver archivo" : str(a, "name"),
                       a.value("mime").toString().contains("pdf") ? "file-type-pdf" : "photo", "reference");
            connect(b, &QPushButton::clicked, this, [this, id, a] {
                previewAsset(id, a);
            });
            row->addWidget(b, 1);
            if (str(a, "media_object_id").isEmpty()) {
                auto *promote = button("", "external-link", "quiet");
                promote->setFixedSize(28, 28);
                promote->setToolTip("Convertir en objeto reutilizable");
                connect(promote, &QPushButton::clicked, this, [this, id] {
                    api.post("/v1/assets/" + id + "/promote", {}, [this](QJsonValue v, QString e) {
                        if (!e.isEmpty()) {
                            error(e);
                            return;
                        }
                        auto object = v.toObject().value("object").toObject();
                        rememberObject(object);
                        refresh();
                    });
                });
                row->addWidget(promote);
            } else {
                const auto mediaId = str(a, "media_object_id");
                auto *open = button("", "external-link", "quiet");
                open->setFixedSize(28, 28);
                open->setToolTip("Abrir objeto del archivo");
                connect(open, &QPushButton::clicked, this, [this, mediaId] {
                    openPreview(mediaId);
                });
                row->addWidget(open);
            }
            inspect->addLayout(row);
        }
        auto *add = button("Adjuntar imagen o PDF", "plus", "quiet");
        connect(add, &QPushButton::clicked, this, [this] {
            attach();
        });
        inspect->addWidget(add);
        inspect->addSpacing(15);
        inspect->addWidget(label("Enlaces entrantes", "section"));
        int count = 0;
        auto id = str(current, "id");
        for (auto value : objects) {
            auto o = value.toObject();
            if (!allLinks(o).contains(id))
                continue;
            ++count;
            auto oType = typeFor(str(o, "type_id"));
            auto *card = new ClickableCard(inspector);
            card->setObjectName("backlinkCard");
            card->setCursor(Qt::PointingHandCursor);
            card->setStyleSheet(QString(
                "#backlinkCard { background: %1; border: 1px solid %2; border-radius: 8px; padding: 6px 8px; margin-bottom: 6px; }"
                "#backlinkCard:hover { border-color: %3; background: %4; }"
            ).arg(dark ? "#1f1f23" : "#fbfbfa", dark ? "#2e2e34" : "#e5e5e7", dark ? "#8b5cf6" : "#a855f7", dark ? "#25252b" : "#f4f4f6"));

            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(6, 6, 6, 6);
            cardLayout->setSpacing(4);

            auto *headerRow = new QHBoxLayout;
            headerRow->setContentsMargins(0, 0, 0, 0);
            headerRow->setSpacing(6);

            auto *iconLabel = new QLabel(card);
            iconLabel->setPixmap(LumenIcons::typeIcon(oType, 18, dark).pixmap(18, 18));
            headerRow->addWidget(iconLabel);

            auto *titleLabel = new QLabel(str(o, "title").isEmpty() ? "Sin título" : str(o, "title"), card);
            titleLabel->setStyleSheet(QString("font-weight: 600; font-size: 13px; color: %1;").arg(dark ? "#f4f4f5" : "#18181b"));
            headerRow->addWidget(titleLabel, 1);

            QString propRelation;
            auto props = o.value("properties").toObject();
            for (auto it = props.begin(); it != props.end(); ++it) {
                if (it.value().toString() == id) {
                    propRelation = it.key();
                    break;
                }
                if (it.value().isArray()) {
                    for (const auto &arrVal : it.value().toArray()) {
                        if (arrVal.toString() == id) {
                            propRelation = it.key();
                            break;
                        }
                    }
                    if (!propRelation.isEmpty()) break;
                }
            }
            auto *badge = new QLabel(propRelation.isEmpty() ? str(oType, "name") : (str(oType, "name") + QString::fromUtf8(" → ") + propRelation), card);
            badge->setStyleSheet(QString("background: %1; color: %2; font-size: 10px; font-weight: 600; border-radius: 4px; padding: 2px 6px;")
                .arg(dark ? "#2d2a38" : "#f3e8ff", dark ? "#c084fc" : "#7e22ce"));
            headerRow->addWidget(badge);
            cardLayout->addLayout(headerRow);

            const QString snippet = referenceSnippet(o, id, str(current, "title"));
            const QString displayText = !snippet.isEmpty() ? snippet : str(o, "content").simplified().left(140);
            if (!displayText.isEmpty()) {
                auto *snipLabel = new QLabel(displayText, card);
                snipLabel->setWordWrap(true);
                snipLabel->setStyleSheet(QString(
                    "font-size: 11px; color: %1; border-left: 2px solid %2; padding-left: 6px; margin-top: 2px;"
                ).arg(dark ? "#a1a1aa" : "#71717a", dark ? "#52525b" : "#d4d4d8"));
                cardLayout->addWidget(snipLabel);
            }

            card->onClicked = [this, o, id] {
                openPreview(str(o, "id"), referenceBlock(o, id));
            };
            inspect->addWidget(card);
        }
        if (!count)
            inspect->addWidget(label("Las menciones aparecerán aquí.", "muted"));
        inspect->addStretch();
        inspect->addWidget(
            label("@  enlazar o crear\n/  insertar bloque\nCtrl P  paleta global\nCtrl J  diario de hoy",
                  "keyboardHints"));
    }

QString MainWindow::referenceBlock(const QJsonObject &source, const QString &target) const {
        for (auto v : source.value("properties").toObject().value("blocks").toArray())
            for (auto r : v.toObject().value("runs").toArray())
                if (r.toObject().value("object_id").toString() == target)
                    return v.toObject().value("id").toString();
        return {};
    }

QString MainWindow::referenceSnippet(const QJsonObject &source, const QString &target, const QString &targetTitle) const {
        for (auto v : source.value("properties").toObject().value("blocks").toArray()) {
            auto blk = v.toObject();
            for (auto r : blk.value("runs").toArray()) {
                if (r.toObject().value("object_id").toString() == target) {
                    QString text = blk.value("text").toString();
                    if (!text.isEmpty()) return text;
                }
            }
        }
        const QString cnt = source.value("content").toString();
        if (!cnt.isEmpty() && !targetTitle.isEmpty() && cnt.contains(targetTitle, Qt::CaseInsensitive)) {
            for (const auto &line : cnt.split('\n')) {
                if (line.contains(targetTitle, Qt::CaseInsensitive))
                    return line.trimmed();
            }
        }
        return {};
    }

void MainWindow::savePreview(QPointer<GuardedDialog> dialog, std::shared_ptr<PreviewSession> state) {
        if (!dialog || state->saving)
            return;
        if (!state->dirty) {
            if (state->closing) {
                dialog->close();
                if (state->afterClose) {
                    auto next = std::move(state->afterClose);
                    next();
                }
            }
            return;
        }
        state->timer->stop();
        state->object["title"] = state->heading->text().trimmed();
        state->object["document"] = state->edit->canonicalDocument();
        state->object.remove("content");
        auto p = state->object.value("properties").toObject();
        p.remove("rich_text"); p.remove("blocks");
        state->object["properties"] = p;
        state->object["op_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        // Inline references are derived by the backend; preserve explicit links.
        if (state->object.contains("revision"))
            state->object["base_revision"] = state->object.value("revision");
        state->saving = true;
        state->dirty = false;
        state->status->setText("Guardando…");
        api.patchEx("/v1/objects/" + str(state->object, "id"), state->object,
                  [this, dialog, state](int status, QJsonValue v, QString e) {
                      state->saving = false;
                      if (!dialog)
                          return;
                      if (status == 409) {
                          // A conflict never advances the base revision of a local draft.
                          // Reload/merge must be explicit before it can overwrite remote work.
                          state->dirty = true;
                          state->closing = false;
                          state->status->setText("Conflicto: revisa el objeto antes de guardar");
                          return;
                      }
                      if (!e.isEmpty()) {
                          state->dirty = true;
                          state->closing = false;
                          state->status->setText("No se guardó: " + e);
                          return;
                      }
                      rememberObject(v.toObject());
                      if (state->dirty) state->object["revision"] = v.toObject().value("revision");
                      if (!state->dirty) {
                          state->object = v.toObject();
                          state->status->setText(saveState(state->object));
                      }
                      if (state->dirty)
                          state->timer->start();
                      else if (state->closing) {
                          dialog->close();
                          if (state->afterClose) {
                              auto next = std::move(state->afterClose);
                              next();
                          }
                      }
                  });
    }

void MainWindow::setCoverAsset(const QString &assetId) {
        if (current.isEmpty() || assetId.isEmpty()) return;
        auto p = current.value("properties").toObject();
        p["image_asset_id"] = assetId;
        current["properties"] = p;
        rememberObject(current);
        markDirty();
        saveCurrent([this] {
            renderEditor(route == "journal");
            rebuildSidebar();
        });
    }

void MainWindow::removeCoverAsset() {
        if (current.isEmpty()) return;
        auto p = current.value("properties").toObject();
        p["image_asset_id"] = "";
        current["properties"] = p;
        rememberObject(current);
        markDirty();
        saveCurrent([this] {
            renderEditor(route == "journal");
            rebuildSidebar();
        });
    }

void MainWindow::chooseCoverFile(std::function<void(QString)> onDone) {
        QString p = QFileDialog::getOpenFileName(this, "Elegir imagen de portada", QString(),
                                                "Imágenes (*.png *.jpg *.jpeg *.webp);;Todos los archivos (*)");
        if (p.isEmpty()) return;
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) return;
        QByteArray bytes = f.readAll();
        QString mime = "image/png";
        if (p.endsWith(".jpg", Qt::CaseInsensitive) || p.endsWith(".jpeg", Qt::CaseInsensitive)) mime = "image/jpeg";
        else if (p.endsWith(".webp", Qt::CaseInsensitive)) mime = "image/webp";
        saveLabel->setText("Subiendo portada…");
        api.uploadBytes("", QFileInfo(p).fileName(), bytes, mime, [this, onDone](QJsonValue val, QString err) {
            if (!err.isEmpty()) {
                error(err);
                return;
            }
            const auto res = val.toObject();
            const QString assetId = res.value("id").toString();
            if (onDone) onDone(assetId);
            else setCoverAsset(assetId);
        });
    }

void MainWindow::pasteCoverFromClipboard(std::function<void(QString)> onDone) {
        auto img = QApplication::clipboard()->image();
        if (img.isNull()) {
            error("El portapapeles no contiene ninguna imagen.");
            return;
        }
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (!img.save(&buf, "PNG")) return;
        saveLabel->setText("Guardando portada…");
        const QString filename = QString("Portada %1.png").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH.mm.ss"));
        api.uploadBytes("", filename, bytes, "image/png", [this, onDone](QJsonValue val, QString err) {
            if (!err.isEmpty()) {
                error(err);
                return;
            }
            const auto res = val.toObject();
            const QString assetId = res.value("id").toString();
            if (onDone) onDone(assetId);
            else setCoverAsset(assetId);
        });
    }

void MainWindow::openAddImageModal(std::function<void(QString)> applyNewCover, QWidget *parentWin) {
        auto uploader = [this](const QString &objId, const QString &fn, const QByteArray &b, const QString &mime, std::function<void(QJsonValue, QString)> cb) {
            api.uploadBytes(objId, fn, b, mime, cb);
        };
        auto byteFetcher = [this](const QString &path, std::function<void(QByteArray, QString)> cb) {
            api.bytes(path, cb);
        };
        QWidget *dlgParent = parentWin ? parentWin : this;
        auto *dlg = new LumenAddImageDialog(dlgParent, uploader, byteFetcher, objects, dark, applyNewCover);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->exec();
    }

void MainWindow::showCoverMenu(QWidget *anchor, std::shared_ptr<PreviewSession> previewSession,                        QPointer<QLabel> previewCover, QPointer<QPushButton> previewBtn) {
        QWidget *parentWin = anchor ? anchor->window() : this;
        const auto &targetObj = previewSession ? previewSession->object : current;
        const QString coverId = targetObj.value("properties").toObject().value("image_asset_id").toString();

        auto applyNewCover = [this, previewSession, previewCover, previewBtn](const QString &assetId) {
            if (previewSession) {
                auto p = previewSession->object.value("properties").toObject();
                p["image_asset_id"] = assetId;
                previewSession->object["properties"] = p;
                rememberObject(previewSession->object);
                if (previewBtn) previewBtn->setText("Cambiar portada");
                if (previewCover) {
                    previewCover->show();
                    QPointer<QLabel> guard = previewCover;
                    api.bytes("/v1/assets/" + assetId + "/content", [guard](QByteArray data, QString err) {
                        QPixmap px;
                        if (guard && err.isEmpty() && px.loadFromData(data))
                            guard->setPixmap(px.scaled(qMax(200, guard->width()), guard->maximumHeight(),
                                                      Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    });
                }
                if (previewSession->status) previewSession->status->setText("Guardando…");
                api.patch("/v1/objects/" + str(previewSession->object, "id"), QJsonObject{{"properties", previewSession->object.value("properties")},
                                      {"base_revision", previewSession->object.value("revision")}},
                          [this, previewSession](QJsonValue v, QString err) {
                              if (!err.isEmpty()) {
                                  if (previewSession->status) previewSession->status->setText("No se guardó: " + err);
                                  return;
                              }
                              if (!v.toObject().isEmpty()) {
                                  rememberObject(v.toObject());
                                  previewSession->object["revision"] = v.toObject().value("revision");
                                  if (!previewSession->dirty) {
                                      previewSession->object = v.toObject();
                                      if (previewSession->status) previewSession->status->setText(saveState(previewSession->object));
                                  }
                              }
                              rebuildSidebar();
                          });
            } else {
                setCoverAsset(assetId);
            }
        };

        if (coverId.isEmpty()) {
            openAddImageModal(applyNewCover, parentWin);
            return;
        }

        QMenu menu(parentWin);
        menu.addAction(LumenIcons::icon("photo"), "Cambiar Imagen de Portada…", [this, applyNewCover, parentWin] {
            openAddImageModal(applyNewCover, parentWin);
        });

        menu.addAction(LumenIcons::icon("file-upload"), "Elegir archivo local…", [this, applyNewCover] {
            chooseCoverFile(applyNewCover);
        });

        menu.addAction(LumenIcons::icon("copy"), "Pegar imagen del portapapeles", [this, applyNewCover] {
            pasteCoverFromClipboard(applyNewCover);
        });

        QJsonArray docRefs = previewSession && previewSession->edit ? previewSession->edit->documentReferenceIds() : documentReferences();
        bool addedSubhead = false;
        for (const auto &refVal : docRefs) {
            auto refObj = objectFor(refVal.toString());
            if (str(refObj, "type_id") == "image") {
                auto rProps = refObj.value("properties").toObject();
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
                    if (!addedSubhead) {
                        menu.addSeparator();
                        addedSubhead = true;
                    }
                    QString t = str(refObj, "title");
                    if (t.isEmpty()) t = "Imagen de la nota";
                    menu.addAction(LumenIcons::icon("photo"), "Usar: " + t, [applyNewCover, aId] {
                        applyNewCover(aId);
                    });
                }
            }
        }

        if (!coverId.isEmpty()) {
            menu.addSeparator();
            menu.addAction(LumenIcons::icon("x"), "Quitar portada", [this, previewSession, previewCover, previewBtn] {
                if (previewSession) {
                    auto p = previewSession->object.value("properties").toObject();
                    p["image_asset_id"] = "";
                    previewSession->object["properties"] = p;
                    rememberObject(previewSession->object);
                    if (previewBtn) previewBtn->setText("Añadir portada");
                    if (previewCover) {
                        previewCover->clear();
                        previewCover->hide();
                    }
                    if (previewSession->status) previewSession->status->setText("Guardando…");
                    api.patch("/v1/objects/" + str(previewSession->object, "id"), QJsonObject{{"properties", previewSession->object.value("properties")},
                                      {"base_revision", previewSession->object.value("revision")}},
                              [this, previewSession](QJsonValue v, QString err) {
                                  if (!err.isEmpty()) {
                                      if (previewSession->status) previewSession->status->setText("No se guardó: " + err);
                                      return;
                                  }
                                  if (!v.toObject().isEmpty()) {
                                     rememberObject(v.toObject());
                                     if (!previewSession->dirty) {
                                         previewSession->object = v.toObject();
                                         if (previewSession->status) previewSession->status->setText(saveState(previewSession->object));
                                     }
                                  }
                                  rebuildSidebar();
                              });
                } else {
                    removeCoverAsset();
                }
            });
        }
        menu.exec(anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos());
    }

void MainWindow::showObjectOptionsMenu(QWidget *anchor, std::shared_ptr<PreviewSession> previewSession,                                QPointer<QLabel> previewCover, QPointer<QPushButton> previewBtn,                                QWidget *canvasToResize) {
        QWidget *parentWin = anchor ? anchor->window() : this;
        QMenu menu(parentWin);
        const auto &targetObj = previewSession ? previewSession->object : current;
        const QString coverId = targetObj.value("properties").toObject().value("image_asset_id").toString();
        QString summary = targetObj.value("properties").toObject().value("summary").toString();
        if (summary.isEmpty()) summary = targetObj.value("properties").toObject().value("description").toString();

        auto applyNewCover = [this, previewSession, previewCover, previewBtn](const QString &assetId) {
            if (previewSession) {
                auto p = previewSession->object.value("properties").toObject();
                p["image_asset_id"] = assetId;
                previewSession->object["properties"] = p;
                rememberObject(previewSession->object);
                if (previewBtn) previewBtn->setText("Cambiar portada");
                if (previewCover) {
                    previewCover->show();
                    QPointer<QLabel> guard = previewCover;
                    api.bytes("/v1/assets/" + assetId + "/content", [guard](QByteArray data, QString err) {
                        QPixmap px;
                        if (guard && err.isEmpty() && px.loadFromData(data))
                            guard->setPixmap(px.scaled(qMax(200, guard->width()), guard->maximumHeight(),
                                                      Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    });
                }
                if (previewSession->status) previewSession->status->setText("Guardando…");
                api.patch("/v1/objects/" + str(previewSession->object, "id"), QJsonObject{{"properties", previewSession->object.value("properties")},
                                      {"base_revision", previewSession->object.value("revision")}},
                          [this, previewSession](QJsonValue v, QString err) {
                              if (!err.isEmpty()) {
                                  if (previewSession->status) previewSession->status->setText("No se guardó: " + err);
                                  return;
                              }
                              if (!v.toObject().isEmpty()) {
                                  rememberObject(v.toObject());
                                  previewSession->object["revision"] = v.toObject().value("revision");
                                  if (!previewSession->dirty) {
                                      previewSession->object = v.toObject();
                                      if (previewSession->status) previewSession->status->setText(saveState(previewSession->object));
                                  }
                              }
                              rebuildSidebar();
                          });
            } else {
                setCoverAsset(assetId);
            }
        };

        menu.addAction(LumenIcons::icon("file-text"), summary.isEmpty() ? "Añadir Descripción" : "Editar Descripción", [this, previewSession] {
            if (previewSession) {
                if (previewSession->heading) previewSession->heading->setFocus();
            } else {
                if (title) title->setFocus();
            }
        });

        // D6: the alias chip editor (add via Enter, remove via the chip's x
        // or Backspace on the empty input); commits through the normal
        // properties PATCH and shows the server's 400s non-blocking.
        menu.addAction(LumenIcons::icon("tag"), "Aliases…", [this, targetObj, previewSession] {
            showAliasEditor(str(targetObj, "id"), previewSession);
        });

        menu.addAction(LumenIcons::icon("photo"), coverId.isEmpty() ? "Añadir Imagen de Portada" : "Cambiar Imagen de Portada", [this, applyNewCover, parentWin] {
            openAddImageModal(applyNewCover, parentWin);
        });

        if (!coverId.isEmpty()) {
            menu.addAction(LumenIcons::icon("x"), "Quitar Imagen de Portada", [this, previewSession, previewCover, previewBtn] {
                if (previewSession) {
                    auto p = previewSession->object.value("properties").toObject();
                    p["image_asset_id"] = "";
                    previewSession->object["properties"] = p;
                    rememberObject(previewSession->object);
                    if (previewBtn) previewBtn->setText("Añadir portada");
                    if (previewCover) {
                        previewCover->clear();
                        previewCover->hide();
                    }
                    if (previewSession->status) previewSession->status->setText("Guardando…");
                    api.patch("/v1/objects/" + str(previewSession->object, "id"), QJsonObject{{"properties", previewSession->object.value("properties")},
                                      {"base_revision", previewSession->object.value("revision")}},
                              [this, previewSession](QJsonValue v, QString err) {
                                  if (!err.isEmpty()) {
                                      if (previewSession->status) previewSession->status->setText("No se guardó: " + err);
                                      return;
                                  }
                                  if (!v.toObject().isEmpty()) {
                                      rememberObject(v.toObject());
                                      previewSession->object["revision"] = v.toObject().value("revision");
                                      if (!previewSession->dirty) {
                                          previewSession->object = v.toObject();
                                          if (previewSession->status) previewSession->status->setText(saveState(previewSession->object));
                                      }
                                  }
                                  rebuildSidebar();
                              });
                } else {
                    removeCoverAsset();
                }
            });
        }

        auto *wideAction = menu.addAction(LumenIcons::icon("arrows-maximize"), "Diseño Amplio");
        wideAction->setCheckable(true);
        const bool isCurrentlyWide = canvasToResize && canvasToResize->maximumWidth() > 800;
        wideAction->setChecked(isCurrentlyWide);
        connect(wideAction, &QAction::toggled, this, [canvasToResize](bool checked) {
            if (canvasToResize) {
                canvasToResize->setMaximumWidth(checked ? 1150 : 720);
                canvasToResize->updateGeometry();
            }
        });

        menu.addSeparator();

        menu.addAction(LumenIcons::icon("copy"), "Copiar enlace al objeto", [targetObj] {
            QApplication::clipboard()->setText("lumen://object/" + str(targetObj, "id"));
        });

        menu.addAction(LumenIcons::icon("adjustments-horizontal"), "Propiedades del tipo…", [this, targetObj] {
            propertyDialog(str(targetObj, "type_id"));
        });

        const QString targetId = str(targetObj, "id");
        const bool targetIsTemplate = objectIsTemplate(targetObj);
        menu.addAction(LumenIcons::icon("package-open"),
                       targetIsTemplate ? "Unsave template" : "Save as template",
                       [this, targetId, targetIsTemplate] {
                           setObjectTemplateFlag(targetId, !targetIsTemplate);
                       });

        menu.addAction(LumenIcons::icon("file-upload"), "Adjuntar archivo…", [this] {
            attach();
        });

        menu.exec(anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos());
    }

// Definición de la propiedad tags de un tipo. Todas las piezas de contenido
// estándar la traen (server/model.go); el respaldo cubre tipos viejos o
// personalizados sin ella para que la fila siga ofreciendo el selector.
static QJsonObject tagsDefinitionFor(const QJsonObject &typ) {
        for (const auto &v : typ.value("properties").toArray()) {
            const auto def = v.toObject();
            const QString key = def.value("key").toString(def.value("id").toString(def.value("name").toString()));
            if (key == "tags") return def;
        }
        return QJsonObject{{"key", "tags"}, {"name", "Etiquetas"}, {"type", "object"},
                           {"target_type", "tag"}, {"multiple", true}};
}

// Fila de etiquetas bajo el título: asignadas (propiedad tags) que abren el
// objeto Etiqueta, mencionadas en el contenido que saltan al bloque con
// focusBlock, y acceso al selector para añadir o quitar.
void MainWindow::refreshTagChips(QWidget *row, RichEditor *edit,
                                 const std::function<QJsonObject()> &getObject,
                                 const std::function<void(const QJsonObject &)> &apply,
                                 const std::function<void()> &dirty) {
        auto *layout = qobject_cast<QHBoxLayout *>(row->layout());
        if (!layout || !getObject || !apply) return;
        const auto object = getObject();
        const auto props = object.value("properties").toObject();
        QStringList assigned;
        for (const auto &v : props.value("tags").toArray())
            if (!v.toString().isEmpty()) assigned << v.toString();
        QList<RichEditor::TagMention> mentions;
        QStringList mentionSignature;
        if (edit)
            for (const auto &m : edit->tagMentions())
                if (!assigned.contains(m.id)) {
                    mentions.append(m);
                    mentionSignature << m.id + ":" + m.blockId;
                }
        // Reconstruir sólo cuando cambia el conjunto visible: textChanged dispara
        // por cada pulsación y los chips no deben parpadear al escribir.
        const QString signature = assigned.join(",") + "|" + mentionSignature.join(",");
        if (row->property("tagSignature").toString() == signature) return;
        row->setProperty("tagSignature", signature);
        clearLayout(layout);
        auto chipTitle = [this](const QString &id) {
            for (const auto &v : objects)
                if (str(v.toObject(), "id") == id) return str(v.toObject(), "title");
            return QString();
        };
        // Etiquetas asignadas: abren el objeto Etiqueta.
        for (const auto &id : assigned) {
            const QString title = chipTitle(id);
            if (title.isEmpty()) continue;
            auto *chip = button(title, "tag", "tagChip");
            chip->setToolTip("Abrir etiqueta");
            chip->setFixedHeight(26);
            connect(chip, &QPushButton::clicked, this, [this, id] { openPreview(id); });
            layout->addWidget(chip);
        }
        // Mencionadas dentro del contenido: llevan al bloque y lo resaltan.
        for (const auto &m : mentions) {
            const QString title = chipTitle(m.id);
            if (title.isEmpty()) continue;
            auto *chip = button(title, "tag", "tagMentionChip");
            chip->setToolTip("Mencionada en esta nota · ir al bloque");
            chip->setFixedHeight(26);
            const QPointer<RichEditor> editGuard(edit);
            const QString blockId = m.blockId;
            connect(chip, &QPushButton::clicked, chip, [editGuard, blockId] {
                if (editGuard) editGuard->focusBlock(blockId);
            });
            layout->addWidget(chip);
        }
        auto *add = button(assigned.isEmpty() && mentions.isEmpty() ? "Etiqueta" : "", "plus", "quiet");
        add->setToolTip("Añadir o quitar etiquetas");
        add->setAccessibleName("Añadir etiquetas");
        add->setFixedSize(26, 26);
        const QPointer<QWidget> rowGuard(row);
        connect(add, &QPushButton::clicked, this, [this, rowGuard, edit, getObject, apply, dirty] {
            const auto currentObject = getObject();
            QJsonValue result;
            if (!LumenProperties::chooseRelationValue(this, "Elegir Etiquetas", objects, types,
                                                      tagsDefinitionFor(typeFor(str(currentObject, "type_id"))),
                                                      currentObject.value("properties").toObject().value("tags"),
                                                      str(currentObject, "id"), result))
                return;
            auto o = currentObject;
            auto p = o.value("properties").toObject();
            if (result.isNull() || (result.isArray() && result.toArray().isEmpty()))
                p.remove("tags");
            else
                p["tags"] = result;
            o["properties"] = p;
            apply(o);
            if (rowGuard) refreshTagChips(rowGuard, edit, getObject, apply, dirty);
            if (dirty) dirty();
        });
        layout->addWidget(add);
        layout->addStretch();
        row->setVisible(true);
}

void MainWindow::openPreview(QString id, QString blockId) {
        auto known = objectFor(id);
        if (str(known, "type_id") == "query") {
            openObject(id);
            return;
        }
        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            newTab(id);
            return;
        }
        if (!canLeave([this, id, blockId] {
                openPreview(id, blockId);
            }))
            return;
        const QPointer<QWidget> focusOrigin = focusWidget();
        const auto originNavigation = navigationGeneration;
        api.get("/v1/objects/" + id, [this, id, blockId, focusOrigin, originNavigation](QJsonValue v, QString err) {
            if (originNavigation != navigationGeneration) return;
            if (!err.isEmpty()) {
                error(err);
                return;
            }
            auto object = v.toObject();
            rememberObject(object);
            rememberRecent(id);
            auto *d = new GuardedDialog(this);
            d->setObjectName("objectPreview");
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->setWindowTitle(str(object, "title"));
            d->setWindowModality(Qt::WindowModal);
            d->setSizeGripEnabled(true);
            const auto properties = object.value("properties").toObject();
            const bool isImage = str(object, "type_id") == "image";
            QString imageAssetId;
            if (isImage) {
                imageAssetId = properties.value("image_asset_id").toString();
                if (imageAssetId.isEmpty()) imageAssetId = properties.value("asset_id").toString();
                if (imageAssetId.isEmpty()) {
                    for (auto av : properties.value("assets").toArray()) {
                        auto a = av.toObject();
                        if (a.value("mime").toString().startsWith("image/")) {
                            imageAssetId = str(a, "id");
                            break;
                        }
                    }
                }
            }

            d->resize(isImage ? qBound(850, width() - 80, 1180) : qBound(650, width() - 150, 980),
                      isImage ? qBound(550, height() - 80, 780) : qBound(480, height() - 100, 760));
            auto *outer = new QVBoxLayout(d);
            outer->setContentsMargins(15, 12, 15, 12);
            outer->setSpacing(8);
            // WrapBar (aliases stage): the preview header never clips its
            // chips either — same behavior as the object page header.
            auto *tools = new WrapBar;
            tools->setObjectName("previewChipsBar");
            auto typ = typeFor(str(object, "type_id"));
            auto typeChip = button(str(typ, "name"), "", "typeBadge");
            typeChip->setIcon(LumenIcons::typeIcon(typ, 24, dark));
            typeChip->setIconSize(QSize(24, 24));
            tools->addChip(typeChip);
            // D5 cue: previews of flagged objects carry the same Template
            // badge as the full page (read-only here).
            if (objectIsTemplate(object)) {
                auto *templateBadge = label("Template", "templateChip");
                templateBadge->setToolTip("This object is a template.");
                tools->addChip(templateBadge);
            }
            auto *colBtn = button("Colecciones", "network", "quiet");
            colBtn->setToolTip("Ver colecciones de este tipo");
            connect(colBtn, &QPushButton::clicked, d, [this, d, typ] {
                d->close();
                showCollection(str(typ, "id"));
            });
            tools->addChip(colBtn);
            QPointer<QToolButton> toggleDetails;
            if (isImage && !imageAssetId.isEmpty()) {
                auto *tb = new QToolButton(d);
                tb->setIcon(LumenIcons::icon("layout-sidebar-right", dark ? QColor("#e4e3e2") : QColor("#222224"), 20));
                tb->setCheckable(true);
                tb->setAutoRaise(true);
                tb->setToolTip("Alternar panel de propiedades · Alt+D");
                tb->setMinimumSize(30, 30);
                tools->addTrailingChip(tb);
                toggleDetails = tb;

                auto *moreBtn = new QToolButton(d);
                moreBtn->setIcon(LumenIcons::icon("dots", dark ? QColor("#e4e3e2") : QColor("#222224"), 20));
                moreBtn->setAutoRaise(true);
                moreBtn->setToolTip("Opciones del objeto");
                moreBtn->setMinimumSize(30, 30);
                tools->addTrailingChip(moreBtn);

                auto *moreMenu = new QMenu(d);
                moreMenu->addAction(LumenIcons::icon("copy"), "Copiar enlace al objeto", [id] {
                    QApplication::clipboard()->setText("lumen://object/" + id);
                });
                moreMenu->addAction(LumenIcons::icon("file-upload"), "Descargar imagen", [this, d, imageAssetId, object] {
                    api.bytes("/v1/assets/" + imageAssetId + "/content", [this, d, object](QByteArray bytes, QString err) {
                        if (bytes.isEmpty() || !err.isEmpty()) return;
                        const QString defaultName = str(object, "title").isEmpty() ? "imagen.png" : str(object, "title") + ".png";
                        const QString savePath = QFileDialog::getSaveFileName(d, "Descargar imagen", defaultName, "Imágenes (*.png *.jpg *.jpeg *.webp)");
                        if (!savePath.isEmpty()) {
                            QFile f(savePath);
                            if (f.open(QIODevice::WriteOnly)) f.write(bytes);
                        }
                    });
                });
                connect(moreBtn, &QToolButton::clicked, moreBtn, [moreBtn, moreMenu] {
                    moreMenu->exec(moreBtn->mapToGlobal(QPoint(0, moreBtn->height())));
                });
            }
            QPointer<QPushButton> previewCoverBtn;
            QPointer<QToolButton> objMoreBtn;
            if (!isImage) {
                QString curCoverId = properties.value("image_asset_id").toString();
                previewCoverBtn = button(!curCoverId.isEmpty() ? "Cambiar portada" : "Añadir portada", "photo", "quiet");
                tools->addTrailingChip(previewCoverBtn);

                objMoreBtn = new QToolButton(d);
                objMoreBtn->setIcon(LumenIcons::icon("dots", dark ? QColor("#e4e3e2") : QColor("#222224"), 20));
                objMoreBtn->setAutoRaise(true);
                objMoreBtn->setToolTip("Opciones del objeto");
                objMoreBtn->setMinimumSize(30, 30);
                tools->addTrailingChip(objMoreBtn);
            }
            auto *full = button("Abrir página", "arrows-maximize", "quiet");
            auto *close = button("Cerrar", "x", "quiet");
            tools->addTrailingChip(full);
            tools->addTrailingChip(close);
            outer->addWidget(tools);

            auto *scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            auto *host = new QWidget;
            auto *hl = new QHBoxLayout(host);
            hl->setContentsMargins(isImage ? 12 : 25, isImage ? 12 : 25, isImage ? 12 : 25, isImage ? 16 : 30);
            if (!isImage) hl->addStretch();
            auto *canvas = new QWidget;
            canvas->setMaximumWidth(isImage ? 420 : 708);
            auto *l = new QVBoxLayout(canvas);
            l->setContentsMargins(0, 0, 0, 0);
            l->setSpacing(14);
            hl->addWidget(canvas, 1);
            if (!isImage) hl->addStretch();
            scroll->setWidget(host);

            if (isImage && !imageAssetId.isEmpty()) {
                auto *splitRow = new QHBoxLayout;
                splitRow->setContentsMargins(0, 0, 0, 0);
                splitRow->setSpacing(10);

                auto *mediaArea = new QWidget(d);
                auto *mediaLayout = new QVBoxLayout(mediaArea);
                mediaLayout->setContentsMargins(0, 0, 0, 0);
                mediaLayout->setSpacing(6);

                auto *imageScroll = new QScrollArea(mediaArea);
                imageScroll->setObjectName("previewImageScroll");
                imageScroll->setAlignment(Qt::AlignCenter);
                imageScroll->setWidgetResizable(false);
                imageScroll->setFrameShape(QFrame::NoFrame);
                imageScroll->setStyleSheet(QString("QScrollArea{background:%1;border-radius:8px;}").arg(dark ? "#0f0f12" : "#f4f4f6"));

                auto *imageLabel = new QLabel(imageScroll);
                imageLabel->setObjectName("previewImageLabel");
                imageLabel->setAlignment(Qt::AlignCenter);
                imageLabel->setAccessibleName(str(object, "title"));
                imageScroll->setWidget(imageLabel);
                mediaLayout->addWidget(imageScroll, 1);

                auto *pillHost = new QWidget(mediaArea);
                auto *pillLayout = new QHBoxLayout(pillHost);
                pillLayout->setContentsMargins(0, 4, 0, 4);
                pillLayout->setAlignment(Qt::AlignCenter);

                auto *pill = new QFrame(pillHost);
                pill->setObjectName("previewZoomPill");
                pill->setStyleSheet(QString(
                    "#previewZoomPill{background:%1;border:1px solid %2;border-radius:18px;padding:2px 8px;}"
                    "QToolButton{border:0;padding:4px;border-radius:12px;background:transparent;}"
                    "QToolButton:hover{background:%3;}"
                ).arg(dark ? "#1b1b1f" : "#ffffff", dark ? "#29292e" : "#e0dfdf", dark ? "#2a2a30" : "#ececec"));

                auto *pl = new QHBoxLayout(pill);
                pl->setContentsMargins(6, 2, 6, 2);
                pl->setSpacing(6);

                auto *zoomVal = new QLabel("100 %", pill);
                zoomVal->setStyleSheet("font-weight:600;font-size:12px;padding:0 4px;");
                auto *zoomOut = new QToolButton(pill);
                zoomOut->setIcon(LumenIcons::icon("zoom-out", dark ? QColor("#e4e3e2") : QColor("#222224"), 16));
                zoomOut->setToolTip("Alejar");
                auto *zoomIn = new QToolButton(pill);
                zoomIn->setIcon(LumenIcons::icon("zoom-in", dark ? QColor("#e4e3e2") : QColor("#222224"), 16));
                zoomIn->setToolTip("Acercar");
                auto *downloadTool = new QToolButton(pill);
                downloadTool->setIcon(LumenIcons::icon("file-upload", dark ? QColor("#e4e3e2") : QColor("#222224"), 16));
                downloadTool->setToolTip("Descargar imagen");
                auto *infoTool = new QLabel(pill);
                infoTool->setStyleSheet("font-size:11px;color:#919097;padding:0 4px;");

                pl->addWidget(zoomVal);
                pl->addWidget(zoomOut);
                pl->addWidget(zoomIn);
                pl->addWidget(downloadTool);
                pl->addWidget(infoTool);
                pillLayout->addWidget(pill);
                mediaLayout->addWidget(pillHost);

                splitRow->addWidget(mediaArea, 1);
                splitRow->addWidget(scroll, 0);
                scroll->setFixedWidth(380);
                outer->addLayout(splitRow, 1);

                auto originalImg = std::make_shared<QPixmap>();
                auto zoomLevel = std::make_shared<double>(1.0);
                auto fitMode = std::make_shared<bool>(true);

                auto updatePreviewImg = [imageScroll, imageLabel, zoomVal, originalImg, zoomLevel, fitMode] {
                    if (originalImg->isNull()) return;
                    const auto avail = imageScroll->viewport()->size() - QSize(16, 16);
                    if (*fitMode) {
                        *zoomLevel = qMin(double(avail.width()) / originalImg->width(),
                                          double(avail.height()) / originalImg->height());
                    }
                    *zoomLevel = qBound(0.05, *zoomLevel, 6.0);
                    const QSize target(qMax(1, qRound(originalImg->width() * *zoomLevel)),
                                       qMax(1, qRound(originalImg->height() * *zoomLevel)));
                    imageLabel->setPixmap(originalImg->scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    imageLabel->resize(target);
                    zoomVal->setText(QString("%1 %").arg(qRound(*zoomLevel * 100)));
                };

                connect(zoomIn, &QToolButton::clicked, d, [zoomLevel, fitMode, updatePreviewImg] {
                    *fitMode = false;
                    *zoomLevel = qBound(0.05, *zoomLevel * 1.25, 6.0);
                    updatePreviewImg();
                });
                connect(zoomOut, &QToolButton::clicked, d, [zoomLevel, fitMode, updatePreviewImg] {
                    *fitMode = false;
                    *zoomLevel = qBound(0.05, *zoomLevel / 1.25, 6.0);
                    updatePreviewImg();
                });
                connect(downloadTool, &QToolButton::clicked, d, [this, d, imageAssetId, object] {
                    api.bytes("/v1/assets/" + imageAssetId + "/content", [this, d, object](QByteArray bytes, QString err) {
                        if (bytes.isEmpty() || !err.isEmpty()) return;
                        const QString defaultName = str(object, "title").isEmpty() ? "imagen.png" : str(object, "title") + ".png";
                        const QString savePath = QFileDialog::getSaveFileName(d, "Descargar imagen", defaultName, "Imágenes (*.png *.jpg *.jpeg *.webp)");
                        if (!savePath.isEmpty()) {
                            QFile f(savePath);
                            if (f.open(QIODevice::WriteOnly)) f.write(bytes);
                        }
                    });
                });

                if (imageMemoryCache_.contains(id)) {
                    *originalImg = imageMemoryCache_.value(id);
                    infoTool->setText(QString("%1×%2 px").arg(originalImg->width()).arg(originalImg->height()));
                    QTimer::singleShot(0, d, updatePreviewImg);
                } else {
                    imageLabel->setText("Cargando imagen…");
                }
                api.bytes("/v1/assets/" + imageAssetId + "/content", [this, id, d, imageLabel, infoTool, originalImg, updatePreviewImg](QByteArray bytes, QString err) {
                    if (!d) return;
                    if (!err.isEmpty() || bytes.isEmpty()) {
                        if (originalImg->isNull()) imageLabel->setText("No se pudo cargar la imagen.");
                        return;
                    }
                    originalImg->loadFromData(bytes);
                    if (!originalImg->isNull()) {
                        imageMemoryCache_[id] = *originalImg;
                        infoTool->setText(QString("%1×%2 px").arg(originalImg->width()).arg(originalImg->height()));
                        QTimer::singleShot(0, d, updatePreviewImg);
                    } else if (originalImg->isNull()) {
                        imageLabel->setText("Formato de imagen no compatible.");
                    }
                });

                if (toggleDetails) {
                    connect(toggleDetails, &QToolButton::toggled, scroll, [scroll, d, updatePreviewImg](bool checked) {
                        scroll->setVisible(checked);
                        QTimer::singleShot(30, d, updatePreviewImg);
                    });
                    toggleDetails->setChecked(false);
                    scroll->hide();
                    new QShortcut(QKeySequence("Alt+D"), d, [toggleDetails] {
                        if (toggleDetails) toggleDetails->toggle();
                    });
                }
            } else {
                outer->addWidget(scroll, 1);
            }

            auto state = std::make_shared<PreviewSession>();
            state->object = object;
            QPointer<QLabel> previewCoverLabel;
            if (!isImage) {
                previewCoverLabel = new QLabel;
                previewCoverLabel->setObjectName("ogCover");
                previewCoverLabel->setAlignment(Qt::AlignCenter);
                previewCoverLabel->setMinimumHeight(120);
                previewCoverLabel->setMaximumHeight(200);
                previewCoverLabel->setStyleSheet("border-radius:8px;");
                l->addWidget(previewCoverLabel);
                QString curCoverId = properties.value("image_asset_id").toString();
                if (curCoverId.isEmpty()) {
                    previewCoverLabel->hide();
                } else {
                    QPointer<QLabel> guard = previewCoverLabel;
                    api.bytes("/v1/assets/" + curCoverId + "/content", [guard](QByteArray data, QString err) {
                        QPixmap px;
                        if (guard && err.isEmpty() && px.loadFromData(data))
                            guard->setPixmap(px.scaled(qMax(200, guard->width()), guard->maximumHeight(),
                                                      Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                    });
                }
                if (previewCoverBtn) {
                    connect(previewCoverBtn, &QPushButton::clicked, d, [this, previewCoverBtn, state, previewCoverLabel] {
                        showCoverMenu(previewCoverBtn, state, previewCoverLabel, previewCoverBtn);
                    });
                }
                if (objMoreBtn) {
                    connect(objMoreBtn, &QToolButton::clicked, d, [this, objMoreBtn, state, previewCoverLabel, previewCoverBtn, canvas] {
                        showObjectOptionsMenu(objMoreBtn, state, previewCoverLabel, previewCoverBtn, canvas);
                    });
                }
            }
            state->heading = new DocumentTitle(str(object, "title"));
            state->heading->setObjectName("documentTitle");
            l->addWidget(state->heading);
            auto *tagRow = new QWidget(canvas);
            tagRow->setObjectName("tagChipsRow");
            auto *tagRowLayout = new QHBoxLayout(tagRow);
            tagRowLayout->setContentsMargins(0, 0, 0, 0);
            tagRowLayout->setSpacing(6);
            tagRowLayout->addStretch();
            l->addWidget(tagRow);
            auto *props = new LumenProperties;
            props->setTheme(dark);
            props->setData(object, typeFor(str(object, "type_id")), objects, types);
            l->addWidget(props);
            QPointer<LumenProperties> propsGuard(props);
            auto assets = properties.value("assets").toArray();
            for (auto av : assets) {
                auto a = av.toObject();
                auto *b = button(str(a, "name"), str(object, "type_id") == "pdf" ? "file-type-pdf" : "photo",
                                 "reference");
                connect(b, &QPushButton::clicked, d, [this, a] {
                    previewAsset(str(a, "id"), a);
                });
                l->addWidget(b);
            }
            state->edit = new RichEditor;
            state->edit->setObjectName("previewEditor");
            wireEditor(state->edit, id, true);
            QFont f("Inter");
            f.setPixelSize(fontSize);
            state->edit->setFont(f);
            loadEditor(state->edit, object);
            state->edit->setMinimumHeight(220);
            l->addWidget(state->edit, 1);

            auto *backlinksBox = new QWidget(canvas);
            auto *bbl = new QVBoxLayout(backlinksBox);
            bbl->setContentsMargins(0, 10, 0, 0);
            bbl->setSpacing(4);
            bbl->addWidget(label("Enlaces entrantes", "section"));
            int backlinkCount = 0;
            const QString targetId = str(object, "id");
            const QString originId = properties.value("source_object_id").toString();
            for (const auto &val : objects) {
                auto o = val.toObject();
                const QString oId = str(o, "id");
                if (oId == targetId) continue;
                if (allLinks(o).contains(targetId) || (!originId.isEmpty() && oId == originId)) {
                    ++backlinkCount;
                    auto oType = typeFor(str(o, "type_id"));
                    auto *b = button(str(o, "title"), str(oType, "icon"), "reference");
                    b->setIcon(LumenIcons::typeIcon(oType, 24, dark));
                    b->setIconSize(QSize(24, 24));
                    if (str(o, "type_id") == "journal") {
                        b->setText(b->text() + "  [Nota diaria]");
                    }
                    connect(b, &QPushButton::clicked, d, [this, d, o] {
                        d->close();
                        if (str(o, "type_id") == "journal") {
                            navigateJournal(QDate::fromString(str(o, "id"), Qt::ISODate));
                        } else {
                            openObject(str(o, "id"));
                        }
                    });
                    bbl->addWidget(b);
                    const QString snippet = referenceSnippet(o, targetId, str(object, "title"));
                    if (!snippet.isEmpty()) {
                        auto *snip = new QLabel(snippet, backlinksBox);
                        snip->setWordWrap(true);
                        snip->setStyleSheet(QString("color:%1;font-size:12px;margin-left:28px;margin-bottom:6px;").arg(dark ? "#a5a4ac" : "#6c6b75"));
                        bbl->addWidget(snip);
                    }
                }
            }
            if (backlinkCount == 0) {
                bbl->addWidget(label("Sin menciones ni enlaces entrantes.", "muted"));
            }
            l->addWidget(backlinksBox);

            state->timer = new QTimer(d);
            state->timer->setSingleShot(true);
            state->timer->setInterval(450);
            state->status = label(saveState(object), "muted");
            outer->addWidget(state->status);
            auto change = [state] {
                state->dirty = true;
                state->status->setText("Guardando…");
                state->timer->start();
            };
            connect(state->edit, &QTextEdit::textChanged, d, change);
            connect(state->heading, &QTextEdit::textChanged, d, change);
            std::function<void()> refreshTags = [this, guard = QPointer<QWidget>(tagRow), state, propsGuard, change] {
                if (!guard) return;
                refreshTagChips(guard, state->edit,
                                [state] { return state->object; },
                                [this, state, propsGuard](const QJsonObject &o) {
                                    state->object = o;
                                    if (propsGuard)
                                        propsGuard->setData(o, typeFor(str(o, "type_id")), objects, types);
                                },
                                [change] { change(); });
            };
            props->onChange = [state, change, refreshTags](QString key, QJsonValue value) {
                auto p = state->object.value("properties").toObject();
                p[key] = value;
                if (key == "summary") p.remove("description");
                state->object["properties"] = p;
                refreshTags();
                change();
            };
            refreshTags();
            connect(state->edit, &QTextEdit::textChanged, d, refreshTags);
            props->onAddProperty = [this, object] {
                propertyDialog(str(object, "type_id"));
            };
            props->onEditType = props->onAddProperty;
            QPointer<GuardedDialog> guard = d;
            connect(state->timer, &QTimer::timeout, d, [this, guard, state] {
                savePreview(guard, state);
            });
            d->mayClose = [this, guard, state] {
                if (state->dirty || state->saving) {
                    state->closing = true;
                    savePreview(guard, state);
                    return false;
                }
                return true;
            };
            connect(close, &QPushButton::clicked, d, &QDialog::close);
            connect(full, &QPushButton::clicked, d, [this, guard, state, id] {
                if (state->dirty || state->saving) {
                    state->afterClose = [this, id] {
                        openObject(id);
                    };
                    state->closing = true;
                    savePreview(guard, state);
                    return;
                }
                if (guard)
                    guard->close();
                openObject(id);
            });
            connect(typeChip, &QPushButton::clicked, d, [this, guard, state, typ] {
                auto next = [this, typ] { showCollection(str(typ, "id")); };
                if (state->dirty || state->saving) {
                    state->afterClose = next;
                    state->closing = true;
                    savePreview(guard, state);
                    return;
                }
                if (guard) guard->close();
                next();
            });
            connect(d, &QDialog::finished, this, [this, state, id] {
                if (str(current, "id") == id && !dirty && !saving) {
                    rememberPosition();
                    current = state->object;
                    renderEditor(route == "journal");
                }
                if (activeViews)
                    activeViews->setData(objects, types);
                if (graphCanvas)
                    graphCanvas->setData(objects, types);
                if (calendarOverview)
                    calendarOverview->setData(objects, types);
                rebuildSidebar();
            });
            connect(d, &QObject::destroyed, this, [this, focusOrigin, originNavigation] {
                QTimer::singleShot(0, this, [this, focusOrigin, originNavigation] {
                    if (originNavigation != navigationGeneration || QApplication::activeModalWidget())
                        return;
                    QWidget *target = focusOrigin && focusOrigin->isVisible() ? focusOrigin.data()
                                      : editor && editor->isVisible() ? editor : nullptr;
                    if (target) {
                        activateWindow();
                        target->setFocus(Qt::OtherFocusReason);
                    }
                });
            });
            d->show();
            if (!blockId.isEmpty())
                QTimer::singleShot(0, d, [state, blockId] {
                    state->edit->focusBlock(blockId);
                });
        });
    }

void MainWindow::propertyDialog(QString typeId) {
        auto typ = typeFor(typeId);
        if (typ.isEmpty())
            return;
        QDialog d(this);
        d.setObjectName("typeSettingsDialog");
        d.setWindowTitle("Propiedades de " + str(typ, "name"));
        d.resize(580, 640);
        auto *l = new QVBoxLayout(&d);
        l->addWidget(label("Tipo de objeto", "dialogTitle"));
        auto *name = new QLineEdit(str(typ, "name"));
        name->setPlaceholderText("Nombre del tipo");
        l->addWidget(name);
        auto *color = new QPushButton("Color del tipo");
        QString chosen = str(typ, "color");
        connect(color, &QPushButton::clicked, &d, [&] {
            auto c = QColorDialog::getColor(QColor(chosen), &d, "Color del tipo");
            if (c.isValid()) {
                chosen = c.name();
                color->setStyleSheet("color:" + chosen + ";");
            }
        });
        l->addWidget(color);
        // D5: the type's default template (contracts/SYNC.md "Default template
        // per type"). This dialog is the type settings UI; the picker offers
        // "None" plus the type's templates and PATCHes default_template_id.
        auto *defaultRow = new QWidget;
        defaultRow->setObjectName("typeDefaultTemplateRow");
        auto *defaultLayout = new QHBoxLayout(defaultRow);
        defaultLayout->setContentsMargins(0, 0, 0, 0);
        defaultLayout->setSpacing(8);
        defaultLayout->addWidget(label("Default template"));
        auto *defaultTemplate = new QComboBox;
        defaultTemplate->setObjectName("typeDefaultTemplate");
        defaultTemplate->setIconSize(QSize(22, 22));
        defaultTemplate->setAccessibleName("Default template");
        defaultTemplate->setToolTip("New objects of this type can start from this template. The "
                                    "journal applies it when a daily note is created.");
        fillTemplateCombo(defaultTemplate, typeId, "default", str(typ, "default_template_id"));
        requestTemplates(typeId);
        defaultLayout->addWidget(defaultTemplate, 1);
        l->addWidget(defaultRow);
        auto *list = new QListWidget;
        auto schema = std::make_shared<QJsonArray>(typ.value("properties").toArray());
        auto fill = [list, schema] {
            list->clear();
            for (auto v : *schema) {
                auto o = v.toObject();
                list->addItem(v.isString() ? v.toString()
                                           : o.value("name").toString(o.value("id").toString()));
            }
        };
        fill();
        l->addWidget(list, 1);
        auto *propName = new QLineEdit;
        propName->setPlaceholderText("Nombre de una propiedad");
        l->addWidget(propName);
        auto *kind = new QComboBox;
        for (auto p : QList<QPair<QString, QString>>{{"Texto", "text"},
                                                     {"Número", "number"},
                                                     {"Fecha", "date"},
                                                     {"Casilla", "boolean"},
                                                     {"Opciones", "select"},
                                                     {"Objeto relacionado", "object"}})
            kind->addItem(p.first, p.second);
        l->addWidget(kind);
        auto *options = new QLineEdit;
        options->setPlaceholderText("Opciones separadas por comas");
        l->addWidget(options);
        auto *target = new QComboBox;
        target->addItem("Cualquier tipo", "");
        for (auto v : types)
            target->addItem(str(v.toObject(), "name"), str(v.toObject(), "id"));
        l->addWidget(target);
        auto *multiple = new QCheckBox("Permitir varios objetos");
        l->addWidget(multiple);
        auto selected = std::make_shared<int>(-1);
        connect(list, &QListWidget::itemDoubleClicked, &d, [=](QListWidgetItem *) {
            *selected = list->currentRow();
            auto v = (*schema)[*selected];
            auto p = v.toObject();
            propName->setText(v.isString() ? v.toString() : p.value("name").toString());
            kind->setCurrentIndex(qMax(0, kind->findData(p.value("type").toString("text"))));
            QStringList os;
            for (auto x : p.value("options").toArray())
                os << x.toString();
            options->setText(os.join(", "));
            target->setCurrentIndex(qMax(0, target->findData(p.value("target_type").toString())));
            multiple->setChecked(p.value("multiple").toBool());
        });
        auto *add = button("Añadir o actualizar propiedad", "plus", "quiet");
        l->addWidget(add);
        auto *remove = button("Quitar propiedad seleccionada", "x", "quiet");
        l->addWidget(remove);
        connect(remove, &QPushButton::clicked, &d, [=] {
            const int row = list->currentRow();
            if (row < 0 || row >= schema->size())
                return;
            schema->removeAt(row);
            *selected = -1;
            propName->clear();
            options->clear();
            fill();
        });
        connect(add, &QPushButton::clicked, &d, [=] {
            auto title = propName->text().trimmed();
            if (title.isEmpty())
                return;
            QString key = title.toLower().normalized(QString::NormalizationForm_D);
            key.remove(QRegularExpression("[\\x{0300}-\\x{036f}]"));
            key.replace(QRegularExpression("[^a-z0-9_]+"), "_");
            if (key.isEmpty() || key[0].isDigit())
                key = "prop_" + key;
            key = key.left(64);
            if (*selected >= 0) {
                auto old = (*schema)[*selected];
                key = old.isString()
                          ? old.toString()
                          : old.toObject().value("id").toString(old.toObject().value("name").toString());
            }
            QJsonObject property{{"id", key}, {"name", title}, {"type", kind->currentData().toString()}};
            if (kind->currentData() == "select") {
                QJsonArray values;
                for (auto s : options->text().split(',', Qt::SkipEmptyParts))
                    values.append(s.trimmed());
                property["options"] = values;
            }
            if (kind->currentData() == "object") {
                property["multiple"] = multiple->isChecked();
                if (!target->currentData().toString().isEmpty())
                    property["target_type"] = target->currentData().toString();
            }
            if (*selected >= 0)
                (*schema)[*selected] = property;
            else
                schema->append(property);
            *selected = -1;
            propName->clear();
            options->clear();
            fill();
        });
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        l->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted)
            return;
        api.patch("/v1/types/" + typeId,
                  {{"name", name->text().trimmed()}, {"color", chosen}, {"properties", *schema},
                   // An empty string clears the reference (contract).
                   {"default_template_id", defaultTemplate->currentData().toString()}},
                  [this, typeId](QJsonValue v, QString err) {
                      if (!err.isEmpty()) {
                          error(err);
                          return;
                      }
                      for (int i = 0; i < types.size(); ++i)
                          if (str(types[i].toObject(), "id") == typeId)
                              types[i] = v.toObject();
                      rebuildSidebar();
                      if (editor && !dirty && !saving)
                          renderEditor(route == "journal");
                  });
    }

void MainWindow::importCollectionFiles(QString type) {
        if (!canLeave([this, type] { importCollectionFiles(type); })) return;
        const auto paths = QFileDialog::getOpenFileNames(
            this, "Importar archivos", {},
            "Notas, imágenes y PDF (*.md *.txt *.pdf *.png *.jpg *.jpeg *.webp *.gif)");
        if (paths.isEmpty()) return;
        auto batch = std::make_shared<CollectionImport>();
        batch->paths = paths;
        batch->type = type;
        importCollectionNext(batch);
    }

void MainWindow::importCollectionNext(std::shared_ptr<CollectionImport> batch) {
        if (batch->index >= batch->paths.size()) {
            api.get("/v1/objects", [this, batch](QJsonValue value, QString err) {
                if (!err.isEmpty()) { error(err); return; }
                for (auto object : arr(value)) rememberObject(object.toObject());
                if (activeViews) activeViews->setData(objects, types);
                rebuildSidebar();
                saveLabel->setText(QString("%1 archivo(s) importado(s)").arg(batch->imported));
                if (!batch->errors.isEmpty()) error(batch->errors.join("\n"));
            });
            return;
        }
        const QString path = batch->paths.at(batch->index++);
        const QFileInfo file(path);
        saveLabel->setText(QString("Importando %1 de %2…").arg(batch->index).arg(batch->paths.size()));
        const auto suffix = file.suffix().toLower();
        const bool text = suffix == "md" || suffix == "txt";
        auto finish = [this, batch, name = file.fileName()](QString err) {
            if (!err.isEmpty()) batch->errors.append(name + ": " + err);
            else ++batch->imported;
            importCollectionNext(batch);
        };
        if (text) {
            QFile input(path);
            if (input.size() > 10 * 1024 * 1024 || !input.open(QIODevice::ReadOnly)) {
                finish("No se pudo leer la nota (máximo 10 MB)."); return;
            }
            QString type = batch->type;
            if (type.isEmpty() || type == "journal" || type == "query" || type == "image" || type == "pdf") type = "note";
            api.post("/v1/objects", {{"title", file.completeBaseName()}, {"type_id", type},
                                   {"document", LumenDocument::fromText(QString::fromUtf8(input.readAll()))},
                                   {"properties", QJsonObject{}}, {"links", QJsonArray{}}},
                     [this, finish](QJsonValue value, QString err) {
                         if (err.isEmpty()) rememberObject(value.toObject());
                         finish(err);
                     });
            return;
        }
        const bool standalone = batch->type.isEmpty() || batch->type == "image" || batch->type == "pdf";
        auto upload = [this, path, finish](QString origin) {
            api.upload(origin, path, [finish](QJsonValue, QString err) { finish(err); });
        };
        if (standalone) upload({});
        else {
            const QString type = batch->type == "query" || batch->type == "journal" ? "note" : batch->type;
            api.post("/v1/objects", {{"title", file.completeBaseName()}, {"type_id", type},
                                   {"document", LumenDocument::empty()}, {"properties", QJsonObject{}}, {"links", QJsonArray{}}},
                     [this, upload, finish](QJsonValue value, QString err) {
                         if (!err.isEmpty()) { finish(err); return; }
                         rememberObject(value.toObject());
                         upload(str(value.toObject(), "id"));
                     });
        }
    }

void MainWindow::importMedia(QString type) {
        auto path = QFileDialog::getOpenFileName(
            this, type == "pdf" ? "Importar PDF" : "Importar imagen", {},
            type == "pdf" ? "PDF (*.pdf)" : "Imágenes (*.png *.jpg *.jpeg *.webp *.gif)");
        if (path.isEmpty())
            return;
        saveLabel->setText("Guardando archivo…");
        api.upload("", path, [this](QJsonValue v, QString e) {
            if (!e.isEmpty()) {
                error(e);
                return;
            }
            auto id = v.toObject().value("media_object_id").toString();
            api.get("/v1/objects/" + id, [this](QJsonValue object, QString err) {
                if (!err.isEmpty()) {
                    error(err);
                    return;
                }
                rememberObject(object.toObject());
                rebuildSidebar();
                openObject(str(object.toObject(), "id"));
            });
        });
    }

void MainWindow::format(QString what) {
        if (!editor || navigationPending)
            return;
        if (what == "Título") {
            editor->applyBlockStyle("heading2");
            return;
        }
        if (what == "Lista") {
            editor->applyBlockStyle("bullet");
            return;
        }
        if (what == "Cita") {
            editor->applyBlockStyle("quote");
            return;
        }
        auto c = editor->textCursor();
        QTextCharFormat f;
        if (what == "Negrita") {
            f.setFontWeight(c.charFormat().fontWeight() == QFont::Bold ? QFont::Normal : QFont::Bold);
            c.mergeCharFormat(f);
        } else if (what == "Cursiva") {
            f.setFontItalic(!c.charFormat().fontItalic());
            c.mergeCharFormat(f);
        } else if (what == "Título") {
            QTextBlockFormat b;
            b.setHeadingLevel(2);
            c.mergeBlockFormat(b);
            f.setFontPointSize(fontSize + 5);
            f.setFontWeight(QFont::DemiBold);
            c.mergeCharFormat(f);
        } else if (what == "Lista") {
            c.createList(QTextListFormat::ListDisc);
        } else if (what == "Cita") {
            QTextBlockFormat b;
            b.setLeftMargin(20);
            b.setBackground(QColor(dark ? "#333d38" : "#f5f5e9"));
            c.mergeBlockFormat(b);
        }
        editor->setTextCursor(c);
        editor->setFocus();
        markDirty();
    }

void MainWindow::slashMenu() {
        QMenu menu(this);
        for (QString s : {"Título", "Lista", "Cita", "Enlazar objeto", "Guardar URL", "Adjuntar archivo"})
            menu.addAction(s);
        auto *a = menu.exec(editor->mapToGlobal(editor->cursorRect().bottomLeft()));
        if (!a)
            return;
        QString t = a->text();
        if (t == "Enlazar objeto")
            linkObject();
        else if (t == "Guardar URL")
            captureUrl();
        else if (t == "Adjuntar archivo")
            attach();
        else
            format(t);
    }

void MainWindow::linkObject() {
        if (editor && !navigationPending)
            editor->showReferencePicker();
    }

void MainWindow::insertReference(const QJsonObject &object) {
        if (!editor)
            return;
        rememberObject(object);
        editor->setReferenceData(objects, types);
        editor->insertObjectReference(object);
        reconcileReferences();
        markDirty();
    }

void MainWindow::createdObject(const QJsonObject &object, const QString &originId, const QString &originRoute,                        quint64 generation) {
        rememberObject(object);
        if (originRoute == "journal" && !originId.isEmpty()) {
            if (!navigationPending && editor && route == "journal" && str(current, "id") == originId) {
                insertReference(object);
                saveCurrent();
            } else {
                // Only merge the relation; never send an old copy of the note's text.
                api.patch("/v1/objects/" + originId, {{"add_links", QJsonArray{str(object, "id")}}},
                          [this, originId](QJsonValue value, QString errorMessage) {
                              if (!errorMessage.isEmpty()) {
                                  error("El objeto se creó, pero no se pudo enlazar al diario: " +
                                        errorMessage);
                                  return;
                              }
                              for (int i = 0; i < objects.size(); ++i)
                                  if (str(objects[i].toObject(), "id") == originId)
                                      objects[i] = value.toObject();
                          });
            }
        } else if (generation == navigationGeneration) {
            openObject(str(object, "id"));
            // Crear con Ctrl+N/Enter debe dejar el cursor listo en la nota,
            // no en el botón del sidebar que abrió el diálogo.
            if (editor && editor->isVisible() && str(current, "id") == str(object, "id"))
                editor->setFocus(Qt::OtherFocusReason);
        }
        rebuildSidebar();
    }

void MainWindow::newObject(QString defaultType) {
        if (defaultType == "image" || defaultType == "pdf") {
            importMedia(defaultType);
            return;
        }
        QDialog d(this);
        d.setWindowTitle("Nuevo objeto");
        d.setObjectName("newObjectDialog");
        d.setMinimumWidth(480);
        auto *l = new QVBoxLayout(&d);
        l->addWidget(label("Dale un lugar a tu idea", "dialogTitle"));
        auto *name = new QLineEdit;
        name->setObjectName("newObjectTitle");
        name->setPlaceholderText("¿Cómo se llama?");
        l->addWidget(name);
        auto *combo = new QComboBox;
        combo->setObjectName("newObjectType");
        combo->setIconSize(QSize(24, 24));
        for (auto tv : types) {
            auto t = tv.toObject();
            if (str(t, "id") != "journal" && str(t, "id") != "collection")
                combo->addItem(LumenIcons::typeIcon(t, 24, dark), str(t, "name"), str(t, "id"));
        }
        combo->setCurrentIndex(qMax(0, combo->findData(defaultType.isEmpty() ? "note" : defaultType)));
        l->addWidget(combo);
        auto *templateRow = new QWidget;
        templateRow->setObjectName("newObjectTemplateRow");
        auto *templateLayout = new QHBoxLayout(templateRow);
        templateLayout->setContentsMargins(0, 0, 0, 0);
        templateLayout->setSpacing(8);
        templateLayout->addWidget(label("Start from template"));
        auto *templateCombo = new QComboBox;
        templateCombo->setObjectName("newObjectTemplate");
        templateCombo->setIconSize(QSize(22, 22));
        templateCombo->setAccessibleName("Start from template");
        templateCombo->setProperty("templateRow", QVariant::fromValue<QWidget *>(templateRow));
        fillTemplateCombo(templateCombo, combo->currentData().toString(), "create");
        requestTemplates(combo->currentData().toString());
        templateLayout->addWidget(templateCombo, 1);
        l->addWidget(templateRow);
        connect(combo, &QComboBox::currentIndexChanged, &d, [this, combo, templateCombo](int) {
            const QString typeId = combo->currentData().toString();
            // A fresh type always starts from a blank object; the picker
            // repopulates when that type's template listing lands.
            fillTemplateCombo(templateCombo, typeId, "create");
            requestTemplates(typeId);
        });
        auto *summary = new QLineEdit;
        summary->setPlaceholderText("Una primera idea, si quieres…");
        l->addWidget(summary);
        auto *b = button("Crear objeto", "document-new", "primary");
        b->setObjectName("newObjectCreate");
        b->setProperty("primaryAction", true);
        l->addWidget(b);
        connect(b, &QPushButton::clicked, &d, &QDialog::accept);
        connect(name, &QLineEdit::returnPressed, &d, &QDialog::accept);
        name->setFocus();
        if (d.exec() != QDialog::Accepted)
            return;
        if (combo->currentData() == "image" || combo->currentData() == "pdf") {
            importMedia(combo->currentData().toString());
            return;
        }
        // Creating from a template uses the template's own resolved title
        // ({date} variables included), so the title is only required for a
        // blank creation.
        const QString templateId = templateCombo->currentData().toString();
        if (!templateId.isEmpty()) {
            const auto originId = str(current, "id");
            const auto originRoute = route;
            const auto generation = navigationGeneration;
            createFromTemplate(templateId, name->text().trimmed(), originId, originRoute, generation);
            return;
        }
        if (name->text().trimmed().isEmpty())
            return;
        const auto selectedType = combo->currentData().toString();
        if (selectedType == "image" || selectedType == "pdf") { importMedia(selectedType); return; }
        const auto originId = str(current, "id");
        const auto originRoute = route;
        const auto generation = navigationGeneration;
        QJsonObject props;
        if (selectedType == "task") {
            props["status"] = "todo";
            if (originRoute == "journal")
                props["due_date"] = day.toString(Qt::ISODate);
        } else if (originRoute == "journal") {
            props["date"] = day.toString(Qt::ISODate);
        }
        api.post("/v1/objects",
                 {{"title", name->text().trimmed()},
                  {"type_id", selectedType},
                  {"document", LumenDocument::fromText(summary->text())},
                  {"properties", props},
                  {"links", QJsonArray{}}},
                 [this, originId, originRoute, generation](QJsonValue v, QString e) {
                     if (!e.isEmpty()) {
                         error(e);
                         return;
                     }
                     createdObject(v.toObject(), originId, originRoute, generation);
                 });
    }

void MainWindow::newType() {
        QDialog d(this);
        d.setWindowTitle("Nuevo tipo de objeto");
        d.setMinimumWidth(420);
        auto *l = new QVBoxLayout(&d);
        l->addWidget(label("Tu mundo no cabe en carpetas", "dialogTitle"));
        auto *name = new QLineEdit;
        name->setPlaceholderText("Curso, receta, película…");
        l->addWidget(name);
        auto *color = new QLineEdit("#7b91ae");
        l->addWidget(label("Color del tipo"));
        l->addWidget(color);
        l->addWidget(label("Copiar propiedades de"));
        auto *base = new QComboBox;
        base->addItem("Ninguno", "");
        for (auto v : types) {
            auto t = v.toObject();
            if (str(t, "id") != "journal" && str(t, "id") != "collection")
                base->addItem(LumenIcons::typeIcon(t, 24, dark), str(t, "name"), str(t, "id"));
        }
        l->addWidget(base);
        auto *b = button("Crear tipo", "list-add", "primary");
        l->addWidget(b);
        connect(b, &QPushButton::clicked, &d, &QDialog::accept);
        if (d.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
            return;
        QJsonArray properties;
        const QString baseId = base->currentData().toString();
        if (!baseId.isEmpty())
            properties = typeFor(baseId).value("properties").toArray();
        api.post("/v1/types",
                 {{"name", name->text().trimmed()},
                  {"color", color->text()},
                  {"icon", "text-x-generic"},
                  {"properties", properties}},
                 [this](QJsonValue v, QString e) {
                     if (!e.isEmpty()) {
                         error(e);
                         return;
                     }
                     const auto created = v.toObject();
                     types.append(created);
                     rebuildSidebar();
                     // Un tipo nuevo nace con reglas editables: copiar una base
                     // y abrir el editor evita tener que buscarlo en el sidebar.
                     propertyDialog(str(created, "id"));
                 });
    }

void MainWindow::commandPalette() {
        QDialog d(this);
        d.setObjectName("commandPalette");
        d.setWindowTitle("Buscar en Lumen");
        d.resize(660, 470);
        auto *layout = new QVBoxLayout(&d);
        layout->setContentsMargins(12, 12, 12, 10);
        layout->setSpacing(8);
        auto *search = new CommandInput;
        search->setPlaceholderText("Buscar contenido, tipos o acciones…");
        search->setMinimumHeight(41);
        layout->addWidget(search);
        auto *list = new QListWidget;
        list->setIconSize(QSize(24, 24));
        search->results = list;
        layout->addWidget(list, 1);
        layout->addWidget(label(
            "↑ ↓ elegir    Enter abrir    Ctrl+Enter nueva pestaña    Shift+Enter vista rápida    Esc cerrar",
            "muted"));
        // D6 (aliases): with a query typed, object rows come from
        // GET /v1/search — the server matcher (title, content, aliases)
        // with its matched_alias — instead of the local cache filter; a
        // result that matched through an alias carries the alias as a
        // right-aligned hint. The empty query keeps the instant local
        // listing; the local rows shown while the request is in flight
        // (and on a failed request) apply the same alias rule, so search
        // keeps working offline. Stale responses are dropped by a shared
        // generation counter that outlives the dialog.
        auto searchGeneration = std::make_shared<quint64>(0);
        auto attachAliasHint = [this, list](QListWidgetItem *item, const QString &alias) {
            auto *hint = new QLabel(alias, list);
            hint->setStyleSheet(QString("color:%1;font-size:12px;padding:2px 8px;")
                                    .arg(dark ? "#919097" : "#75747b"));
            hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            hint->setAttribute(Qt::WA_TransparentForMouseEvents);
            hint->setAccessibleName("Matched alias");
            list->setItemWidget(item, hint);
            item->setToolTip((item->toolTip().isEmpty() ? QString() : item->toolTip() + "\n") +
                             "Matched alias: " + alias);
        };
        auto appendObjectRow = [this, list, &attachAliasHint](const QJsonObject &o,
                                                              const QString &matchedAlias) {
            auto type = typeFor(str(o, "type_id"));
            auto *item = new QListWidgetItem(LumenIcons::typeIcon(type, 24, dark),
                                             str(o, "title") + "    ·    " + str(type, "name"), list);
            item->setData(Qt::UserRole, str(o, "id"));
            item->setToolTip(str(o, "content").left(280));
            if (!matchedAlias.isEmpty())
                attachAliasHint(item, matchedAlias);
        };
        // The client-side mirror of the server matcher: title and content
        // hits carry no alias hint (the title is the better link text),
        // an alias-only hit carries the alias.
        auto appendLocalObjects = [this, &appendObjectRow](const QString &q) {
            for (auto value : objects) {
                auto o = value.toObject();
                QString matchedAlias;
                if (!q.isEmpty() && !str(o, "title").contains(q, Qt::CaseInsensitive) &&
                    !LumenQuery::plainText(o).contains(q, Qt::CaseInsensitive)) {
                    matchedAlias = LumenAliases::matched(o, q);
                    if (matchedAlias.isEmpty())
                        continue;
                }
                appendObjectRow(o, matchedAlias);
            }
        };
        auto fill = [this, list, searchGeneration, &appendLocalObjects, &appendObjectRow](QString query) {
            list->clear();
            const auto q = query.trimmed();
            for (auto pair : QList<QPair<QString, QString>>{{"Calendario · hoy", "@journal"},
                                                            {"Nuevo objeto", "@new"},
                                                            {"Guardar un enlace", "@capture"},
                                                            {"Todos los objetos", "@all"},
                                                            {"Grafo de conexiones", "@graph"},
                                                            {"Apariencia y atajos", "@prefs"},
                                                            {"Nuevo tipo de objeto", "@type"}}) {
                if (q.isEmpty() || pair.first.contains(q, Qt::CaseInsensitive)) {
                    auto *item = new QListWidgetItem(icon(pair.second == "@journal" ? "calendar" : "plus"),
                                                     pair.first, list);
                    item->setData(Qt::UserRole, pair.second);
                }
            }
            for (auto value : types) {
                auto t = value.toObject();
                if (str(t, "id") == "journal" || str(t, "id") == "collection")
                    continue;
                if (q.isEmpty() || str(t, "name").contains(q, Qt::CaseInsensitive)) {
                    auto *item = new QListWidgetItem(LumenIcons::typeIcon(t, 24, dark), "Crear · " + str(t, "name"), list);
                    item->setData(Qt::UserRole, "@create:" + str(t, "id"));
                }
            }
            if (q.isEmpty()) {
                appendLocalObjects(q);
                if (list->count())
                    list->setCurrentRow(0);
                return;
            }
            // Instant local rows first; the server response replaces them
            // (it also matches weblink reader articles, which the local
            // cache filter does not).
            const int objectStart = list->count();
            appendLocalObjects(q);
            if (list->count())
                list->setCurrentRow(0);
            const auto generation = ++*searchGeneration;
            QUrlQuery params;
            params.addQueryItem("q", q);
            QPointer<QListWidget> resultList(list);
            api.get("/v1/search?" + params.toString(QUrl::FullyEncoded),
                    [resultList, searchGeneration, generation, objectStart, &appendObjectRow]
                    (QJsonValue value, QString err) {
                        // A newer keystroke answered, or the palette closed
                        // (the list dies with the stack dialog; a live guard
                        // implies the frame and its lambdas are alive).
                        if (!resultList || generation != *searchGeneration)
                            return;
                        QListWidget *list = resultList.data();
                        for (int i = list->count() - 1; i >= objectStart; --i)
                            delete list->takeItem(i);
                        if (!err.isEmpty())
                            return;  // the local rows already in place stand in
                        for (auto result : value.toArray()) {
                            auto o = result.toObject();
                            appendObjectRow(o, o.value("matched_alias").toString());
                        }
                        if (list->count() > objectStart)
                            list->setCurrentRow(objectStart);
                    });
        };
        fill("");
        connect(search, &QLineEdit::textChanged, &d, fill);
        QString id;
        Qt::KeyboardModifiers mode = Qt::NoModifier;
        auto accept = [&] {
            if (auto *item = list->currentItem()) {
                id = item->data(Qt::UserRole).toString();
                mode = QApplication::keyboardModifiers();
                d.accept();
            }
        };
        connect(search, &QLineEdit::returnPressed, &d, accept);
        connect(list, &QListWidget::itemActivated, &d, [accept](QListWidgetItem *) {
            accept();
        });
        search->setFocus();
        if (d.exec() != QDialog::Accepted)
            return;
        if (id == "@journal")
            navigateJournal(QDate::currentDate());
        else if (id == "@new")
            newObject();
        else if (id == "@capture")
            captureUrl();
        else if (id == "@all")
            showCollection("");
        else if (id == "@graph")
            showGraph();
        else if (id == "@prefs")
            preferences();
        else if (id == "@type")
            newType();
        else if (id.startsWith("@create:"))
            newObject(id.mid(8));
        else if (mode & Qt::ControlModifier)
            newTab(id);
        else if (mode & Qt::ShiftModifier)
            openPreview(id);
        else
            openObject(id);
    }

void MainWindow::captureUrl() {
        QDialog d(this);
        d.setWindowTitle("Guardar un enlace");
        d.setMinimumWidth(500);
        auto *l = new QVBoxLayout(&d);
        l->addWidget(label("Una fuente que merece quedarse", "dialogTitle"));
        auto *url = new QLineEdit;
        url->setPlaceholderText("https://…");
        auto *text = new QTextEdit;
        text->setPlaceholderText("¿Por qué quieres guardarlo?");
        text->setMaximumHeight(110);
        l->addWidget(url);
        l->addWidget(text);
        l->addWidget(label("El servidor obtiene la vista previa. No se envían tus notas ni cookies al sitio.",
                           "muted"));
        auto *b = button("Guardar en mi espacio", "insert-link", "primary");
        l->addWidget(b);
        connect(b, &QPushButton::clicked, &d, &QDialog::accept);
        connect(url, &QLineEdit::returnPressed, &d, &QDialog::accept);
        if (d.exec() != QDialog::Accepted || url->text().trimmed().isEmpty())
            return;
        const auto originId = str(current, "id");
        const auto originRoute = route;
        const auto generation = navigationGeneration;
        saveLabel->setText("Obteniendo vista previa…");
        api.post("/v1/capture",
                 {{"url", url->text().trimmed()},
                  {"selection", text->toPlainText()},
                  {"type_id", "web"},
                  {"date", day.toString(Qt::ISODate)},
                  {"source", "desktop"}},
                 [this, originId, originRoute, generation](QJsonValue v, QString e) {
                     if (!e.isEmpty()) {
                         error(e);
                         return;
                     }
                     auto o = v.toObject();
                     if (o.contains("object"))
                         o = o.value("object").toObject();
                     createdObject(o, originId, originRoute, generation);
                 });
    }

void MainWindow::attach() {
        if (navigationPending)
            return;
        if (current.isEmpty()) {
            error("Abre una nota para adjuntar un archivo.");
            return;
        }
        QString p = QFileDialog::getOpenFileName(
            this, "Adjuntar un archivo", QString(),
            "Fuentes (*.pdf *.png *.jpg *.jpeg *.webp *.txt);;Todos los archivos (*)");
        if (p.isEmpty())
            return;
        const auto id = str(current, "id");
        saveCurrent([this, p, id] {
            saveLabel->setText("Guardando archivo…");
            api.upload(id, p, [this, id](QJsonValue, QString e) {
                if (!e.isEmpty()) {
                    error(e);
                    return;
                }
                api.get("/v1/objects/" + id, [this, id](QJsonValue value, QString problem) {
                    if (!problem.isEmpty()) {
                        error(problem);
                        return;
                    }
                    rememberObject(value.toObject());
                    if (!navigationPending && str(current, "id") == id) {
                        if (!dirty && !saving) {
                            current = value.toObject();
                            renderEditor(route == "journal");
                        } else {
                            auto p = current.value("properties").toObject();
                            auto incoming = value.toObject().value("properties").toObject();
                            p["assets"] = incoming.value("assets");
                            if (incoming.contains("image_asset_id"))
                                p["image_asset_id"] = incoming.value("image_asset_id");
                            current["properties"] = p;
                            reconcileReferences();
                            renderInspector(route == "journal");
                        }
                    }
                    rebuildSidebar();
                });
            });
        });
    }

void MainWindow::previewAsset(QString id, QJsonObject asset, QWidget *owner) {
        saveLabel->setText("Abriendo archivo…");
        api.bytes("/v1/assets/" + id + "/content", [this, id, asset, owner=QPointer<QWidget>(owner?owner:this)](QByteArray bytes, QString errorMessage) {
            if (!errorMessage.isEmpty()) {
                error(errorMessage);
                return;
            }
            if(owner)MediaViewer::showAsset(owner, id, str(asset, "name"), bytes, dark, reduceMotion);
            saveLabel->setText(saveState(current));
        });
    }
