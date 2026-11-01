#include "../src/views/lumen_views.hpp"
#include <QtWidgets>
#include <QtTest>
#include <QBuffer>
#include <cstdio>

static bool check(bool value,const char *message){if(!value)fprintf(stderr,"FAIL: %s\n",message);return value;}
static QStringList ids(QJsonArray result){QStringList out;for(auto v:result)out<<v.toObject().value("id").toString();return out;}
int main(int argc,char **argv){
    QApplication app(argc,argv);bool pass=true;
    QFile shared(QFINDTESTDATA("../../contracts/fixtures/saved-views.json"));
    pass &= check(shared.open(QIODevice::ReadOnly), "shared saved view fixture opens");
    const auto fixture = QJsonDocument::fromJson(shared.readAll()).object();
    for (const auto &value : fixture.value("cases").toArray()) {
        const auto scenario = value.toObject();
        pass &= check(ids(LumenQuery::evaluate(fixture.value("objects").toArray(), {}, scenario.value("spec").toObject())) ==
            ids([&] { QJsonArray result; for (const auto &id : scenario.value("ids").toArray()) result.append(QJsonObject{{"id", id}}); return result; }()),
            qPrintable(scenario.value("name").toString()));
    }
    const QJsonArray types{
        QJsonObject{{"id","book"},{"name","Libro"},{"properties",QJsonArray{
            QJsonObject{{"key","pages"},{"type","integer"},{"min",0}},
            QJsonObject{{"key","status"},{"type","select"},{"options",QJsonArray{"Pendiente","Leyendo","Leído"}}},
            QJsonObject{{"key","related"},{"type","relation"},{"target_type","book"}},
            QJsonObject{{"key","date"},{"type","date"}}}}},
        QJsonObject{{"id","web"},{"name","Enlace web"},{"properties",QJsonArray{"url"}}}};
    const QJsonArray objects{
        QJsonObject{{"id","a"},{"type_id","book"},{"title","Bosque"},{"content","<b>Estudio de árboles</b>"},{"created_at","2026-10-01T10:00:00Z"},{"updated_at","2026-10-01T10:00:00Z"},{"links",QJsonArray{"b"}},{"properties",QJsonObject{{"pages",80},{"status","Leyendo"},{"image_asset_id","fake-cover"}}}},
        QJsonObject{{"id","b"},{"type_id","book"},{"title","Río"},{"content","Agua y árboles"},{"created_at","2026-10-02T10:00:00Z"},{"updated_at","2026-10-02T10:00:00Z"},{"links",QJsonArray{}},{"properties",QJsonObject{{"pages",400}}}},
        QJsonObject{{"id","c"},{"type_id","web"},{"title","Árbol"},{"content","Enlace externo"},{"created_at","2026-10-03T10:00:00Z"},{"updated_at","2026-10-03T10:00:00Z"},{"links",QJsonArray{"b"}},{"properties",QJsonObject{{"url","https://example.com"},{"image_asset_id","fake-cover"}}}},
        QJsonObject{{"id","j"},{"type_id","journal"},{"title","2026-10-29"}}};
    QJsonObject spec{{"type_id","book"},{"search","árboles"},{"sort",QJsonArray{QJsonObject{{"field","title"},{"direction","asc"}}}},
        {"filter",QJsonObject{{"mode","or"},{"rules",QJsonArray{
            QJsonObject{{"field","properties.status"},{"op","equals"},{"value","Leyendo"}},
            QJsonObject{{"mode","and"},{"rules",QJsonArray{QJsonObject{{"field","created_at"},{"op","after"},{"value","2026-10-01T20:00:00Z"}},QJsonObject{{"field","backlinks"},{"op","equals"},{"value","a"}}}}}}}}}};
    pass&=check(ids(LumenQuery::evaluate(objects,types,spec))==QStringList{"a","b"},"AND/OR nested query, type, text, backlink and date share one ID set");
    auto limited=spec;limited["limit"]=1;pass&=check(ids(LumenQuery::evaluate(objects,types,limited))==QStringList{"a"},"limit follows sorting");
    // Collections keep their curated order: "order": "manual" follows the ids
    // array instead of the default updated_at sort; an explicit sort wins.
    const QJsonObject manual{{"ids",QJsonArray{"b","a"}},{"order","manual"}};
    pass&=check(ids(LumenQuery::evaluate(objects,types,manual))==QStringList{"b","a"},"manual order follows the curated ids array");
    auto curated=manual;curated["search"]="Estudio";
    pass&=check(ids(LumenQuery::evaluate(objects,types,curated))==QStringList{"a"},"search filters inside a curated list");
    auto curatedSorted=manual;curatedSorted["sort"]=QJsonArray{QJsonObject{{"field","title"},{"direction","asc"}}};
    pass&=check(ids(LumenQuery::evaluate(objects,types,curatedSorted))==QStringList{"a","b"},"explicit sort overrides the curated order until cleared");
    pass&=check(LumenQuery::evaluate(objects,types,{}).size()==3,"journals excluded from generic collections");
    auto links=QJsonObject{{"filter",QJsonObject{{"rules",QJsonArray{QJsonObject{{"field","links"},{"op","equals"},{"value","b"}}}}}}};
    pass&=check(ids(LumenQuery::evaluate(objects,types,links))==QStringList{"c","a"},"forward relation matches current objects");
    pass&=check(LumenQuery::plainText(objects[0].toObject())=="Estudio de árboles","HTML is plain searchable content");
    auto relationObjects=objects;auto relationObject=relationObjects[2].toObject();relationObject["property_links"]=QJsonArray{"a","b"};relationObjects[2]=relationObject;
    pass&=check(LumenQuery::fieldValue(relationObject,"links",relationObjects).toArray()==QJsonArray{"b","a"},"explicit and property links form a deduplicated union");
    pass&=check(LumenQuery::fieldValue(relationObjects[0].toObject(),"backlinks",relationObjects).toArray().contains("c"),"property relation contributes backlinks");
    const auto a=objects[0].toObject();
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.pages","hello",objects).valid,"numeric validation rejects text");
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.pages","-5",objects).valid,"numeric min validation");
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.pages","4.5",objects).valid,"integer validation");
    pass&=check(LumenQuery::validateEdit(a,types,"properties.pages","93",objects).patch.value("properties").toObject().value("pages").isDouble(),"numbers retain numeric JSON type");
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.status","Inventado",objects).valid,"select validation");
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.related","missing",objects).valid,"missing relationship validation");
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.related","c",objects).valid,"mixed type relationship validation");
    pass&=check(LumenQuery::validateEdit(a,types,"properties.related","b",objects).valid,"existing correctly typed relationship");
    pass&=check(!LumenQuery::validateEdit(a,types,"properties.date","2026-02-31",objects).valid,"calendar validation");
    pass&=check(!LumenQuery::validateEdit(a,types,"title"," ",objects).valid,"empty title validation");
    pass&=check(!LumenQuery::validateEdit(a,types,"created_at","yesterday",objects).valid,"system fields read only");
    pass&=check(!LumenQuery::validateEdit(objects[2].toObject(),types,"properties.pages","42",objects).valid,"mixed type table cannot invent a foreign property");
    LumenViews view;view.setObjectName("workspaceViews");view.setTheme(true,false);view.resize(920,700);int requests=0;QJsonObject lastPatch;QString patchId;
    QPixmap fake(80,150);fake.fill(Qt::red);QByteArray fakeBytes;QBuffer buffer(&fakeBytes);buffer.open(QIODevice::WriteOnly);fake.save(&buffer,"PNG");
    view.onAssetRequest=[&](QString,std::function<void(QByteArray,QString)> callback){++requests;callback(fakeBytes,{});};
    view.onPatch=[&](QString id,QJsonObject patch){patchId=id;lastPatch=patch;};
    view.setData(objects,types);view.setScope("book",spec);view.show();QTest::qWait(80);
    auto *search=view.findChild<QLineEdit*>("viewSearch");search->ensurePolished();
    pass&=check(search->font().pixelSize()==13,"collection styles survive the host assigning an external objectName");
    const auto expected=view.visibleIds();
    for(const auto &mode:QStringList{"list","gallery","table","wall","kanban","embed"}){
        view.setPresentation(mode);QTest::qWait(30);pass&=check(view.visibleIds()==expected,qPrintable("same IDs in "+mode));
    }
    pass&=check(requests==1,"cover bytes cached in memory across presentations");
    view.resize(1120,700);view.setPresentation("gallery");QTest::qWait(100);
    auto cards=view.findChildren<QAbstractButton*>("lumenObjectCard");
    bool fourColumns=!cards.isEmpty();for(auto *card:cards)if(card->isVisible()&&card->width()>310)fourColumns=false;
    pass&=check(fourColumns,"responsive gallery uses four columns at 1120 px without provisional viewport reflow loops");
    // Overview is a presentation of the same scope, with a distinct recent-open order.
    LumenViews collection;collection.setTheme(true,true);collection.resize(920,720);
    pass&=check(collection.findChild<QToolButton*>("collectionOverview")->isHidden()&&collection.findChild<QToolButton*>("collectionAll")->isHidden(),"embedded views initially hide collection navigation");
    collection.setCollectionMode(false);collection.show();QTest::qWait(20);
    pass&=check(collection.findChild<QToolButton*>("collectionAll")->isVisible()&&collection.viewSpec().value("collection_mode")=="all","first explicit all-mode call activates navigation and persists the mode");
    int imported=0;QString createdType,openedId;
    collection.onCreate=[&](QString type){createdType=type;};collection.onImport=[&]{++imported;};collection.onOpen=[&](QString id){openedId=id;};
    collection.onAssetRequest=[&](QString,std::function<void(QByteArray,QString)> callback){callback(fakeBytes,{});};
    collection.setData(objects,types);collection.setScope("book");collection.setRecentIds({"c","a","b","a","missing"});collection.setCollectionMode(true);collection.show();QTest::qWait(50);
    auto *openedSection=collection.findChild<QWidget*>("overviewRecentlyOpened");auto *createdSection=collection.findChild<QWidget*>("overviewRecentlyCreated");
    pass&=check(collection.collectionMode()&&collection.visibleIds()==QStringList{"b","a"},"overview keeps the complete scoped evaluator ID set");
    auto *overviewTab=collection.findChild<QToolButton*>("collectionOverview");auto *allTab=collection.findChild<QToolButton*>("collectionAll");
    pass&=check(overviewTab->isChecked()&&!allTab->isChecked()&&collection.viewSpec().value("collection_mode")=="overview","overview navigation pill and persisted mode agree");
    pass&=check(overviewTab->geometry().center().y()==collection.findChild<QToolButton*>("overviewPersonalize")->geometry().center().y(),"collection tabs and overview controls occupy the same row");
    pass&=check(openedSection&&openedSection->property("objectIds").toStringList()==QStringList{"a","b"},"recent-open history preserves order, removes duplicates and excludes foreign or missing IDs");
    pass&=check(createdSection&&createdSection->property("objectIds").toStringList()==QStringList{"b","a"},"recently created uses creation time within the current scope");
    pass&=check(!collection.findChild<QLineEdit*>("viewSearch")->isVisible()&&collection.findChild<QToolButton*>("overviewPersonalize")->isVisible(),"overview exposes section customization instead of collection controls");
    bool framed=false;for(auto *card:collection.findChildren<QAbstractButton*>("lumenObjectCard"))if(card->isVisible()&&card->property("objectId")=="a")framed=card->property("coverHeight")==card->width()-28&&card->height()==card->property("coverHeight").toInt()+164;
    pass&=check(framed,"gallery covers use a square contain frame below the type badge without clipping metadata");
    const QString captures=qEnvironmentVariable("LUMEN_VIEWS_CAPTURE_DIR");if(!captures.isEmpty()){QDir().mkpath(captures);collection.grab().save(captures+"/overview.png");}
    collection.setRecentIds({"missing","c"});QTest::qWait(30);
    pass&=check(!collection.findChild<QWidget*>("overviewRecentlyOpened")&&collection.findChild<QWidget*>("overviewRecentlyCreated"),"empty recent history falls back to recently created objects");
    QTest::mouseClick(allTab,Qt::LeftButton);QTest::qWait(30);
    pass&=check(!collection.collectionMode()&&allTab->isChecked()&&collection.viewSpec().value("collection_mode")=="all","Todo navigation button changes mode and records the snapshot");
    pass&=check(allTab->geometry().center().y()==collection.findChild<QToolButton*>("viewFilters")->parentWidget()->geometry().center().y(),"Todo navigation and grouped collection controls occupy the same row");
    QTest::mouseClick(overviewTab,Qt::LeftButton);QTest::qWait(30);
    pass&=check(collection.collectionMode()&&overviewTab->isChecked()&&collection.viewSpec().value("collection_mode")=="overview","overview navigation button restores overview mode");
    collection.setScope("web");collection.setCollectionMode(false);QTest::qWait(30);
    pass&=check(collection.findChild<QLineEdit*>("viewSearch")->isVisible()&&!collection.findChild<QComboBox*>("viewSort")->isVisible()&&!collection.findChild<QComboBox*>("viewPresentation")->isVisible(),"Todo exposes one compact toolbar with selectors inside menus");
    auto *copy=collection.findChild<QToolButton*>("cardCopyUrl");if(copy)QTest::mouseClick(copy,Qt::LeftButton);
    pass&=check(copy&&QApplication::clipboard()->text()=="https://example.com"&&openedId.isEmpty(),"copy URL is an independent card action and does not open the object");
    auto *render=collection.findChild<QToolButton*>("viewRenderButton");QAction *listAction=nullptr;
    if(render&&render->menu())for(auto *action:render->menu()->actions())if(action->data()=="list")listAction=action;
    if(listAction)listAction->trigger();
    QTest::qWait(30);
    pass&=check(listAction&&collection.viewSpec().value("presentation")=="list"&&collection.visibleIds()==QStringList{"c"},"representation menu changes layout without changing query IDs");
    collection.setScope("book");QTest::qWait(30);auto *sortTool=collection.findChild<QToolButton*>("viewSortButton");QAction *ascendingTitle=nullptr;
    if(sortTool&&sortTool->menu())for(auto *action:sortTool->menu()->actions())if(action->menu()&&action->text()=="Nombre")ascendingTitle=action->menu()->actions().value(0);
    if(ascendingTitle)ascendingTitle->trigger();
    QTest::qWait(30);
    pass&=check(ascendingTitle&&collection.visibleIds()==QStringList{"a","b"},"sort popover action orders the current scope through the shared evaluator");
    bool popup=false;QTimer::singleShot(30,&collection,[&]{auto *dialog=collection.findChild<QDialog*>("viewFilterPopover");popup=dialog&&dialog->windowFlags().testFlag(Qt::Popup);if(dialog)dialog->reject();});
    QTest::mouseClick(collection.findChild<QToolButton*>("viewFilters"),Qt::LeftButton);
    pass&=check(popup,"filter button opens an anchored native popover");
    collection.setScope("project");collection.setPresentation("table");QTest::qWait(30);
    auto *emptyTitle=collection.findChild<QLabel*>("viewEmpty");auto *newEmpty=collection.findChild<QToolButton*>("emptyCreate");auto *importEmpty=collection.findChild<QToolButton*>("emptyImport");
    pass&=check(emptyTitle&&emptyTitle->text()=="Aquí no hay nada (todavía)."&&newEmpty&&importEmpty,"a truly empty type displays the reference empty state even in table mode");
    if(newEmpty)QTest::mouseClick(newEmpty,Qt::LeftButton);
    if(importEmpty)QTest::mouseClick(importEmpty,Qt::LeftButton);
    pass&=check(createdType=="project"&&imported==1,"empty actions emit scoped create and native import callbacks");
    if(!captures.isEmpty())collection.grab().save(captures+"/empty.png");
    collection.setScope("book",QJsonObject{{"search","no-such-result"}});QTest::qWait(30);auto *clear=collection.findChild<QToolButton*>("clearViewFilters");
    pass&=check(clear&&!collection.findChild<QToolButton*>("emptyCreate")&&!collection.findChild<QWidget*>("collectionEmptyIllustration"),"filtered empty results have their own clear-filter action");
    if(clear)QTest::mouseClick(clear,Qt::LeftButton);
    QTest::qWait(30);
    pass&=check(collection.visibleIds()==QStringList{"b","a"}&&collection.viewSpec().value("type_id")=="book","clearing filters restores the current type's ID set");
    auto board=spec;board.remove("filter");board.remove("search");board["group_by"]="properties.status";board["presentation"]="kanban";
    view.setScope("book",board);pass&=check(view.applyKanbanMove("a","Leído"),"kanban drop accepted");QTest::qWait(20);
    pass&=check(patchId=="a"&&lastPatch.keys()==QStringList{"properties"},"kanban changes only property patch");
    pass&=check(lastPatch.value("properties").toObject().value("status")=="Leído"&&lastPatch.value("properties").toObject().value("pages")==80,"kanban preserves unrelated properties");
    view.undoLastMove();pass&=check(lastPatch.value("properties").toObject().value("status")=="Leyendo","undo restores original property");
    pass&=check(!view.applyKanbanMove("a","Inventado"),"kanban validates enum options");
    view.setPresentation("table");QTest::qWait(20);auto *table=view.findChild<QTableView*>("objectsTable");
    pass&=check(table&&table->model()->columnCount()>=2&&table->horizontalHeader()->sectionsMovable(),"real table with movable property columns");
    auto current=view.viewSpec();current["visible_properties"]=QJsonArray{"title","properties.pages"};view.setScope("book",current);QTest::qWait(20);table=view.findChild<QTableView*>("objectsTable");
    // Most recently updated book b appears first under the default sort.
    pass&=check(table&&table->model()->setData(table->model()->index(0,1),"402",Qt::EditRole),"table edits typed property");
    pass&=check(lastPatch.value("properties").toObject().value("pages").isDouble(),"table emits numeric patch");
    QTest::qWait(30);table=view.findChild<QTableView*>("objectsTable");
    table->setCurrentIndex(table->model()->index(1,0));const QString selected=table->currentIndex().data(Qt::UserRole).toString();
    table->horizontalHeader()->resizeSection(0,333);table->horizontalHeader()->moveSection(0,1);
    const auto persisted=view.viewSpec();
    pass&=check(persisted.value("visible_properties").toArray().first()=="properties.pages"&&persisted.value("column_widths").toObject().value("title")==333,"saved specification retains reordered/resized columns");
    view.setData(objects,types);QTest::qWait(30);table=view.findChild<QTableView*>("objectsTable");
    pass&=check(table->currentIndex().data(Qt::UserRole).toString()==selected,"polling retains selected object identity");
    table->edit(table->model()->index(0,0));QTest::qWait(20);auto *number=table->findChild<QDoubleSpinBox*>();
    pass&=check(number,"integer property uses a native numeric editor");
    if(number){number->setFocus();number->setValue(551);view.setData(objects,types);QTest::qWait(40);pass&=check(table->findChild<QDoubleSpinBox*>()==number&&number->value()==551,"background refresh does not destroy an active cell editor");QTest::keyClick(number,Qt::Key_Escape);QTest::qWait(240);}
    if(pass)fprintf(stdout,"LUMEN_VIEWS_OK: query AND/OR, links/backlinks, dates, sort/limit, typed edits, six presentations, memory covers, overview history, compact controls, scoped empty actions, kanban and undo\n");
    return pass?0:1;
}
