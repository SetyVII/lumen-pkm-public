#include "lumen_tasks.hpp"
#include <QtTest>
#include <cstdio>

namespace {
void require(bool cond, const char *msg) {
    if (!cond) {
        qCritical() << "TASKS_TEST_FAILED:" << msg;
        throw std::runtime_error(msg);
    }
}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        const QDate refDate(2026, 10, 30); // Reference Friday

        // 1. Natural Language Parser Tests
        {
            auto p1 = LumenTasksParser::parse("Prepare weekly meeting !! tomorrow 2pm @Marketing", refDate);
            require(p1.title == "Prepare weekly meeting", "title parsed cleanly without metatags");
            require(p1.priority == 2, "priority !! parsed as 2 (Medium)");
            require(p1.hasDate && p1.dueDate == QDate(2026, 10, 31), "tomorrow parses to next day");
            require(p1.hasTime && p1.time == QTime(14, 0), "2pm parses to 14:00");
            require(p1.contextName == "Marketing", "context parses to Marketing");

            auto p2 = LumenTasksParser::parse("Comprar pan ! hoy @Personal", refDate);
            require(p2.title == "Comprar pan", "title is Comprar pan");
            require(p2.priority == 1, "priority ! parsed as 1");
            require(p2.hasDate && p2.dueDate == refDate, "hoy parses to refDate");
            require(p2.contextName == "Personal", "context parsed as Personal");

            auto p3 = LumenTasksParser::parse("Urgente !!!", refDate);
            require(p3.title == "Urgente", "title is Urgente");
            require(p3.priority == 3, "priority !!! parsed as 3");
            require(!p3.hasDate, "no date specified");

            auto p4 = LumenTasksParser::parse("Auditoría 2026-11-05 15:30", refDate);
            require(p4.hasDate && p4.dueDate == QDate(2026, 11, 5), "ISO date parsed");
            require(p4.hasTime && p4.time == QTime(15, 30), "15:30 parsed");
            require(p4.title == "Auditoría", "title is Auditoría");
        }

        // 2. LumenTasks Widget Tabs & Filtering
        {
            LumenTasks view;
            view.resize(800, 600);

            QJsonArray types{
                QJsonObject{{"id", "task"}, {"name", "Tarea"}, {"icon", "circle-check"}, {"color", "#e07a5f"}},
                QJsonObject{{"id", "project"}, {"name", "Proyecto"}, {"icon", "folder"}, {"color", "#7c90b9"}}
            };

            const QString todayStr = QDate::currentDate().toString(Qt::ISODate);
            const QString yesterdayStr = QDate::currentDate().addDays(-1).toString(Qt::ISODate);
            const QString tomorrowStr = QDate::currentDate().addDays(1).toString(Qt::ISODate);

            QJsonArray objects{
                QJsonObject{{"id", "proj1"}, {"type_id", "project"}, {"title", "Marketing Sync"}},
                // Task 1: Inbox (undated, open)
                QJsonObject{{"id", "t1"}, {"type_id", "task"}, {"title", "Tarea en Inbox"},
                            {"properties", QJsonObject{{"status", "todo"}, {"priority", 1}}}},
                // Task 2: Today (due today, open)
                QJsonObject{{"id", "t2"}, {"type_id", "task"}, {"title", "Tarea de Hoy"},
                            {"properties", QJsonObject{{"status", "todo"}, {"due_date", todayStr}, {"priority", 2}, {"context_id", "proj1"}}}},
                // Task 3: Overdue (due yesterday, open -> must show in Today and Scheduled)
                QJsonObject{{"id", "t3"}, {"type_id", "task"}, {"title", "Tarea Vencida"},
                            {"properties", QJsonObject{{"status", "todo"}, {"due_date", yesterdayStr}, {"priority", 3}}}},
                // Task 4: Scheduled tomorrow
                QJsonObject{{"id", "t4"}, {"type_id", "task"}, {"title", "Tarea de Mañana"},
                            {"properties", QJsonObject{{"status", "todo"}, {"due_date", tomorrowStr}}}},
                // Task 5: Completed
                QJsonObject{{"id", "t5"}, {"type_id", "task"}, {"title", "Tarea Hecha"},
                            {"properties", QJsonObject{{"status", "done"}, {"due_date", todayStr}}}}
            };

            view.setData(objects, types);

            // Test Inbox
            auto inboxTasks = view.tasksForTab("inbox");
            require(inboxTasks.size() == 1 && inboxTasks.first().value("id").toString() == "t1",
                    "Inbox must only contain undated open tasks");

            // Test Today
            auto todayTasks = view.tasksForTab("today");
            // Must contain: t2 (due today), t3 (overdue), and t5 (done today)
            require(todayTasks.size() == 3, "Today must contain due today, overdue, and completed today");
            // Sorting check: t3 has priority 3, t2 has priority 2 -> t3 comes before t2
            require(todayTasks.first().value("id").toString() == "t3", "Highest priority task must rank first in Today");

            // Test Scheduled
            auto schedTasks = view.tasksForTab("scheduled");
            require(schedTasks.size() == 3, "Scheduled must contain all open dated tasks (t3, t2, t4)");
            auto schedGroups = view.groupedTasksForTab("scheduled");
            require(schedGroups.contains("1_overdue") && schedGroups["1_overdue"].size() == 1, "Scheduled groups overdue tasks");
            require(schedGroups.contains("2_today") && schedGroups["2_today"].size() == 1, "Scheduled groups today tasks");
            require(schedGroups.contains("3_tomorrow") && schedGroups["3_tomorrow"].size() == 1, "Scheduled groups tomorrow tasks");

            // Test Context
            auto ctxGroups = view.groupedTasksForTab("context");
            require(ctxGroups.contains("Marketing Sync"), "Context groups tasks by linked project name");
            require(ctxGroups["Marketing Sync"].first().value("id").toString() == "t2", "t2 is linked to Marketing Sync");

            // Test Open
            auto openTasks = view.tasksForTab("open");
            require(openTasks.size() == 4, "Open must contain all 4 uncompleted tasks (t1, t2, t3, t4)");

            // Test Completed
            auto completedTasks = view.tasksForTab("completed");
            require(completedTasks.size() == 1 && completedTasks.first().value("id").toString() == "t5",
                    "Completed must contain t5");

            // Test Quick Add
            auto *input = view.findChild<QLineEdit *>("taskQuickInput");
            require(input != nullptr, "Quick input widget must exist");
            input->setText("Revisar contratos !!! tomorrow @Marketing Sync");
            bool created = false;
            view.onCreateTask = [&](const QJsonObject &task) {
                created = true;
                require(task.value("title").toString() == "Revisar contratos", "Task title created cleanly");
                require(task.value("properties").toObject().value("priority").toInt() == 3, "Priority is 3");
                require(task.value("properties").toObject().value("context_id").toString() == "proj1", "Context linked to proj1");
            };
            auto *btn = view.findChild<QPushButton *>("taskQuickAddBtn");
            btn->click();
            require(created, "Quick add creates task and invokes callback");

            // Test Tab Switching
            view.setActiveTab("scheduled");
            require(view.activeTab() == "scheduled", "setActiveTab switches active tab");
        }

        std::puts("TASKS_TEST_OK");
    } catch (const std::exception &) {
        return 1;
    }
    return 0;
}
