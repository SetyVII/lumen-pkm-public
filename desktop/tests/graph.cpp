#include "lumen_graph.hpp"
#include <QtTest>
#include <QTemporaryDir>
#include <cstdio>

static void require(bool condition, const char *message) {
    if (!condition) { qCritical() << "GRAPH_TEST_FAILED:" << message; throw std::runtime_error(message); }
}
int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setStyle("Fusion");
    try {
        LumenGraph graph;
        graph.resize(900, 650);
        graph.setTheme(false, true);
        graph.show();
        QJsonArray objects;
        for (int i = 0; i < 90; ++i) {
            QJsonObject object{{"id", "object-" + QString::number(i)}, {"title", "Idea ficticia " + QString::number(i)},
                               {"type_id", i % 2 == 0 ? "idea" : "book"}};
            if (i < 89) object.insert("links", QJsonArray{"object-" + QString::number(i + 1)});
            if (i == 0) {
                object.insert("property_links", QJsonArray{"object-1", "object-2", "missing"});
                object.insert("properties", QJsonObject{{"recomendado_por", "object-2"}, {"summary", "Aprender, sin perder la curiosidad"}});
            }
            objects.append(object);
        }
        graph.setData(objects, QJsonArray{QJsonObject{{"id", "idea"}, {"name", "Idea"}}, QJsonObject{{"id", "book"}, {"name", "Libro"}}});
        require(graph.nodeCount() == 90 && graph.visibleObjectIds().size() == 90, "all ninety objects must be present, with no old forty-five limit");
        require(graph.edgeCount() == 90, "links and property relations must share one deduplicated directed edge per pair");
        graph.setTypeFilter("book");
        require(graph.visibleObjectIds().size() == 45, "type filtering must select IDs without mutating objects");
        graph.setSearch("ficticia 1");
        require(graph.visibleObjectIds().size() == 6, "text and type filters must combine");
        graph.setSearch("missing");
        require(graph.visibleObjectIds().isEmpty(), "empty search must be safe and honest");
        graph.setSearch(""); graph.setTypeFilter("");
        graph.focusObject("object-0");
        QString opened;
        graph.onOpen = [&opened](QString id) { opened = id; };
        QTest::keyClick(&graph, Qt::Key_Return);
        require(opened == "object-0", "Enter must open the focused object ID");
        QTest::keyClick(&graph, Qt::Key_Right);
        require(!graph.selectedObjectId().isEmpty(), "arrow keys must select a spatial neighbor");
        graph.zoomIn(); graph.zoomOut(); graph.fitToContents();
        QTemporaryDir directory;
        require(directory.isValid() && graph.exportImage(directory.filePath("fictional-graph.png")), "explicit PNG export must succeed");
        QImage image(directory.filePath("fictional-graph.png"));
        require(!image.isNull() && image.width() == 1800 && image.height() == 1300, "export must render a crisp complete image");
        graph.setTheme(true, true);
        graph.setData({}, {});
        require(graph.nodeCount() == 0 && graph.edgeCount() == 0 && graph.visibleObjectIds().isEmpty(), "empty graph must clear stale nodes and relations");
        // A larger dataset verifies that layout accepts every ID and avoids the old fixed cap.
        QJsonArray many;
        for (int i = 0; i < 1800; ++i) many.append(QJsonObject{{"id", "many-" + QString::number(i)}, {"title", "Objeto " + QString::number(i)}, {"type_id", "note"}});
        QElapsedTimer elapsed; elapsed.start();
        graph.setData(many, {});
        require(graph.nodeCount() == 1800 && graph.visibleObjectIds().size() == 1800, "large graphs must retain every object");
        require(elapsed.elapsed() < 3000, "spatial layout must avoid a multi-second quadratic setup for 1800 isolated objects");
        std::printf("GRAPH_OK: 90 objects, explicit/property relation union, filters, keyboard focus/open, zoom/fit, PNG export, empty and 1800-object layout (%lld ms)\n", static_cast<long long>(elapsed.elapsed()));
        return 0;
    } catch (const std::exception &exception) { std::fprintf(stderr, "%s\n", exception.what()); return 1; }
}
