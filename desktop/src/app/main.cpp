#include "main_window.hpp"

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Lumen");
    app.setApplicationVersion("0.6.0");
    app.setOrganizationName("Lumen");
    app.setStyle("Fusion");
    QIcon::setThemeName("breeze");
    for (const auto &f : {"Inter-Regular.otf", "Inter-Medium.otf", "Inter-SemiBold.otf", "Inter-Bold.otf"})
        QFontDatabase::addApplicationFont(QString(":/assets/fonts/") + f);
    app.setFont(QFont("Inter", 10));
    MainWindow w;
    for (const auto &arg : app.arguments())
        if (arg.startsWith("--size=")) {
            auto parts = arg.mid(7).split('x');
            if (parts.size() == 2)
                w.resize(parts[0].toInt(), parts[1].toInt());
        }
    w.show();
    QStringList args = app.arguments();
    QString shot;
    int si = args.indexOf("--screenshot");
    if (si >= 0 && si + 1 < args.size())
        shot = args[si + 1];
    bool seed = args.contains("--seed-demo"), smoke = args.contains("--smoke");
    if (seed || !shot.isEmpty() || smoke) {
        auto *t = new QTimer(&w);
        t->setInterval(100);
        auto elapsed = std::make_shared<int>(0);
        QObject::connect(t, &QTimer::timeout, &w, [&w, &app, t, elapsed, seed, shot, smoke, args] {
            (*elapsed)++;
            if (!w.failure.isEmpty()) {
                qCritical().noquote() << w.failure;
                app.exit(2);
                t->stop();
                return;
            }
            if (!w.viewReady()) {
                if (*elapsed > 200) {
                    qCritical() << "Servidor no disponible";
                    app.exit(3);
                }
                return;
            }
            t->stop();
            auto finish = [&w, &app, shot, smoke, args] {
                for (const auto &arg : args) {
                    if (arg.startsWith("--date="))
                        w.navigateJournal(QDate::fromString(arg.mid(7), Qt::ISODate));
                    if (arg.startsWith("--presentation="))
                        w.showCollection("", {{"presentation", arg.mid(15)}});
                }
                if (args.contains("--view=gallery"))
                    w.showCollection("");
                if (args.contains("--view=week"))
                    w.showTimeline(7);
                if (args.contains("--view=three"))
                    w.showTimeline(3);
                if (args.contains("--view=graph"))
                    w.showGraph();
                QTimer::singleShot(1200, &w, [&w, &app, shot, smoke, args] {
                    if (!shot.isEmpty())
                        w.screenshot(shot);
                    if (smoke) {
                        qInfo() << "NATIVE_SMOKE_OK";
                        app.exit(0);
                    } else if (args.contains("--exit-after-screenshot"))
                        app.exit(0);
                });
            };
            if (seed)
                w.seedDemo(finish);
            else
                finish();
        });
        t->start();
    }
    return app.exec();
}
