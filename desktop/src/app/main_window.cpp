#include "main_window.hpp"

bool MainWindow::viewReady() const {
        return ready && !navigationPending && editor;
    }

MainWindow::MainWindow() : api(this) {
        setWindowTitle("Lumen · 0.6");
        resize(1448, 900);
        setMinimumSize(930, 620);
        QSettings pref("Lumen", "Lumen");
        dark = pref.value("dark", true).toBool();
        compact = pref.value("compact", true).toBool();
        fontSize = pref.value("fontSize", 16).toInt();
        if (pref.value("visualVersion", 0).toInt() < 2 && pref.contains("fontSize"))
            fontSize = qRound(fontSize * 4. / 3.);
        fontSize = qBound(12, fontSize, 22);
        themePreset = pref.value("themePreset", "Grafito").toString();
        reduceMotion = pref.value("reduceMotion", false).toBool();
        // D4: restore the persisted recently-opened ids for this workspace
        // (object ids only; see main_window_sync.cpp).
        loadRecentIds();
        auto *central = new QWidget;
        central->setObjectName("appBackground");
        setCentralWidget(central);
        auto *outer = new QVBoxLayout(central);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);
        auto *bar = new QWidget;
        bar->setObjectName("topbar");
        bar->setFixedHeight(41);
        auto *top = new QHBoxLayout(bar);
        top->setContentsMargins(8, 3, 10, 3);
        top->setSpacing(5);
        auto *space = new QWidget;
        space->setFixedWidth(262);
        auto *spaceRow = new QHBoxLayout(space);
        spaceRow->setContentsMargins(4, 0, 0, 0);
        spaceRow->setSpacing(5);
        auto *logo = button("Lumen", "bulb", "quiet");
        logo->setObjectName("spaceButton");
        logo->setToolTip("Tu espacio personal");
        connect(logo,&QPushButton::clicked,this,[this,logo]{
            QMenu menu;menu.addAction("Volver al diario",[this]{navigateJournal(QDate::currentDate());});
            menu.addAction("Apariencia y atajos",[this]{preferences();});menu.exec(logo->mapToGlobal(QPoint(0,logo->height())));
        });
        logo->setMinimumHeight(29);
        spaceRow->addWidget(logo);
        spaceRow->addStretch();
        auto *collapse = button("", "layout-sidebar-left-collapse", "quiet");
        collapse->setFixedSize(28, 28);
        collapse->setToolTip("Mostrar u ocultar navegación · Ctrl+\\");
        connect(collapse, &QPushButton::clicked, this, [this] {
            sidebar->setVisible(!sidebar->isVisible());
        });
        spaceRow->addWidget(collapse);
        top->addWidget(space);
        backButton = button("", "chevron-left", "quiet");
        forwardButton = button("", "chevron-right", "quiet");
        for (auto *b : {backButton, forwardButton})
            b->setFixedSize(27, 28);
        backButton->setToolTip("Atrás · Alt+←");
        forwardButton->setToolTip("Adelante · Alt+→");
        connect(backButton, &QPushButton::clicked, this, [this] {
            moveHistory(-1);
        });
        connect(forwardButton, &QPushButton::clicked, this, [this] {
            moveHistory(1);
        });
        top->addWidget(backButton);
        top->addWidget(forwardButton);
        tabBar = new QTabBar;
        tabBar->setObjectName("documentTabs");
        tabBar->setDocumentMode(true);
        tabBar->setExpanding(false);
        tabBar->setElideMode(Qt::ElideRight);
        tabBar->setTabsClosable(true);
        tabBar->addTab(icon("calendar"), "Calendario");
        connect(tabBar, &QTabBar::currentChanged, this, [this](int i) {
            switchTab(i);
        });
        connect(tabBar, &QTabBar::tabCloseRequested, this, [this](int i) {
            closeTab(i);
        });
        top->addWidget(tabBar);
        auto *newTabButton = button("", "plus", "quiet");
        newTabButton->setFixedSize(28, 28);
        newTabButton->setToolTip("Nueva pestaña · Ctrl+T");
        connect(newTabButton, &QPushButton::clicked, this, [this] {
            newTab();
        });
        top->addWidget(newTabButton);
        top->addStretch();
        crumb = label("Calendario", "muted");
        crumb->hide();
        auto *search = button("", "search", "quiet");
        search->setFixedSize(30, 29);
        search->setToolTip("Buscar contenido y acciones · Ctrl+K / Ctrl+P");
        connect(search, &QPushButton::clicked, this, [this] {
            commandPalette();
        });
        top->addWidget(search);
        auto *toggle = button("", "layout-sidebar-right-collapse", "quiet");
        toggle->setFixedSize(30, 29);
        toggle->setToolTip("Mostrar u ocultar información · Ctrl+.");
        connect(toggle, &QPushButton::clicked, this, [this] {
            inspector->setVisible(!inspector->isVisible());
        });
        top->addWidget(toggle);
        outer->addWidget(bar);
        notice = label("", "notice");
        notice->setAccessibleName("Estado de la operación");
        notice->hide();
        outer->addWidget(notice);
        auto *container = new QWidget;
        shell = new QHBoxLayout(container);
        shell->setContentsMargins(0, 0, 8, 0);
        shell->setSpacing(8);
        outer->addWidget(container, 1);
        auto *navScroll = new QScrollArea;
        navScroll->setObjectName("navigationScroll");
        navScroll->setFixedWidth(278);
        navScroll->setWidgetResizable(true);
        navScroll->setFrameShape(QFrame::NoFrame);
        navScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sidebar = new QWidget;
        sidebar->setObjectName("sidebar");
        nav = new QVBoxLayout(sidebar);
        nav->setContentsMargins(8, 12, 8, 12);
        nav->setSpacing(3);
        navScroll->setWidget(sidebar);
        shell->addWidget(navScroll);
        // Hide the scrolling shell along with its contents when collapsing navigation.
        disconnect(collapse, nullptr, this, nullptr);
        connect(collapse, &QPushButton::clicked, this, [navScroll] {
            navScroll->setVisible(!navScroll->isVisible());
        });
        page = new QWidget;
        page->setObjectName("page");
        body = new QVBoxLayout(page);
        body->setContentsMargins(12, 10, 12, 8);
        body->setSpacing(9);
        shell->addWidget(page, 1);
        auto *infoScroll = new QScrollArea;
        infoScroll->setObjectName("informationScroll");
        infoScroll->setWidgetResizable(true);
        infoScroll->setFrameShape(QFrame::NoFrame);
        infoScroll->setFixedWidth(312);
        inspector = new QWidget;
        inspector->setObjectName("inspector");
        inspect = new QVBoxLayout(inspector);
        inspect->setContentsMargins(14, 16, 14, 16);
        inspect->setSpacing(11);
        infoScroll->setWidget(inspector);
        shell->addWidget(infoScroll);
        infoScroll->hide();
        disconnect(toggle, nullptr, this, nullptr);
        connect(toggle, &QPushButton::clicked, this, [infoScroll] {
            infoScroll->setVisible(!infoScroll->isVisible());
        });
        auto *status = new QWidget;
        status->setObjectName("statusbar");
        status->setFixedHeight(23);
        auto *sl = new QHBoxLayout(status);
        sl->setContentsMargins(15, 0, 15, 0);
        connection = label("Conectando…", "muted");
        saveLabel = label("", "muted");
        connection->setWordWrap(false);
        saveLabel->setWordWrap(false);
        sl->addWidget(connection);
        sl->addStretch();
        sl->addWidget(saveLabel);
        outer->addWidget(status);
        saveTimer.setSingleShot(true);
        saveTimer.setInterval(450);
        connect(&saveTimer, &QTimer::timeout, this, [this] {
            saveCurrent();
        });
        syncTimer.setInterval(1800);
        connect(&syncTimer, &QTimer::timeout, this, [this] {
            pollWorkspace();
        });
        syncTimer.start();
        shortcut("F5", [this] {
            refresh();
        });
        shortcut("Ctrl+B", [this] {
            format("Negrita");
        });
        shortcut("Ctrl+I", [this] {
            format("Cursiva");
        });
        shortcut("Ctrl+2", [this] {
            format("Título");
        });
        shortcut("Ctrl+Shift+7", [this] {
            format("Lista");
        });
        shortcut("Ctrl+K", [this] {
            if (editor && editor->hasFocus() && editor->textCursor().hasSelection())
                editor->showLinkEditor();
            else
                commandPalette();
        });
        shortcut("Ctrl+P", [this] {
            commandPalette();
        });
        shortcut("Ctrl+F", [this] {
            if (editor) editor->showFindBar();
        });
        shortcut("Ctrl+R", [this] {
            if (!editor) return;
            if (editor->findBarVisible()) editor->setFindReplaceVisible(!editor->findReplaceVisible());
            else editor->showFindBar(true);
        });
        shortcut("F3", [this] {
            if (editor) editor->findNextMatch();
        });
        shortcut("Shift+F3", [this] {
            if (editor) editor->findPreviousMatch();
        });
        shortcut("Ctrl+N", [this] {
            newObject();
        });
        shortcut("Ctrl+U", [this] {
            newObject();
        });
        shortcut("Ctrl+J", [this] {
            navigateJournal(QDate::currentDate());
        });
        shortcut("Ctrl+S", [this] {
            saveCurrent();
        });
        shortcut("Ctrl+L", [this] {
            linkObject();
        });
        shortcut("Ctrl+Shift+V", [this] {
            captureUrl();
        });
        shortcut("Ctrl+Shift+A", [this] {
            attach();
        });
        shortcut("Ctrl+Shift+P", [this] {
            preferences();
        });
        shortcut("Ctrl+Shift+G", [this] {
            showGraph();
        });
        shortcut("Ctrl+Shift+T", [this] {
            newType();
        });
        shortcut("Ctrl+T", [this] {
            newTab();
        });
        shortcut("Ctrl+W", [this] {
            closeTab(activeTab);
        });
        shortcut("Alt+Left", [this] {
            moveHistory(-1);
        });
        shortcut("Alt+Right", [this] {
            moveHistory(1);
        });
        shortcut("Ctrl+Alt+Left", [this] {
            navigateJournal(day.addDays(-1));
        });
        shortcut("Ctrl+Alt+Right", [this] {
            navigateJournal(day.addDays(1));
        });
        shortcut("Ctrl+.", [infoScroll] {
            infoScroll->setVisible(!infoScroll->isVisible());
        });
        shortcut("Ctrl+\\", [navScroll] {
            navScroll->setVisible(!navScroll->isVisible());
        });
        applyTheme();
        rebuildSidebar();
        updateHistoryButtons();
        if (api.token.isEmpty())
            QTimer::singleShot(0, this, [this] {
                connectDialog();
            });
        else
            refresh();
    }

void MainWindow::shortcut(const QString &key, std::function<void()> fn) {
        auto *s = new QShortcut(QKeySequence(key), this);
        connect(s, &QShortcut::activated, this, fn);
    }

void MainWindow::error(const QString &s) {
        failure = s;
        notice->setText(s);
        notice->show();
        saveLabel->setText(dirty ? "Sin guardar" : "Operación no completada");
    }

MainWindow::NavState MainWindow::snapshot() const {
        NavState s;
        s.route = route;
        s.id = str(current, "id");
        s.type = typeFilter;
        s.section = typeSection;
        s.date = day;
        s.mode = calendarMode;
        if (activeViews)
            s.view = activeViews->viewSpec();
        if (documentScroll)
            s.scroll = documentScroll->verticalScrollBar()->value();
        return s;
    }

void MainWindow::rememberPosition() {
        if (documentScroll && !str(current, "id").isEmpty())
            scrollPositions[str(current, "id")] = documentScroll->verticalScrollBar()->value();
    }

void MainWindow::updateHistoryButtons() {
        backButton->setEnabled(historyIndices.value(activeTab) > 0);
        forwardButton->setEnabled(activeTab < histories.size() &&
                                  historyIndices.value(activeTab) + 1 < histories[activeTab].size());
    }

void MainWindow::recordNavigation() {
        auto s = snapshot();
        auto &h = histories[activeTab];
        auto &i = historyIndices[activeTab];
        if (!replaying) {
            if (!h.isEmpty() && h[i].route == s.route && h[i].id == s.id && h[i].type == s.type &&
                h[i].date == s.date && h[i].mode == s.mode && h[i].section == s.section)
                h[i] = s;
            else {
                while (h.size() > i + 1)
                    h.removeLast();
                h.append(s);
                i = h.size() - 1;
            }
        } else {
            if (i >= 0 && i < h.size())
                h[i] = s;
            replaying = false;
        }
        QString text = route == "journal"    ? QString("Calendario")
                       : route == "timeline" ? QString("Calendario")
                       : route == "tasks"    ? QString("Tareas")
                       : route == "graph"    ? QString("Grafo")
                       : route == "gallery"
                           ? (typeFilter.isEmpty() ? QString("Todo") : str(typeFor(typeFilter), "name"))
                           : str(current, "title");
        if (text.isEmpty())
            text = "Lumen";
        tabBar->setTabText(activeTab, text.left(35));
        tabBar->setTabToolTip(activeTab, text);
        tabBar->setTabIcon(activeTab,
                           icon(route == "journal" || route == "timeline" ? "calendar"
                                : route == "tasks"                        ? "circle-check"
                                : route == "graph"                        ? "network"
                                                   : str(typeFor(str(current, "type_id")), "icon")));
        updateHistoryButtons();
    }

void MainWindow::restoreState(const NavState &s) {
        replaying = true;
        day = s.date;
        calendarMode = s.mode;
        if (!s.id.isEmpty())
            scrollPositions[s.id] = s.scroll;
        if (s.route == "gallery" || s.route == "query")
            showCollection(s.type, s.view, s.route == "query" ? s.id : QString(),
                           s.route == "gallery" ? s.section : QString("all"));
        else if (s.route == "collection")
            showCollectionPage(s.id);
        else if (s.route == "tasks")
            showTasks();
        else if (s.route == "graph")
            showGraph();
        else if (s.route == "timeline")
            showTimeline(s.mode == "week" ? 7 : s.mode == "three" ? 3 : 31);
        else if (s.route == "object")
            openObject(s.id);
        else
            navigateJournal(s.date);
    }

void MainWindow::moveHistory(int delta) {
        int target = historyIndices[activeTab] + delta;
        if (target < 0 || target >= histories[activeTab].size())
            return;
        if (!canLeave([this, delta] {
                moveHistory(delta);
            }))
            return;
        histories[activeTab][historyIndices[activeTab]] = snapshot();
        historyIndices[activeTab] = target;
        restoreState(histories[activeTab][target]);
    }

void MainWindow::switchTab(int index) {
        if (index < 0 || index == activeTab || index >= histories.size())
            return;
        {
            QSignalBlocker b(tabBar);
            tabBar->setCurrentIndex(activeTab);
        }
        if (!canLeave([this, index] {
                switchTab(index);
            }))
            return;
        histories[activeTab][historyIndices[activeTab]] = snapshot();
        activeTab = index;
        {
            QSignalBlocker b(tabBar);
            tabBar->setCurrentIndex(index);
        }
        restoreState(histories[index][historyIndices[index]]);
    }

void MainWindow::newTab(QString objectId) {
        if (!canLeave([this, objectId] {
                newTab(objectId);
            }))
            return;
        histories[activeTab][historyIndices[activeTab]] = snapshot();
        NavState s;
        s.id = objectId;
        s.route = objectId.isEmpty() ? "journal" : "object";
        histories.append(QVector<NavState>{s});
        historyIndices.append(0);
        {
            QSignalBlocker b(tabBar);
            tabBar->addTab("Nueva pestaña");
        }
        switchTab(histories.size() - 1);
    }

void MainWindow::closeTab(int index) {
        if (index < 0 || index >= histories.size())
            return;
        if (index != activeTab) {
            histories.removeAt(index);
            historyIndices.removeAt(index);
            QSignalBlocker block(tabBar);
            tabBar->removeTab(index);
            if (index < activeTab)
                --activeTab;
            tabBar->setCurrentIndex(activeTab);
            updateHistoryButtons();
            return;
        }
        if (!canLeave([this, index] {
                closeTab(index);
            }))
            return;
        if (histories.size() == 1) {
            navigateJournal(QDate::currentDate());
            return;
        }
        histories.removeAt(index);
        historyIndices.removeAt(index);
        {
            QSignalBlocker block(tabBar);
            tabBar->removeTab(index);
            activeTab = qMin(index, int(histories.size() - 1));
            tabBar->setCurrentIndex(activeTab);
        }
        restoreState(histories[activeTab][historyIndices[activeTab]]);
    }

void MainWindow::setInfoVisible(bool visible) {
        if (auto *panel = findChild<QScrollArea *>("informationScroll"))
            panel->setVisible(visible);
    }

void MainWindow::resetBody() {
        ++navigationGeneration;
        navigationPending = false;
        // Detach the outgoing page's text widgets before hiding/deleting them:
        // a queued textChanged from a stale editor or title must not mark the
        // next page dirty (the same stale-widget class as the 0.6 SIGSEGV).
        if (editor)
            disconnect(editor, nullptr, this, nullptr);
        if (title)
            disconnect(title, nullptr, this, nullptr);
        editor = nullptr;
        title = nullptr;
        linkedSection = nullptr;
        documentPane = nullptr;
        documentScroll = nullptr;
        activeViews = nullptr;
        miniCalendar = nullptr;
        calendarOverview = nullptr;
        graphCanvas = nullptr;
        activeTasks = nullptr;
        clearLayout(body);
        clearLayout(inspect);
        page->setEnabled(true);
    }

bool MainWindow::canLeave(std::function<void()> next) {
        if (saving) {
            pendingNavigation = next;
            return false;
        }
        if (dirty) {
            pendingNavigation = next;
            saveCurrent();
            return false;
        }
        return true;
    }

quint64 MainWindow::beginNavigation() {
        rememberPosition();
        navigationPending = true;
        if (documentPane)
            documentPane->setEnabled(false);
        saveLabel->setText("Abriendo…");
        return ++navigationGeneration;
    }

bool MainWindow::finishNavigation(quint64 generation) {
        if (generation != navigationGeneration)
            return false;
        navigationPending = false;
        if (documentPane)
            documentPane->setEnabled(true);
        return true;
    }

void MainWindow::showJournal() {
        navigateJournal(day);
    }

void MainWindow::navigateJournal(QDate target) {
        if (!target.isValid())
            return;
        if (!canLeave([this, target] {
                navigateJournal(target);
            }))
            return;
        if (!histories[activeTab].isEmpty())
            histories[activeTab][historyIndices[activeTab]] = snapshot();
        const auto generation = beginNavigation();
        auto commit = [this, target](const QJsonObject &o) {
            day = target;
            current = o;
            rememberObject(o);
            route = "journal";
            calendarMode = "day";
            typeFilter.clear();
            crumb->setText("Calendario");
            renderEditor(true);
            rebuildSidebar();
            recordNavigation();
        };
        QJsonObject cached;
        for (auto v : objects) {
            auto o = v.toObject();
            if (str(o, "type_id") == "journal" &&
                o.value("properties").toObject().value("date").toString() == target.toString(Qt::ISODate)) {
                cached = o;
                break;
            }
        }
        if (!cached.isEmpty()) {
            finishNavigation(generation);
            commit(cached);
            const auto liveGeneration = navigationGeneration;
            api.get("/v1/journal/" + target.toString(Qt::ISODate),
                    [this, liveGeneration](QJsonValue v, QString e) {
                        if (!e.isEmpty() || navigationGeneration != liveGeneration)
                            return;
                        auto o = v.toObject();
                        rememberObject(o);
                        if (!dirty && !saving && str(current, "id") == str(o, "id")) {
                            if (current.value("document") != o.value("document") ||
                                current.value("properties") != o.value("properties")) {
                                rememberPosition();
                                current = o;
                                renderEditor(true);
                            } else {
                                current = o;
                                saveLabel->setText(saveState(o));
                            }
                        }
                    });
            return;
        }
        // D5: lazily created daily notes start from the journal type's
        // default template when one is set (contracts/SYNC.md "Journal
        // default template"). The parameter only affects lazy creation:
        // existing notes are never re-applied, and the cached path above
        // (a live refresh of an existing note) never sends it.
        QString journalPath = "/v1/journal/" + target.toString(Qt::ISODate);
        if (journalTypeHasDefaultTemplate())
            journalPath += "?apply_default_template=true";
        api.get(journalPath,
                [this, generation, commit](QJsonValue v, QString e) {
                    if (!finishNavigation(generation))
                        return;
                    if (!e.isEmpty()) {
                        if (miniCalendar) {
                            QSignalBlocker b(miniCalendar);
                            miniCalendar->setSelectedDate(day);
                        }
                        replaying = false;
                        error("No se pudo abrir el día: " + e);
                        return;
                    }
                    commit(v.toObject());
                });
    }

void MainWindow::openObject(const QString &id) {
        const auto cached = objectFor(id);
        if (str(cached, "type_id") == "query") {
            auto spec = cached.value("properties").toObject().value("view").toObject();
            showCollection(spec.value("type_id").toString(), spec, id);
            return;
        }
        if (str(cached, "type_id") == "collection") {
            showCollectionPage(id);
            return;
        }
        if (!canLeave([this, id] {
                openObject(id);
            }))
            return;
        if (!histories[activeTab].isEmpty())
            histories[activeTab][historyIndices[activeTab]] = snapshot();
        const auto generation = beginNavigation();
        auto commit = [this](QJsonObject o) {
            current = o;
            rememberObject(o);
            rememberRecent(str(o, "id"));
            route = "object";
            typeFilter.clear();
            crumb->setText(str(o, "title"));
            renderEditor(false);
            rebuildSidebar();
            recordNavigation();
        };
        if (!cached.isEmpty()) {
            finishNavigation(generation);
            commit(cached);
            return;
        }
        api.get("/v1/objects/" + id, [this, generation, commit, id](QJsonValue v, QString e) {
            if (!finishNavigation(generation))
                return;
            if (!e.isEmpty()) {
                replaying = false;
                error(e);
                return;
            }
            auto o = v.toObject();
            if (str(o, "type_id") == "collection") {
                rememberObject(o);
                showCollectionPage(id);
                return;
            }
            commit(o);
        });
    }

void MainWindow::preferences() {
        QDialog d(this);
        d.setWindowTitle("Hazlo tuyo");
        d.setMinimumWidth(460);
        auto *l = new QVBoxLayout(&d);
        l->addWidget(label("Un espacio con tu ritmo", "dialogTitle"));
        auto *preset = new QComboBox;
        preset->addItems({"Grafito", "Lumen", "Noir", "Terminal", "Papel"});
        preset->setCurrentText(themePreset);
        l->addWidget(label("Tema visual"));
        l->addWidget(preset);
        auto *theme = new QCheckBox("Tema oscuro");
        theme->setChecked(dark);
        auto *dense = new QCheckBox("Espaciado compacto");
        dense->setChecked(compact);
        auto *motion = new QCheckBox("Reducir animaciones");
        motion->setChecked(reduceMotion);
        auto *size = new QSpinBox;
        size->setRange(12, 22);
        size->setValue(fontSize);
        l->addWidget(theme);
        l->addWidget(dense);
        l->addWidget(motion);
        l->addWidget(label("Tamaño del texto"));
        l->addWidget(size);
        l->addWidget(label("Ctrl K  buscar y navegar\nCtrl N  crear objeto\nCtrl L  enlazar objeto\nCtrl J  "
                           "diario\nCtrl S  guardar\nCtrl Shift V  capturar URL\nCtrl Shift A  "
                           "adjuntar\nCtrl Shift G  conexiones\nCtrl Alt ← / →  día anterior / "
                           "siguiente\nAlt ← / →  historial · Ctrl T / W pestañas",
                           "keyboardHints"));
        auto *b = button("Aplicar", "configure", "primary");
        l->addWidget(b);
        connect(b, &QPushButton::clicked, &d, &QDialog::accept);
        if (d.exec() != QDialog::Accepted)
            return;
        themePreset = preset->currentText();
        dark = theme->isChecked();
        if (themePreset == "Noir" || themePreset == "Terminal")
            dark = true;
        if (themePreset == "Papel")
            dark = false;
        compact = dense->isChecked();
        reduceMotion = motion->isChecked();
        fontSize = size->value();
        QSettings pref("Lumen", "Lumen");
        pref.setValue("dark", dark);
        pref.setValue("compact", compact);
        pref.setValue("fontSize", fontSize);
        pref.setValue("visualVersion", 2);
        pref.setValue("themePreset", themePreset);
        pref.setValue("reduceMotion", reduceMotion);
        applyTheme();
        if (editor) {
            QFont f("Inter");
            f.setPixelSize(fontSize);
            editor->setFont(f);
            editor->setAppearance(dark, reduceMotion);
        }
        if (route == "gallery")
            showCollection(typeFilter, {}, {}, typeSection);
        if (route == "collection")
            showCollectionPage(str(current, "id"));
        if (route == "graph")
            showGraph();
    }

void MainWindow::applyTheme() {
        QString bg = dark ? "#151518" : "#fcfcfc", side = dark ? "#0d0d10" : "#f4f4f5",
                fg = dark ? "#e4e3e2" : "#303035", muted = dark ? "#919097" : "#75747b",
                border = dark ? "#29292e" : "#e4e4e8", hover = dark ? "#242428" : "#e9e9ed",
                active = dark ? "#2d2d31" : "#dfdfe5", accent = dark ? "#d2b7f5" : "#73519b";
        if (themePreset == "Terminal")
            accent = "#a1ce87";
        if (themePreset == "Noir")
            accent = "#ad9bdd";
        if (themePreset == "Papel") {
            bg = "#f8f5eb";
            side = "#eeebdf";
        }
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(bg));
        palette.setColor(QPalette::Base, QColor(bg));
        palette.setColor(QPalette::AlternateBase, QColor(side));
        palette.setColor(QPalette::Text, QColor(fg));
        palette.setColor(QPalette::WindowText, QColor(fg));
        palette.setColor(QPalette::Button, QColor(side));
        palette.setColor(QPalette::ButtonText, QColor(fg));
        palette.setColor(QPalette::Highlight, QColor(active));
        palette.setColor(QPalette::HighlightedText, QColor(fg));
        palette.setColor(QPalette::ToolTipBase, QColor(hover));
        palette.setColor(QPalette::ToolTipText, QColor(fg));
        palette.setColor(QPalette::Link, QColor(accent));
        qApp->setPalette(palette);
        QString css = QString(R"(
QWidget{color:%1;background:%2;font-family:Inter,"Noto Sans";font-size:13px;}
QLabel{background:transparent;} #topbar,#sidebar,#navigationScroll,#statusbar{background:%3;}
#topbar{border-bottom:1px solid %4;} #statusbar{border-top:1px solid %4;}
#page,#documentCanvas,#documentScroll,#objectPreview{background:%2;}
#page{border:1px solid %4;border-radius:12px;} #collectionTitle{font-size:18px;font-weight:600;padding:4px 0;}

#informationScroll,#inspector{background:%3;border-radius:7px;}
#muted{color:%5;font-size:11.6px;} #section{color:%5;font-size:11px;font-weight:500;padding:12px 7px 4px 7px;}
#eyebrow{color:%8;font-size:12px;font-weight:500;} #pageTitle{font-size:25px;font-weight:700;}
#subheading{font-size:15px;font-weight:600;} #dialogTitle{font-size:23px;font-weight:600;}
#documentTitle{font-size:31px;font-weight:700;letter-spacing:-0.6px;padding:0;border:0;background:transparent;border-radius:0;}
#documentTitle:focus{border:0;background:transparent;}
QPushButton,QToolButton{border:1px solid %4;border-radius:5px;padding:5px 9px;background:%2;text-align:left;}
QPushButton:hover,QToolButton:hover{background:%6;} QPushButton:pressed,QToolButton:pressed{background:%7;}
QPushButton:focus,QToolButton:focus{border:1px solid %8;}
QPushButton:disabled{color:%5;} #nav,#quiet,#reference,#spaceButton{border:1px solid transparent;background:transparent;}
#nav{padding:5px 8px;} #nav:hover,#quiet:hover,#reference:hover,#spaceButton:hover{background:%6;}
#nav:focus,#quiet:focus,#reference:focus,#spaceButton:focus{border:1px solid %8;}
#nav:pressed,#quiet:pressed,#reference:pressed{background:%7;}
#activeNav{border:1px solid transparent;background:%7;padding:5px 8px;}
#activeNav:hover{background:%6;} #activeNav:focus{border:1px solid %8;}
#quiet{color:%5;padding:4px 7px;} #primary{background:%7;color:%1;border:1px solid %4;padding:6px 12px;font-weight:500;}
#primary:hover{background:%6;border-color:%8;} #primary:pressed{background:%3;}
#pillActive{background:%7;border:1px solid %4;} #searchButton{background:%6;color:%5;border:0;}
#objectChip{background:%7;border:1px solid transparent;padding:4px 8px;color:%1;}
#objectChip:hover{border-color:%8;background:%6;} #objectChip:focus{border-color:%8;}
#tagChip{background:%7;border:1px solid transparent;border-radius:8px;padding:2px 9px;color:%1;font-size:12.5px;}
#tagChip:hover{border-color:%8;background:%6;} #tagChip:focus{border-color:%8;}
#tagMentionChip{background:transparent;border:1px dashed %4;border-radius:8px;padding:2px 9px;color:%1;font-size:12.5px;}
#tagMentionChip:hover{background:%6;border:1px solid %8;} #tagMentionChip:focus{border-color:%8;}
#typeBadge{background:%7;border:1px solid transparent;border-radius:7px;padding:3px 8px;color:%1;font-size:14px;}
#typeBadge:hover{background:%6;border-color:%4;}
#templateChip{background:%7;border:1px solid %4;border-radius:12px;padding:3px 10px;font-size:12px;color:%8;}
#typeBadge:focus,#collectionCreate:focus,QPushButton[primaryAction="true"]:focus{border:1px solid %8;}
#collectionCreate, QPushButton[primaryAction="true"]{background:%1;color:%2;border:1px solid %4;border-radius:9px;padding:5px 13px;}
#collectionCreate:hover, QPushButton[primaryAction="true"]:hover{background:%6;color:%1;}
#typeCollectionsBar{background:transparent;}
QPushButton#collectionChip{background:%3;border:1px solid %4;border-radius:14px;padding:5px 12px;font-size:13px;}
QPushButton#collectionChip:hover{background:%6;border-color:%8;} QPushButton#collectionChip:focus{border-color:%8;}
QPushButton#collectionChipAll{background:%7;border:1px solid transparent;border-radius:14px;padding:5px 12px;font-weight:500;}
QPushButton#collectionChipNew{color:%5;border:1px dashed %4;background:transparent;border-radius:14px;padding:5px 10px;}
QPushButton#collectionChipNew:hover{background:%6;}
#typeDashboardTabs{background:transparent;}
QToolButton#dashboardTab{background:transparent;border:1px solid transparent;border-radius:8px;padding:6px 11px;font-size:13px;color:%5;}
QToolButton#dashboardTab:hover{background:%6;} QToolButton#dashboardTab:focus{border-color:%8;}
QToolButton#dashboardTab:checked{background:%7;color:%1;font-weight:500;}
QToolButton#dashboardTabAdd{color:%5;border:1px dashed %4;background:transparent;border-radius:8px;padding:6px 9px;}
QToolButton#dashboardTabAdd:hover{background:%6;} QToolButton#dashboardTabAdd:focus{border-color:%8;}
#keyboardHints,#sidebarFoot{color:%5;font-size:11.6px;} #sidebarFoot{padding:8px;}
#empty{color:%5;font-size:15px;padding:25px;} #notice{background:#463727;color:#ffdfa9;padding:8px 18px;}
#editor,#previewEditor{border:0;border-radius:0;background:transparent;padding:0;selection-background-color:%7;}
#ogCover{background:%3;border-radius:6px;} #divider{color:%4;}
QLineEdit,QComboBox,QSpinBox,QDoubleSpinBox,QDateEdit{border:1px solid %4;border-radius:5px;padding:6px 8px;background:%2;selection-background-color:%7;}
QLineEdit:focus,QComboBox:focus,QSpinBox:focus,QDateEdit:focus{border-color:%8;}
QTextEdit{border:1px solid %4;border-radius:5px;padding:6px;selection-background-color:%7;}
QListWidget{border:1px solid %4;border-radius:6px;padding:4px;outline:0;}
QListWidget::item{padding:8px;border-radius:4px;} QListWidget::item:hover{background:%6;} QListWidget::item:selected{background:%7;color:%1;}
QTabBar::tab{background:transparent;color:%5;padding:7px 10px;border:0;min-width:100px;max-width:210px;}
QTabBar::tab:selected{background:%7;color:%1;border-radius:5px;} QTabBar::tab:hover{background:%6;}
QTabBar::close-button{image:url(:/icons/x.svg);width:12px;height:12px;}
QScrollArea{border:0;background:%2;} QScrollBar:vertical{background:transparent;width:6px;margin:1px;}
QScrollBar::handle:vertical{background:%4;border-radius:3px;min-height:30px;} QScrollBar::handle:vertical:hover{background:%5;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
QScrollBar:horizontal{background:transparent;height:6px;} QScrollBar::handle:horizontal{background:%4;border-radius:3px;min-width:30px;}
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}
QCalendarWidget QWidget{background:transparent;font-size:11px;} QCalendarWidget QToolButton{border:0;padding:3px;color:%1;}
QMenu{background:%2;border:1px solid %4;padding:5px;} QMenu::item{padding:7px 20px;} QMenu::item:selected{background:%7;}
QToolTip{background:%6;color:%1;border:1px solid %4;padding:5px;border-radius:4px;}
QSplitter::handle{background:%4;} QDialog{background:%2;}
)")
                          .arg(fg, bg, side, border, muted, hover, active, accent);
        if (themePreset == "Terminal")
            css.replace("Inter,\"Noto Sans\"", "\"Noto Sans Mono\",monospace");
        css += QString("#editor,#previewEditor{font-size:%1px;}").arg(fontSize);
        qApp->setStyleSheet(css);
        body->setContentsMargins(compact ? 12 : 20, 10, compact ? 12 : 20, 8);
        for(auto *widget:findChildren<QWidget*>()){
            if(auto *view=dynamic_cast<LumenViews*>(widget))view->setTheme(dark,reduceMotion);
            if(auto *form=dynamic_cast<LumenProperties*>(widget))form->setTheme(dark);
        }
        if (miniCalendar)
            miniCalendar->setTheme(dark);
        if (calendarOverview)
            calendarOverview->setTheme(dark, reduceMotion);
        if (graphCanvas)
            graphCanvas->setTheme(dark, reduceMotion);
    }

void MainWindow::seedDemo(std::function<void()> done) {
        QList<QJsonObject> demo = {
            {{"type_id", "book"},
             {"title", "El arte de aprender"},
             {"content", "Aprender es construir conexiones entre ideas, no acumular información."},
             {"properties", QJsonObject{{"author", "Biblioteca de ejemplo"}, {"status", "Leyendo"}}}},
            {{"type_id", "idea"},
             {"title", "Aprender haciendo"},
             {"content", "Convertir cada lectura en una pequeña práctica."},
             {"properties", QJsonObject{}}},
            {{"type_id", "person"},
             {"title", "Clara · ejemplo"},
             {"content", "Una conversación sobre hábitos de estudio."},
             {"properties", QJsonObject{}}},
            {{"type_id", "project"},
             {"title", "Mi jardín de ideas"},
             {"content", "Un proyecto personal para estudiar y conectar lo aprendido."},
             {"properties", QJsonObject{}}}};
        auto idx = std::make_shared<int>(0);
        auto seq = std::make_shared<std::function<void()>>();
        *seq = [this, demo, idx, seq, done] {
            if (*idx >= demo.size()) {
                auto generation = beginNavigation();
                api.get("/v1/journal/" + day.toString(Qt::ISODate), [this, generation, done](QJsonValue value,
                                                                                             QString err) {
                    if (!finishNavigation(generation))
                        return;
                    if (!err.isEmpty()) {
                        error(err);
                        return;
                    }
                    current = value.toObject();
                    route = "journal";
                    renderEditor(true);
                    rebuildSidebar();
                    editor->setHtml("<h2>Un espacio para pensar</h2><p>Hoy empiezo algo pequeño: leer con "
                                    "calma, escribir con intención y conectar lo que descubro.</p><p>Quiero "
                                    "recordar que aprender no es llenar carpetas. Es hacer que una idea "
                                    "encuentre a otra.</p><h3>Lo que me llevo hoy</h3><ul><li>Una pregunta "
                                    "merece su propio lugar.</li><li>Los libros, las personas y los "
                                    "proyectos se pueden conectar.</li></ul><p>Mis fuentes de hoy: </p>");
                    linked = QJsonArray{};
                    editor->moveCursor(QTextCursor::End);
                    for (auto ov : objects) {
                        auto object = ov.toObject();
                        if (str(object, "title") == "El arte de aprender" ||
                            str(object, "title") == "Aprender haciendo")
                            insertReference(object);
                    }
                    saveCurrent(done);
                });
                QTimer::singleShot(0, this, [seq] {
                    *seq = {};
                });
                return;
            }
            auto o = demo[(*idx)++];
            for (auto value : objects) {
                auto existing = value.toObject();
                if (str(existing, "title") == str(o, "title") &&
                    str(existing, "type_id") == str(o, "type_id")) {
                    (*seq)();
                    return;
                }
            }
            o["links"] = QJsonArray{};
            api.post("/v1/objects", o, [this, seq](QJsonValue v, QString e) {
                if (!e.isEmpty()) {
                    error(e);
                    return;
                }
                objects.append(v.toObject());
                (*seq)();
            });
        };
        (*seq)();
    }

void MainWindow::screenshot(const QString &path) {
        grab().save(path);
    }

void MainWindow::closeEvent(QCloseEvent *e) {
        if (!canLeave([this] {
                close();
            })) {
            e->ignore();
            return;
        }
        QMainWindow::closeEvent(e);
    }
