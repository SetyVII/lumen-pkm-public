#pragma once

#include <QtWidgets>
#include <functional>

// Parser for single-line task capture (e.g. "Prepare weekly meeting !! tomorrow 2pm @Marketing")
namespace LumenTasksParser {
struct ParsedTask {
    QString title;
    int priority = 0; // 0: None, 1: Low (!), 2: Medium (!!), 3: High (!!!)
    QDate dueDate;
    QTime time;
    QString contextName;
    bool hasDate = false;
    bool hasTime = false;
};

ParsedTask parse(const QString &input, const QDate &referenceDate = QDate::currentDate());
}

class LumenTasks : public QWidget {
public:
    explicit LumenTasks(QWidget *parent = nullptr);

    void setTheme(bool dark);
    void setData(const QJsonArray &objects, const QJsonArray &types);
    void setActiveTab(const QString &tab);
    QString activeTab() const { return activeTab_; }

    // Callbacks to main window
    std::function<void(QString id, QJsonObject patch)> onUpdateTask;
    std::function<void(QJsonObject task)> onCreateTask;
    std::function<void(QString objectId)> onOpenObject;
    std::function<void(QString taskId)> onDeleteTask;
    std::function<void(QList<QString> taskIds, QDate newDate)> onBulkReschedule;
    std::function<void(QString tab)> onTabChanged;

    // Direct access to parsed tasks for unit testing
    QList<QJsonObject> tasksForTab(const QString &tab) const;
    QMap<QString, QList<QJsonObject>> groupedTasksForTab(const QString &tab) const;

private:
    void handleQuickAdd();
    void rebuildUi();
    void renderList();
    QWidget *buildTaskRow(const QJsonObject &task);
    QJsonObject findObject(const QString &id) const;
    QString formatScheduleLabel(const QJsonObject &task) const;

    bool dark_ = false;
    QString activeTab_ = "inbox";
    QList<QJsonObject> allObjects_;
    QList<QJsonObject> types_;
    QList<QJsonObject> tasks_;

    // UI Widgets
    QLineEdit *quickInput_ = nullptr;
    QPushButton *quickAddBtn_ = nullptr;
    QTabWidget *tabBar_ = nullptr;
    QScrollArea *scrollArea_ = nullptr;
    QWidget *listContainer_ = nullptr;
    QVBoxLayout *listLayout_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QPushButton *rescheduleTomorrowBtn_ = nullptr;
};
