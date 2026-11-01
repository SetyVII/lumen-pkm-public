#include "lumen_tasks.hpp"
#include "lumen_icons.hpp"
#include <QRegularExpression>
#include <QUuid>

namespace LumenTasksParser {
ParsedTask parse(const QString &rawInput, const QDate &ref) {
    ParsedTask result;
    QString text = rawInput.trimmed();
    const QDate today = ref.isValid() ? ref : QDate::currentDate();

    // 1. Priority: !!!, !!, !
    static const QRegularExpression prioRe(R"((?:^|\s)(!{1,3})(?:\s|$))");
    auto prioMatch = prioRe.match(text);
    if (prioMatch.hasMatch()) {
        const int count = prioMatch.captured(1).length();
        result.priority = qBound(1, count, 3);
        text.remove(prioMatch.capturedStart(1), prioMatch.capturedLength(1));
        text = text.trimmed();
    }

    // 2. Context: @ObjectName
    static const QRegularExpression ctxRe(R"((?:^|\s)@([^\s!]+))");
    auto ctxMatch = ctxRe.match(text);
    if (ctxMatch.hasMatch()) {
        result.contextName = ctxMatch.captured(1).trimmed();
        text.remove(ctxMatch.capturedStart(0), ctxMatch.capturedLength(0));
        text = text.trimmed();
    }

    // 3. Time: 14:00, 2pm, 9:30am
    static const QRegularExpression timeRe(R"((?:^|\s)(\d{1,2})(?::(\d{2}))?\s*(am|pm)?(?:\s|$))", QRegularExpression::CaseInsensitiveOption);
    auto timeMatch = timeRe.match(text);
    if (timeMatch.hasMatch() && (!timeMatch.captured(2).isEmpty() || !timeMatch.captured(3).isEmpty())) {
        int hour = timeMatch.captured(1).toInt();
        const int min = timeMatch.captured(2).isEmpty() ? 0 : timeMatch.captured(2).toInt();
        const QString ampm = timeMatch.captured(3).toLower();
        if (ampm == "pm" && hour < 12) hour += 12;
        else if (ampm == "am" && hour == 12) hour = 0;
        if (hour >= 0 && hour <= 23 && min >= 0 && min <= 59) {
            result.time = QTime(hour, min);
            result.hasTime = true;
            text.remove(timeMatch.capturedStart(0), timeMatch.capturedLength(0));
            text = text.trimmed();
        }
    }

    // 4. Relative dates and keywords
    static const QRegularExpression dateWordsRe(
        R"((?:^|\s)(today|hoy|tomorrow|manana|mañana|yesterday|ayer|lunes|martes|miercoles|miércoles|jueves|viernes|sabado|sábado|domingo|monday|tuesday|wednesday|thursday|friday|saturday|sunday)(?:\s|$))",
        QRegularExpression::CaseInsensitiveOption);
    auto dateMatch = dateWordsRe.match(text);
    if (dateMatch.hasMatch()) {
        const QString word = dateMatch.captured(1).toLower();
        if (word == "today" || word == "hoy") {
            result.dueDate = today;
            result.hasDate = true;
        } else if (word == "tomorrow" || word == "manana" || word == "mañana") {
            result.dueDate = today.addDays(1);
            result.hasDate = true;
        } else if (word == "yesterday" || word == "ayer") {
            result.dueDate = today.addDays(-1);
            result.hasDate = true;
        } else {
            static const QMap<QString, int> dayOfWeekMap = {
                {"lunes", 1}, {"monday", 1},
                {"martes", 2}, {"tuesday", 2},
                {"miercoles", 3}, {"miércoles", 3}, {"wednesday", 3},
                {"jueves", 4}, {"thursday", 4},
                {"viernes", 5}, {"friday", 5},
                {"sabado", 6}, {"sábado", 6}, {"saturday", 6},
                {"domingo", 7}, {"sunday", 7}
            };
            if (dayOfWeekMap.contains(word)) {
                int targetDay = dayOfWeekMap.value(word);
                int diff = targetDay - today.dayOfWeek();
                if (diff <= 0) diff += 7; // next occurrence
                result.dueDate = today.addDays(diff);
                result.hasDate = true;
            }
        }
        text.remove(dateMatch.capturedStart(0), dateMatch.capturedLength(0));
        text = text.trimmed();
    }

    // 5. ISO or DD/MM format
    if (!result.hasDate) {
        static const QRegularExpression isoDateRe(R"((?:^|\s)(\d{4}-\d{2}-\d{2})(?:\s|$))");
        auto isoMatch = isoDateRe.match(text);
        if (isoMatch.hasMatch()) {
            QDate d = QDate::fromString(isoMatch.captured(1), Qt::ISODate);
            if (d.isValid()) {
                result.dueDate = d;
                result.hasDate = true;
                text.remove(isoMatch.capturedStart(0), isoMatch.capturedLength(0));
                text = text.trimmed();
            }
        }
    }

    result.title = text.simplified();
    return result;
}
}

LumenTasks::LumenTasks(QWidget *parent) : QWidget(parent) {
    setObjectName("tasksView");
    rebuildUi();
}

void LumenTasks::setTheme(bool dark) {
    if (dark_ == dark) return;
    dark_ = dark;
    renderList();
}

void LumenTasks::setData(const QJsonArray &objects, const QJsonArray &types) {
    allObjects_.clear();
    tasks_.clear();
    types_.clear();
    for (const auto &v : types) types_.append(v.toObject());
    for (const auto &v : objects) {
        const auto obj = v.toObject();
        allObjects_.append(obj);
        if (obj.value("type_id").toString() == "task") {
            tasks_.append(obj);
        }
    }
    renderList();
}

void LumenTasks::setActiveTab(const QString &tab) {
    if (activeTab_ == tab) return;
    activeTab_ = tab;
    if (tabBar_) {
        const QStringList tabKeys = {"inbox", "today", "scheduled", "context", "open", "completed"};
        int idx = tabKeys.indexOf(tab);
        if (idx >= 0 && tabBar_->currentIndex() != idx) {
            tabBar_->setCurrentIndex(idx);
        }
    }
    renderList();
    if (onTabChanged) onTabChanged(activeTab_);
}

void LumenTasks::rebuildUi() {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    // Header & Quick Capture
    auto *topBar = new QWidget(this);
    auto *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(20, 16, 20, 0);
    topLayout->setSpacing(10);

    auto *headerLabel = new QLabel(this);
    headerLabel->setText("Tareas");
    QFont headerFont("Inter"); headerFont.setPixelSize(22); headerFont.setBold(true);
    headerLabel->setFont(headerFont);
    topLayout->addWidget(headerLabel);

    topLayout->addSpacing(16);

    quickInput_ = new QLineEdit(this);
    quickInput_->setObjectName("taskQuickInput");
    quickInput_->setPlaceholderText("Nueva tarea… Ej: 'Reunión semanal !! tomorrow 2pm @Marketing'");
    quickInput_->setClearButtonEnabled(true);
    connect(quickInput_, &QLineEdit::returnPressed, this, &LumenTasks::handleQuickAdd);
    topLayout->addWidget(quickInput_, 1);

    quickAddBtn_ = new QPushButton("+ Nueva", this);
    quickAddBtn_->setObjectName("taskQuickAddBtn");
    quickAddBtn_->setCursor(Qt::PointingHandCursor);
    connect(quickAddBtn_, &QPushButton::clicked, this, &LumenTasks::handleQuickAdd);
    topLayout->addWidget(quickAddBtn_);

    layout->addWidget(topBar);

    // Tab Bar
    tabBar_ = new QTabWidget(this);
    tabBar_->setObjectName("tasksTabBar");
    tabBar_->setDocumentMode(true);

    tabBar_->addTab(new QWidget, LumenIcons::icon("inbox", QColor("#898993"), 18), "Inbox");
    tabBar_->addTab(new QWidget, LumenIcons::icon("sun", QColor("#e07a5f"), 18), "Today");
    tabBar_->addTab(new QWidget, LumenIcons::icon("calendar", QColor("#898993"), 18), "Scheduled");
    tabBar_->addTab(new QWidget, LumenIcons::icon("link", QColor("#898993"), 18), "Context");
    tabBar_->addTab(new QWidget, LumenIcons::icon("circle", QColor("#898993"), 18), "Open");
    tabBar_->addTab(new QWidget, LumenIcons::icon("circle-check", QColor("#48c08a"), 18), "Completed");

    connect(tabBar_, &QTabWidget::currentChanged, this, [this](int idx) {
        const QStringList tabKeys = {"inbox", "today", "scheduled", "context", "open", "completed"};
        if (idx >= 0 && idx < tabKeys.size()) {
            activeTab_ = tabKeys[idx];
            renderList();
            if (onTabChanged) onTabChanged(activeTab_);
        }
    });
    layout->addWidget(tabBar_);

    // Scroll Area for tasks
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    listContainer_ = new QWidget(scrollArea_);
    listLayout_ = new QVBoxLayout(listContainer_);
    listLayout_->setContentsMargins(20, 8, 20, 24);
    listLayout_->setSpacing(6);
    listLayout_->setAlignment(Qt::AlignTop);

    emptyLabel_ = new QLabel("No hay tareas en esta sección", listContainer_);
    emptyLabel_->setAlignment(Qt::AlignCenter);
    emptyLabel_->setStyleSheet("color:#8a8a92; font-size:14px; padding:40px;");
    emptyLabel_->hide();
    listLayout_->addWidget(emptyLabel_);

    scrollArea_->setWidget(listContainer_);
    layout->addWidget(scrollArea_, 1);
}

void LumenTasks::handleQuickAdd() {
    if (!quickInput_) return;
    QString text = quickInput_->text().trimmed();
    if (text.isEmpty()) return;

    QString explicitContextId;
    // Check if an existing object matches @<title>
    for (const auto &obj : allObjects_) {
        const QString title = obj.value("title").toString().trimmed();
        if (!title.isEmpty() && text.contains("@" + title, Qt::CaseInsensitive)) {
            explicitContextId = obj.value("id").toString();
            text.remove(QRegularExpression("@" + QRegularExpression::escape(title), QRegularExpression::CaseInsensitiveOption));
            text = text.trimmed();
            break;
        }
    }

    auto parsed = LumenTasksParser::parse(text);
    if (parsed.title.isEmpty()) parsed.title = text;

    QJsonObject properties;
    properties.insert("status", "todo");
    if (parsed.priority > 0) properties.insert("priority", parsed.priority);
    if (parsed.hasDate) properties.insert("due_date", parsed.dueDate.toString(Qt::ISODate));
    if (parsed.hasTime) properties.insert("time", parsed.time.toString("HH:mm"));

    if (!explicitContextId.isEmpty()) {
        properties.insert("context_id", explicitContextId);
    } else if (!parsed.contextName.isEmpty()) {
        for (const auto &obj : allObjects_) {
            if (obj.value("title").toString().contains(parsed.contextName, Qt::CaseInsensitive)) {
                properties.insert("context_id", obj.value("id").toString());
                break;
            }
        }
    }

    QJsonObject task;
    task.insert("type_id", "task");
    task.insert("title", parsed.title);
    task.insert("properties", properties);

    quickInput_->clear();

    if (onCreateTask) onCreateTask(task);
}

QJsonObject LumenTasks::findObject(const QString &id) const {
    if (id.isEmpty()) return {};
    for (const auto &obj : allObjects_) {
        if (obj.value("id").toString() == id) return obj;
    }
    return {};
}

QList<QJsonObject> LumenTasks::tasksForTab(const QString &tab) const {
    const QDate today = QDate::currentDate();
    QList<QJsonObject> result;

    for (const auto &task : tasks_) {
        const auto props = task.value("properties").toObject();
        const QString status = props.value("status").toString().toLower();
        const bool isDone = (status == "done" || status == "completed");
        const QString dateStr = props.value("due_date").toString();
        const QDate dueDate = dateStr.isEmpty() ? QDate() : QDate::fromString(dateStr, Qt::ISODate);

        if (tab == "inbox") {
            // Tasks with no scheduled date and not done
            if (!isDone && !dueDate.isValid()) result.append(task);
        } else if (tab == "today") {
            // Due today, overdue, or in_progress undated
            if (!isDone && (dueDate == today || (dueDate.isValid() && dueDate < today) || status == "in progress" || status == "in_progress")) {
                result.append(task);
            } else if (isDone && dueDate == today) {
                result.append(task);
            }
        } else if (tab == "scheduled") {
            if (dueDate.isValid() && !isDone) result.append(task);
        } else if (tab == "context") {
            if (!isDone) result.append(task);
        } else if (tab == "open") {
            if (!isDone) result.append(task);
        } else if (tab == "completed") {
            if (isDone) result.append(task);
        }
    }

    // Sort by priority (descending) and then due date
    std::sort(result.begin(), result.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const int pA = a.value("properties").toObject().value("priority").toInt();
        const int pB = b.value("properties").toObject().value("priority").toInt();
        if (pA != pB) return pA > pB;
        const QString dA = a.value("properties").toObject().value("due_date").toString();
        const QString dB = b.value("properties").toObject().value("due_date").toString();
        if (dA.isEmpty() && !dB.isEmpty()) return false;
        if (!dA.isEmpty() && dB.isEmpty()) return true;
        return dA < dB;
    });

    return result;
}

QMap<QString, QList<QJsonObject>> LumenTasks::groupedTasksForTab(const QString &tab) const {
    QMap<QString, QList<QJsonObject>> groups;
    const auto list = tasksForTab(tab);
    const QDate today = QDate::currentDate();

    if (tab == "scheduled") {
        for (const auto &task : list) {
            const QString dateStr = task.value("properties").toObject().value("due_date").toString();
            const QDate d = QDate::fromString(dateStr, Qt::ISODate);
            if (!d.isValid()) continue;
            if (d < today) groups["1_overdue"].append(task);
            else if (d == today) groups["2_today"].append(task);
            else if (d == today.addDays(1)) groups["3_tomorrow"].append(task);
            else if (d <= today.addDays(7)) groups["4_this_week"].append(task);
            else if (d <= today.addDays(14)) groups["5_next_week"].append(task);
            else groups["6_later"].append(task);
        }
    } else if (tab == "context") {
        for (const auto &task : list) {
            const QString ctxId = task.value("properties").toObject().value("context_id").toString();
            if (ctxId.isEmpty()) {
                groups["Sin contexto"].append(task);
            } else {
                const auto ctxObj = findObject(ctxId);
                const QString title = ctxObj.value("title").toString();
                groups[title.isEmpty() ? "Objeto " + ctxId : title].append(task);
            }
        }
    }
    return groups;
}

QString LumenTasks::formatScheduleLabel(const QJsonObject &task) const {
    const auto props = task.value("properties").toObject();
    const QString dateStr = props.value("due_date").toString();
    if (dateStr.isEmpty()) return {};

    const QDate d = QDate::fromString(dateStr, Qt::ISODate);
    const QDate today = QDate::currentDate();
    QString label;
    if (d == today) label = "Hoy";
    else if (d == today.addDays(1)) label = "Mañana";
    else if (d == today.addDays(-1)) label = "Ayer (vencida)";
    else if (d < today) label = QString("Vencida (%1)").arg(d.toString("dd/MM"));
    else label = d.toString("dd MMM");

    const QString time = props.value("time").toString();
    if (!time.isEmpty()) label += " " + time;
    return label;
}

QWidget *LumenTasks::buildTaskRow(const QJsonObject &task) {
    const QString id = task.value("id").toString();
    const auto props = task.value("properties").toObject();
    const QString status = props.value("status").toString().toLower();
    const bool isDone = (status == "done" || status == "completed");
    const int priority = props.value("priority").toInt();
    const QString contextId = props.value("context_id").toString();
    const QString scheduleLabel = formatScheduleLabel(task);
    const bool isOverdue = scheduleLabel.contains("Vencida");

    auto *row = new QFrame(listContainer_);
    row->setObjectName("taskItemRow");
    row->setStyleSheet(QString(R"(
        #taskItemRow {
            background: %1;
            border: 1px solid %2;
            border-radius: 7px;
            padding: 2px 8px;
        }
        #taskItemRow:hover {
            border-color: %3;
        }
    )").arg(dark_ ? "#1f1f23" : "#ffffff")
       .arg(dark_ ? "#2c2c34" : "#e5e5ea")
       .arg(dark_ ? "#4a4a58" : "#c7c7cc"));

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(6, 6, 6, 6);
    rowLayout->setSpacing(8);

    // Checkbox toggle
    auto *checkBtn = new QToolButton(row);
    checkBtn->setCursor(Qt::PointingHandCursor);
    checkBtn->setAutoRaise(true);
    checkBtn->setIcon(isDone ? LumenIcons::icon("circle-check", QColor("#48c08a"), 20)
                            : LumenIcons::icon("circle", QColor("#8e8e93"), 20));
    checkBtn->setIconSize(QSize(20, 20));
    connect(checkBtn, &QToolButton::clicked, this, [this, id, isDone] {
        QJsonObject patch;
        patch.insert("status", isDone ? "todo" : "done");
        if (onUpdateTask) onUpdateTask(id, patch);
        for (auto &t : tasks_) {
            if (t.value("id").toString() == id) {
                auto p = t.value("properties").toObject();
                p.insert("status", isDone ? "todo" : "done");
                t.insert("properties", p);
                break;
            }
        }
        renderList();
    });
    rowLayout->addWidget(checkBtn);

    // Title
    auto *titleBtn = new QPushButton(task.value("title").toString(), row);
    titleBtn->setObjectName("taskTitleBtn");
    titleBtn->setCursor(Qt::PointingHandCursor);
    titleBtn->setFlat(true);
    QFont titleFont("Inter"); titleFont.setPixelSize(14);
    if (isDone) titleFont.setStrikeOut(true);
    titleBtn->setFont(titleFont);
    titleBtn->setStyleSheet(isDone ? "color:#8e8e93; text-align:left; border:none;"
                                  : (dark_ ? "color:#e5e5e7; text-align:left; border:none;" : "color:#1c1c1e; text-align:left; border:none;"));
    connect(titleBtn, &QPushButton::clicked, this, [this, id] {
        if (onOpenObject) onOpenObject(id);
    });
    rowLayout->addWidget(titleBtn, 1);

    // Priority Badge
    if (priority > 0) {
        auto *prioBadge = new QLabel(row);
        const QString prioText = QString("!").repeated(priority);
        prioBadge->setText(prioText);
        const QString prioColor = priority == 3 ? "#e5484d" : (priority == 2 ? "#f76b15" : "#3e63dd");
        prioBadge->setStyleSheet(QString("color:%1; font-weight:bold; font-size:12px; padding:0 4px;").arg(prioColor));
        rowLayout->addWidget(prioBadge);
    }

    // Schedule / Time Badge
    if (!scheduleLabel.isEmpty()) {
        auto *schedBadge = new QLabel(row);
        schedBadge->setText(scheduleLabel);
        schedBadge->setStyleSheet(QString(
            "color:%1; font-size:11.5px; background:%2; border-radius:4px; padding:2px 6px;")
            .arg(isOverdue ? "#e5484d" : (dark_ ? "#a1a1aa" : "#52525b"))
            .arg(isOverdue ? (dark_ ? "#381a1c" : "#ffebee") : (dark_ ? "#27272a" : "#f4f4f5")));
        rowLayout->addWidget(schedBadge);
    }

    // Context Badge
    if (!contextId.isEmpty()) {
        const auto ctxObj = findObject(contextId);
        const QString ctxTitle = ctxObj.value("title").toString();
        auto *ctxBtn = new QPushButton(ctxTitle.isEmpty() ? "Contexto" : ctxTitle, row);
        ctxBtn->setIcon(LumenIcons::icon("link", QColor("#8b5cf6"), 14));
        ctxBtn->setCursor(Qt::PointingHandCursor);
        ctxBtn->setStyleSheet(QString(
            "font-size:11.5px; color:#8b5cf6; background:%1; border:none; border-radius:4px; padding:2px 6px;")
            .arg(dark_ ? "#2a2238" : "#f3e8ff"));
        connect(ctxBtn, &QPushButton::clicked, this, [this, contextId] {
            if (onOpenObject) onOpenObject(contextId);
        });
        rowLayout->addWidget(ctxBtn);
    }

    // Delete tool button
    auto *delBtn = new QToolButton(row);
    delBtn->setAutoRaise(true);
    delBtn->setIcon(LumenIcons::icon("x", QColor("#8e8e93"), 14));
    delBtn->setToolTip("Eliminar tarea");
    connect(delBtn, &QToolButton::clicked, this, [this, id] {
        if (onDeleteTask) onDeleteTask(id);
        for (int i = 0; i < tasks_.size(); ++i) {
            if (tasks_[i].value("id").toString() == id) {
                tasks_.removeAt(i);
                break;
            }
        }
        renderList();
    });
    rowLayout->addWidget(delBtn);

    return row;
}

void LumenTasks::renderList() {
    if (!listLayout_) return;

    // Clear existing widgets
    QLayoutItem *item;
    while ((item = listLayout_->takeAt(0)) != nullptr) {
        if (item->widget() && item->widget() != emptyLabel_) {
            delete item->widget();
        }
        delete item;
    }

    // Today Action bar: "Posponer pendientes a mañana"
    if (activeTab_ == "today") {
        auto *actionRow = new QHBoxLayout;
        actionRow->addStretch();
        auto *postponeBtn = new QPushButton("Posponer pendientes a mañana →", listContainer_);
        postponeBtn->setCursor(Qt::PointingHandCursor);
        postponeBtn->setStyleSheet("color:#e07a5f; font-size:12px; font-weight:500; border:1px solid #e07a5f; border-radius:6px; padding:4px 10px; background:transparent;");
        connect(postponeBtn, &QPushButton::clicked, this, [this] {
            const QDate tomorrow = QDate::currentDate().addDays(1);
            QList<QString> toUpdate;
            for (auto &task : tasks_) {
                const auto props = task.value("properties").toObject();
                const QString status = props.value("status").toString().toLower();
                if (status != "done" && status != "completed") {
                    const QString dStr = props.value("due_date").toString();
                    const QDate d = QDate::fromString(dStr, Qt::ISODate);
                    if (!d.isValid() || d <= QDate::currentDate()) {
                        toUpdate.append(task.value("id").toString());
                        auto p = props;
                        p.insert("due_date", tomorrow.toString(Qt::ISODate));
                        task.insert("properties", p);
                        if (!onBulkReschedule && onUpdateTask)
                            onUpdateTask(task.value("id").toString(), QJsonObject{{"due_date", tomorrow.toString(Qt::ISODate)}});
                    }
                }
            }
            if (onBulkReschedule) onBulkReschedule(toUpdate, tomorrow);
            renderList();
        });
        actionRow->addWidget(postponeBtn);
        listLayout_->addLayout(actionRow);
    }

    const auto currentTasks = tasksForTab(activeTab_);

    if (currentTasks.isEmpty()) {
        emptyLabel_->show();
        listLayout_->addWidget(emptyLabel_);
        return;
    }
    emptyLabel_->hide();

    if (activeTab_ == "scheduled") {
        const auto groups = groupedTasksForTab("scheduled");
        const QList<QPair<QString, QString>> groupMeta = {
            {"1_overdue", "Vencidas"},
            {"2_today", "Hoy"},
            {"3_tomorrow", "Mañana"},
            {"4_this_week", "Esta semana"},
            {"5_next_week", "Próxima semana"},
            {"6_later", "Más adelante"}
        };
        for (const auto &meta : groupMeta) {
            const auto items = groups.value(meta.first);
            if (items.isEmpty()) continue;
            auto *groupLabel = new QLabel(QString("%1 (%2)").arg(meta.second).arg(items.size()), listContainer_);
            groupLabel->setStyleSheet(meta.first == "1_overdue"
                ? "color:#e5484d; font-weight:700; font-size:12px; margin-top:14px; margin-bottom:4px;"
                : "color:#8a8a92; font-weight:600; font-size:12px; margin-top:14px; margin-bottom:4px;");
            listLayout_->addWidget(groupLabel);
            for (const auto &t : items) listLayout_->addWidget(buildTaskRow(t));
        }
    } else if (activeTab_ == "context") {
        const auto groups = groupedTasksForTab("context");
        for (auto it = groups.cbegin(); it != groups.cend(); ++it) {
            auto *groupLabel = new QLabel(QString("%1 (%2)").arg(it.key()).arg(it.value().size()), listContainer_);
            groupLabel->setStyleSheet("color:#8b5cf6; font-weight:600; font-size:12px; margin-top:14px; margin-bottom:4px;");
            listLayout_->addWidget(groupLabel);
            for (const auto &t : it.value()) listLayout_->addWidget(buildTaskRow(t));
        }
    } else {
        for (const auto &t : currentTasks) {
            listLayout_->addWidget(buildTaskRow(t));
        }
    }
}
