// Runs only against an explicitly authorized, disposable API on a non-production port.
#include "../src/app/main_window.hpp"
#include <QScopeGuard>
#include <QPdfWriter>
#include <QtTest>
#include <cstdio>
#include <stdexcept>

static QJsonValue acceptanceRequest(Api &api,const QString &method,const QString &path,const QJsonObject &body={}){
    QEventLoop loop;QJsonValue result;QString error;bool complete=false;
    api.request(method,path,body,[&](QJsonValue value,QString problem){result=value;error=problem;complete=true;loop.quit();});
    QTimer::singleShot(20000,&loop,&QEventLoop::quit);if(!complete)loop.exec();
    if(!complete||!error.isEmpty())throw std::runtime_error((path+": "+(complete?error:"timeout")).toStdString());
    return result;
}
static bool acceptanceUntil(std::function<bool()> condition,int timeout=12000){
    QElapsedTimer timer;timer.start();while(timer.elapsed()<timeout){if(condition())return true;QTest::qWait(20);}return condition();
}
static void acceptanceRequire(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class Widget> static Widget *acceptanceVisible(MainWindow &window,const QString &name){
    for(auto *widget:window.findChildren<Widget*>(name))if(widget->isVisible())return widget;
    return nullptr;
}
static QPoint acceptanceReferencePoint(RichEditor *editor,const QString &id){
    for(auto block=editor->document()->begin();block.isValid();block=block.next()){
        for(auto it=block.begin();!it.atEnd();++it){
            const auto fragment=it.fragment();
            if(!fragment.isValid()||fragment.charFormat().isImageFormat()||
               fragment.charFormat().anchorHref()!="lumen://object/"+id)continue;
            QTextCursor cursor(editor->document());
            const int position=fragment.position()+qMin(2,fragment.length()-1);
            cursor.setPosition(position);const auto left=editor->cursorRect(cursor);
            cursor.setPosition(position+1);const auto right=editor->cursorRect(cursor);
            return QPoint((left.left()+right.left())/2,left.center().y());
        }
    }
    throw std::runtime_error("Clickable text of the object mention was not found");
}

int main(int argc,char **argv){
    QApplication application(argc,argv);application.setStyle("Fusion");
    for(const QString &font:QStringList{"Regular","Medium","SemiBold","Bold"})QFontDatabase::addApplicationFont(":/assets/fonts/Inter-"+font+".otf");
    const auto base=QUrl(qEnvironmentVariable("LUMEN_API_URL"));
    if(qEnvironmentVariable("LUMEN_ACCEPTANCE_DISPOSABLE")!="1"||base.port(8765)==8765||
        !QStringList{"127.0.0.1","localhost","::1"}.contains(base.host())){
        fprintf(stderr,"Acceptance requires LUMEN_ACCEPTANCE_DISPOSABLE=1 and a disposable loopback API on a port other than 8765.\n");return 64;
    }
    Api api;QList<QJsonObject> originalJournals;QStringList fixtureObjects;
    auto cleanup=qScopeGuard([&]{
        for(const auto &object:originalJournals){try{acceptanceRequest(api,"PATCH","/v1/objects/"+str(object,"id"),{{"content",object.value("content")},{"properties",object.value("properties")},{"links",object.value("links")}});}catch(...){}}
        for(const auto &id:fixtureObjects){try{acceptanceRequest(api,"DELETE","/v1/objects/"+id);}catch(...){}}
    });
    try{
        const QDate dateA(2098,11,16),dateB=dateA.addDays(1),dateC=dateA.addDays(2);
        const QString nonce=QString::number(QDateTime::currentMSecsSinceEpoch());
        const QString markerA="Acceptance A "+nonce,markerB="Acceptance B "+nonce,markerC="Acceptance C "+nonce;
        for(auto pair:QList<QPair<QDate,QString>>{{dateA,markerA},{dateB,markerB},{dateC,markerC}}){
            const auto journal=acceptanceRequest(api,"GET","/v1/journal/"+pair.first.toString(Qt::ISODate)).toObject();originalJournals<<journal;
            acceptanceRequest(api,"PATCH","/v1/objects/"+str(journal,"id"),{{"content",pair.second},{"links",QJsonArray{}}});
        }
        // Types have no DELETE endpoint. This uniquely named schema stays only
        // in the explicitly disposable graph; existing types remain untouched.
        const auto emptyType=acceptanceRequest(api,"POST","/v1/types",{{"name","Tipo vacio de aceptacion "+nonce},{"icon","folder"},{"color","#789b88"},{"properties",QJsonArray{}}}).toObject();
        const QString emptyTypeId=str(emptyType,"id");acceptanceRequire(!emptyTypeId.isEmpty(),"Could not create the disposable empty type");
        const QString bookQueryMarker="book-query-"+nonce;
        const auto book=acceptanceRequest(api,"POST","/v1/objects",{{"type_id","book"},{"title","Libro de aceptación "+nonce},{"content","Un libro para verificar relaciones y vistas. "+bookQueryMarker},{"properties",QJsonObject{{"author","Autor ficticio"},{"status","Leyendo"}}},{"links",QJsonArray{}}}).toObject();
        fixtureObjects<<str(book,"id");
        MainWindow window;window.resize(1448,650);window.show();window.activateWindow();
        auto snapshot=[&](const QString &name,QWidget *widget=nullptr){const QString folder=qEnvironmentVariable("LUMEN_ACCEPTANCE_SCREENSHOTS");if(folder.isEmpty())return;QDir().mkpath(folder);QTest::qWait(120);(widget?widget:&window)->grab().save(folder+"/"+name+".png");};
        acceptanceRequire(acceptanceUntil([&]{return window.ready&&acceptanceVisible<QTextEdit>(window,"editor");}),"Native startup failed");
        QElapsedTimer navigation;navigation.start();window.navigateJournal(dateA);
        acceptanceRequire(acceptanceUntil([&]{auto *editor=acceptanceVisible<QTextEdit>(window,"editor");return editor&&editor->toPlainText().contains(markerA);}),"Date A content did not load");
        const qint64 initialMs=navigation.elapsed();
        auto *editor=acceptanceVisible<QTextEdit>(window,"editor");editor->setFocus();editor->moveCursor(QTextCursor::End);QTest::keyClicks(editor," saved-before-navigation");
        navigation.restart();window.navigateJournal(dateB);window.navigateJournal(dateC);
        acceptanceRequire(acceptanceUntil([&]{auto *e=acceptanceVisible<QTextEdit>(window,"editor");return e&&e->toPlainText().contains(markerC);}),"Latest date request lost to earlier asynchronous navigation");
        const qint64 rapidMs=navigation.elapsed();
        const auto savedA=acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[0],"id")).toObject();
        acceptanceRequire(str(savedA,"content").contains("saved-before-navigation"),"Dirty A text was not saved to its originating journal");
        acceptanceRequire(str(acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[1],"id")).toObject(),"content")==markerB,"Navigation wrote A text into B");
        window.setInfoVisible(true);acceptanceRequire(acceptanceUntil([&]{return acceptanceVisible<QCalendarWidget>(window,"miniCalendar");}),"Calendar inspector did not open");
        auto *calendar=acceptanceVisible<QCalendarWidget>(window,"miniCalendar");
        acceptanceRequire(calendar&&calendar->selectedDate()==dateC,"Calendar selection and loaded document disagree");
        snapshot("01-calendar-selected-day");
        calendar->setSelectedDate(dateA);auto *calendarTable=calendar->findChild<QAbstractItemView*>();acceptanceRequire(calendarTable,"Calendar keyboard view missing");
        calendarTable->setFocus();QTest::keyClick(calendarTable,Qt::Key_Return);
        acceptanceRequire(acceptanceUntil([&]{auto *e=acceptanceVisible<QTextEdit>(window,"editor");return e&&e->toPlainText().contains(markerA);}),"Calendar Enter did not open selected day");
        // Exercise the inline UI and the real root callback. The unsaved journal
        // must remain the creation origin, rather than become the created book.
        QPointer<RichEditor> inlineOrigin=acceptanceVisible<RichEditor>(window,"editor");
        const QString inlineTitle="Libro creado en frase "+nonce;
        const QString inlineMarker=" unsaved-inline-origin-"+nonce+" ";
        inlineOrigin->setFocus();inlineOrigin->moveCursor(QTextCursor::End);
        QTest::keyClicks(inlineOrigin,inlineMarker);QTest::keyClicks(inlineOrigin,"@");
        auto *referenceResults=acceptanceVisible<QListWidget>(window,"referenceResults");
        acceptanceRequire(referenceResults,"At trigger did not open the native object selector");
        int bookTypeRow=-1;
        for(int row=0;row<referenceResults->count();++row)
            if(referenceResults->item(row)->data(Qt::UserRole+3).toString()=="book")bookTypeRow=row;
        acceptanceRequire(bookTypeRow>=0,"Inline selector did not offer the book type");
        for(int row=referenceResults->currentRow();row<bookTypeRow;++row)QTest::keyClick(inlineOrigin,Qt::Key_Down);
        QTest::keyClick(inlineOrigin,Qt::Key_Right);QTest::keyClicks(inlineOrigin,inlineTitle);
        QTest::keyClick(inlineOrigin,Qt::Key_Return);
        auto *creationModal=acceptanceVisible<GuardedDialog>(window,"creationModal");
        acceptanceRequire(creationModal,"Inline creation did not open the creation modal");
        QPushButton *createButton=nullptr;
        for(auto *button:creationModal->findChildren<QPushButton*>())
            if(button->text().startsWith("Crear "))createButton=button;
        acceptanceRequire(createButton,"Creation modal has no create button");
        QTest::mouseClick(createButton,Qt::LeftButton);
        acceptanceRequire(acceptanceUntil([&]{return inlineOrigin&&inlineOrigin->documentReferenceIds().size()==1;}),"Inline book creation did not insert a persistent object reference");
        const QString inlineId=inlineOrigin->documentReferenceIds().first().toString();fixtureObjects<<inlineId;
        acceptanceRequire(acceptanceVisible<RichEditor>(window,"editor")==inlineOrigin&&inlineOrigin->toPlainText().contains(inlineMarker),"Creating from a sentence replaced the journal or lost unsaved text");
        const auto inlineBook=acceptanceRequest(api,"GET","/v1/objects/"+inlineId).toObject();
        acceptanceRequire(str(inlineBook,"type_id")=="book"&&str(inlineBook,"title")==inlineTitle,"Inline creation ignored the chosen type or title");
        auto inlineMention=inlineOrigin->document()->find(inlineTitle);
        acceptanceRequire(!inlineMention.isNull(),"Created inline mention text missing");
        inlineOrigin->setTextCursor(inlineMention);QTest::keyClick(inlineOrigin,Qt::Key_Backspace);
        acceptanceRequire(!inlineOrigin->documentReferenceIds().contains(inlineId),"Deleting the final mention kept its inline relation");
        QTest::keyClick(inlineOrigin,Qt::Key_Z,Qt::ControlModifier);
        acceptanceRequire(inlineOrigin->documentReferenceIds().contains(inlineId),"Undo did not restore the created object's reference");
        bool inlineSaved=false;window.saveCurrent([&]{inlineSaved=true;});
        acceptanceRequire(acceptanceUntil([&]{return inlineSaved;}),"Journal with inline-created book did not save");
        const auto inlineJournal=acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[0],"id")).toObject();
        const auto inlineLinks=inlineJournal.value("links").toArray();
        int inlineLinkCount=0;for(auto id:inlineLinks)if(id.toString()==inlineId)++inlineLinkCount;
        acceptanceRequire(str(inlineJournal,"content").contains(inlineMarker)&&inlineLinkCount==1,"Inline creation did not persist the original journal body and unique object ID");
        snapshot("08-inline-created-in-journal");
        auto *navigationScroll=acceptanceVisible<QScrollArea>(window,"navigationScroll");
        acceptanceRequire(navigationScroll&&navigationScroll->widget(),"Sidebar scroll area missing");
        bool longTypePresent=false;
        for(auto *row:navigationScroll->findChildren<QPushButton*>())
            if(row->toolTip()==str(emptyType,"name"))longTypePresent=true;
        acceptanceRequire(longTypePresent,"Long named fixture type was absent from the sidebar");
        acceptanceRequire(acceptanceUntil([&]{return navigationScroll->horizontalScrollBar()->maximum()==0&&navigationScroll->horizontalScrollBar()->value()==0&&navigationScroll->widget()->width()<=navigationScroll->viewport()->width();}),"Long type name caused horizontal sidebar overflow or shifted navigation labels");

        // A click must reach the preview through the anchor itself. Control-click
        // then exercises the separate tab path, rather than calling it directly.
        const auto inlineTextBeforePreview=inlineOrigin->toPlainText();
        QTest::mouseClick(inlineOrigin->viewport(),Qt::LeftButton,Qt::NoModifier,acceptanceReferencePoint(inlineOrigin,inlineId));
        const int inlineCursorBeforePreview=inlineOrigin->textCursor().position();
        QPointer<GuardedDialog> clickedPreview;
        acceptanceRequire(acceptanceUntil([&]{clickedPreview=acceptanceVisible<GuardedDialog>(window,"objectPreview");return bool(clickedPreview);}),"Clicking an inline mention did not open its preview");
        auto *clickedHeading=clickedPreview->findChild<DocumentTitle*>("documentTitle");
        acceptanceRequire(clickedHeading&&clickedHeading->text()==inlineTitle,"Mention preview opened a different object");
        snapshot("09-clicked-inline-preview",clickedPreview);
        QTest::keyClick(clickedPreview,Qt::Key_Escape);
        acceptanceRequire(acceptanceUntil([&]{return !clickedPreview;}),"Escape did not close the clicked object preview");
        acceptanceRequire(inlineOrigin&&acceptanceVisible<RichEditor>(window,"editor")==inlineOrigin&&inlineOrigin->toPlainText()==inlineTextBeforePreview&&inlineOrigin->textCursor().position()==inlineCursorBeforePreview,"Closing mention preview lost the journal, text, or cursor");
        acceptanceRequire(acceptanceUntil([&]{return inlineOrigin->hasFocus();}),"Closing mention preview did not return focus to the sentence");
        auto *inlineTabs=window.findChild<QTabBar*>("documentTabs");
        acceptanceRequire(inlineTabs,"Document tabs missing before Control-click");
        const int previousTabCount=inlineTabs->count();
        QTest::mouseClick(inlineOrigin->viewport(),Qt::LeftButton,Qt::ControlModifier,acceptanceReferencePoint(inlineOrigin,inlineId));
        acceptanceRequire(acceptanceUntil([&]{auto *heading=acceptanceVisible<DocumentTitle>(window,"documentTitle");return inlineTabs->count()==previousTabCount+1&&heading&&heading->text()==inlineTitle;}),"Control-click did not open the mentioned object in another tab");
        acceptanceRequire(!acceptanceVisible<GuardedDialog>(window,"objectPreview"),"Control-click also opened a preview over its new tab");
        window.closeTab(inlineTabs->currentIndex());
        acceptanceRequire(acceptanceUntil([&]{auto *e=acceptanceVisible<RichEditor>(window,"editor");return inlineTabs->count()==previousTabCount&&e&&e->toPlainText().contains(inlineMarker)&&e->documentReferenceIds().contains(inlineId);}),"Closing the mention's tab did not restore its journal origin");

        window.insertReference(book);bool saved=false;window.saveCurrent([&]{saved=true;});
        acceptanceRequire(acceptanceUntil([&]{return saved;}),"Mentioned journal did not save locally");
        auto updated=acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[0],"id")).toObject();
        acceptanceRequire(updated.value("links").toArray().contains(str(book,"id")),"Mention did not persist its object ID");
        editor=acceptanceVisible<QTextEdit>(window,"editor");auto mention=editor->document()->find(str(book,"title"));acceptanceRequire(!mention.isNull(),"Inserted mention text missing");mention.removeSelectedText();
        saved=false;window.saveCurrent([&]{saved=true;});acceptanceRequire(acceptanceUntil([&]{return saved;}),"Unlink save failed");
        updated=acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[0],"id")).toObject();
        acceptanceRequire(!updated.value("links").toArray().contains(str(book,"id")),"Removing final mention retained an explicit relation");

        // This type really has no objects. A filtered collection with no matches
        // has a separate clear-filters state and must not stand in for it.
        const QString scopedTitle="Objeto desde coleccion "+nonce;
        window.showCollection(emptyTypeId);
        acceptanceRequire(acceptanceUntil([&]{auto *v=acceptanceVisible<LumenViews>(window,"workspaceViews");return v&&v->visibleIds().isEmpty()&&acceptanceVisible<QToolButton>(window,"emptyCreate");}),"Empty typed collection did not expose a creation action");
        snapshot("10-empty-typed-collection");
        bool scopedDialogOk=false;
        QTimer scopedDialogDriver;scopedDialogDriver.setInterval(10);QElapsedTimer scopedDialogWait;scopedDialogWait.start();
        QObject::connect(&scopedDialogDriver,&QTimer::timeout,&window,[&]{
            auto *dialog=acceptanceVisible<QDialog>(window,"newObjectDialog");
            if(!dialog){
                if(scopedDialogWait.elapsed()>5000){scopedDialogDriver.stop();if(auto *modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();}
                return;
            }
            scopedDialogDriver.stop();
            auto *name=dialog->findChild<QLineEdit*>("newObjectTitle");
            auto *type=dialog->findChild<QComboBox*>("newObjectType");
            auto *create=dialog->findChild<QPushButton*>("newObjectCreate");
            scopedDialogOk=name&&type&&create&&type->currentData().toString()==emptyTypeId;
            if(!scopedDialogOk){dialog->reject();return;}
            name->setFocus();QTest::keyClicks(name,scopedTitle);QTest::mouseClick(create,Qt::LeftButton);
        });
        scopedDialogDriver.start();
        QTest::mouseClick(acceptanceVisible<QToolButton>(window,"emptyCreate"),Qt::LeftButton);
        scopedDialogDriver.stop();
        acceptanceRequire(scopedDialogOk,"Empty collection creation did not preselect its scoped type");
        acceptanceRequire(acceptanceUntil([&]{auto *heading=acceptanceVisible<DocumentTitle>(window,"documentTitle");return heading&&heading->text()==scopedTitle;}),"Creating from an empty type did not open the created object");
        QString scopedId;QJsonObject scopedObject;
        for(auto value:arr(acceptanceRequest(api,"GET","/v1/objects"))){auto object=value.toObject();if(str(object,"title")==scopedTitle){scopedObject=object;scopedId=str(object,"id");fixtureObjects<<scopedId;break;}}
        acceptanceRequire(!scopedId.isEmpty()&&str(scopedObject,"type_id")==emptyTypeId,"Empty collection creation stored the wrong object type");
        acceptanceRequire(!scopedObject.value("properties").toObject().contains("date"),"Creation inside a type inherited an unrelated journal date");
        const auto afterScopedCreate=acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[0],"id")).toObject();
        acceptanceRequire(!afterScopedCreate.value("links").toArray().contains(scopedId)&&str(afterScopedCreate,"content")==str(updated,"content"),"Creation inside a type modified the previously open journal");
        window.showCollection(emptyTypeId);
        acceptanceRequire(acceptanceUntil([&]{auto *v=acceptanceVisible<LumenViews>(window,"workspaceViews");return v&&v->visibleIds()==QStringList{scopedId};}),"Created object did not belong to its original typed collection");

        // Test the import batch used after the native file chooser, with real
        // files and API readback. The file chooser itself is deliberately manual.
        QTemporaryDir importSources;acceptanceRequire(importSources.isValid(),"Could not create temporary import fixtures");
        const QString importedNoteTitle="nota-importada-"+nonce;
        const QString importedText="Primera linea de importacion "+nonce+"\nSegunda linea conservada.";
        const QString importedNotePath=importSources.path()+"/"+importedNoteTitle+".txt";
        {QFile file(importedNotePath);const auto bytes=importedText.toUtf8();acceptanceRequire(file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size(),"Could not write fictional note for scoped import");}
        window.showCollection("project");
        auto noteImport=std::make_shared<MainWindow::CollectionImport>();noteImport->type="project";noteImport->paths={importedNotePath};
        window.importCollectionNext(noteImport);
        acceptanceRequire(acceptanceUntil([&]{return noteImport->imported==1||!noteImport->errors.isEmpty();})&&noteImport->errors.isEmpty(),"Scoped text import did not complete successfully");
        QString importedNoteId;QJsonObject importedNote;
        for(auto value:arr(acceptanceRequest(api,"GET","/v1/objects"))){auto object=value.toObject();if(str(object,"title")==importedNoteTitle){importedNote=object;importedNoteId=str(object,"id");fixtureObjects<<importedNoteId;break;}}
        acceptanceRequire(!importedNoteId.isEmpty()&&str(importedNote,"type_id")=="project"&&str(importedNote,"content")==importedText,"Scoped text import lost its type, title, or multiline body");
        acceptanceRequire(!importedNote.value("properties").toObject().contains("date")&&importedNote.value("links").toArray().isEmpty(),"Scoped text import inherited a journal date or relation");

        const QString importedImageName="imagen-global-"+nonce+".png";
        const QString importedImagePath=importSources.path()+"/"+importedImageName;
        QImage importImage(84,126,QImage::Format_ARGB32);importImage.fill(QColor("#406c58"));
        acceptanceRequire(importImage.save(importedImagePath),"Could not write fictional image for global import");
        window.showCollection("");
        auto imageImport=std::make_shared<MainWindow::CollectionImport>();imageImport->paths={importedImagePath};
        window.importCollectionNext(imageImport);
        acceptanceRequire(acceptanceUntil([&]{return imageImport->imported==1||!imageImport->errors.isEmpty();})&&imageImport->errors.isEmpty(),"Global image import did not complete successfully");
        QString importedImageId;QJsonObject importedImage;
        for(auto value:arr(acceptanceRequest(api,"GET","/v1/objects"))){auto object=value.toObject();if(object.value("properties").toObject().value("file_name")==importedImageName){importedImage=object;importedImageId=str(object,"id");fixtureObjects<<importedImageId;break;}}
        const auto importedImageProperties=importedImage.value("properties").toObject();
        const QString importedAssetId=importedImageProperties.value("asset_id").toString();
        acceptanceRequire(!importedImageId.isEmpty()&&str(importedImage,"type_id")=="image"&&!importedAssetId.isEmpty()&&!importedImageProperties.value("assets").toArray().isEmpty(),"Global image import did not create a reusable media object and asset");
        acceptanceRequire(!importedImageProperties.contains("source_object_id")&&!importedImageProperties.contains("date"),"Global image import was attached to an unrelated object or journal date");
        bool importedBytesReady=false;QString importedBytesError;QByteArray importedBytes;
        api.bytes("/v1/assets/"+importedAssetId+"/content",[&](QByteArray bytes,QString error){importedBytes=bytes;importedBytesError=error;importedBytesReady=true;});
        acceptanceRequire(acceptanceUntil([&]{return importedBytesReady;})&&importedBytesError.isEmpty(),"Global import asset could not be read back with authentication");
        const auto decodedImport=QImage::fromData(importedBytes);
        acceptanceRequire(!decodedImport.isNull()&&decodedImport.size()==importImage.size()&&decodedImport.pixelColor(0,0)==importImage.pixelColor(0,0),"Imported media bytes did not preserve the original image");
        const auto afterImports=acceptanceRequest(api,"GET","/v1/objects/"+str(originalJournals[0],"id")).toObject();
        acceptanceRequire(str(afterImports,"content")==str(updated,"content")&&!afterImports.value("links").toArray().contains(importedNoteId)&&!afterImports.value("links").toArray().contains(importedImageId),"Collection imports modified the previous journal or linked imported objects to it");

        window.showCollection("book");
        acceptanceRequire(acceptanceUntil([&]{return acceptanceVisible<LumenViews>(window,"workspaceViews");}),"Native collection module missing");
        auto *views=acceptanceVisible<LumenViews>(window,"workspaceViews");
        auto *allObjects=acceptanceVisible<QToolButton>(window,"collectionAll");acceptanceRequire(allObjects,"Typed collection did not offer its All tab");
        QTest::mouseClick(allObjects,Qt::LeftButton);
        acceptanceRequire(!views->collectionMode(),"All tab did not activate the full collection");
        const auto expected=views->visibleIds();
        acceptanceRequire(expected.contains(str(book,"id")),"Book absent from collection");
        for(const auto &mode:QStringList{"list","gallery","table","wall","kanban","embed"}){views->setPresentation(mode);QTest::qWait(20);acceptanceRequire(views->visibleIds()==expected,"Presentation changed the common query's IDs");snapshot("02-collection-"+mode);}
        const QString querySearch=bookQueryMarker;
        auto querySpec=views->viewSpec();querySpec["search"]=querySearch;querySpec["presentation"]="wall";querySpec["group_by"]="properties.status";
        views->setScope("book",querySpec);const QString queryName="Consulta de aceptación "+nonce;
        QPointer<LumenViews> previousViews=views;views->onSaveView(queryName,views->viewSpec());
        acceptanceRequire(acceptanceUntil([&]{auto *current=acceptanceVisible<LumenViews>(window,"workspaceViews");return current&&current!=previousViews&&current->viewSpec().value("search")==querySearch&&current->visibleIds()==QStringList{str(book,"id")};}),"Saving query did not reopen its matching object set");
        QString queryId;
        for(auto ov:arr(acceptanceRequest(api,"GET","/v1/objects"))){auto object=ov.toObject();if(str(object,"type_id")=="query"&&str(object,"title")==queryName){queryId=str(object,"id");fixtureObjects<<queryId;acceptanceRequire(object.value("properties").toObject().value("view").toObject().value("presentation")=="wall","Query presentation was not saved in API");break;}}
        acceptanceRequire(!queryId.isEmpty(),"Saved query object absent from backend");
        window.openObject(str(book,"id"));acceptanceRequire(acceptanceUntil([&]{return acceptanceVisible<LumenProperties>(window,"objectProperties");}),"Object properties module absent");
        auto *author=acceptanceVisible<LumenProperties>(window,"objectProperties")->findChild<QLineEdit*>("property_author");acceptanceRequire(author,"Legacy book author input absent");
        author->setFocus();author->selectAll();QTest::keyClicks(author,"Autora verificada");saved=false;window.saveCurrent([&]{saved=true;});acceptanceRequire(acceptanceUntil([&]{return saved;}),"Inline property edit did not save");
        auto storedBook=acceptanceRequest(api,"GET","/v1/objects/"+str(book,"id")).toObject();acceptanceRequire(storedBook.value("properties").toObject().value("author")=="Autora verificada","Inline property did not persist through root API");snapshot("05-book-properties");
        // Editing a preview of the page already open behind it must refresh that page before
        // its next save. Otherwise an apparently harmless edit can overwrite the preview's text.
        const QString previewTitle="Titulo desde preview "+nonce;
        const QString previewMarker=" preview-preserved-"+nonce;
        const QString mainMarker=" main-after-preview-"+nonce;
        window.openPreview(str(book,"id"));
        QPointer<GuardedDialog> sameObjectPreview;
        acceptanceRequire(acceptanceUntil([&]{
            sameObjectPreview=window.findChild<GuardedDialog*>("objectPreview");
            return sameObjectPreview&&sameObjectPreview->isVisible();
        }),"Same-object editable preview did not open");
        auto *previewHeading=sameObjectPreview->findChild<DocumentTitle*>("documentTitle");
        auto *previewEditor=sameObjectPreview->findChild<RichEditor*>("previewEditor");
        acceptanceRequire(previewHeading&&previewEditor,"Editable preview fields missing");
        previewHeading->setPlainText(previewTitle);
        previewEditor->setFocus();previewEditor->moveCursor(QTextCursor::End);
        QTest::keyClicks(previewEditor,previewMarker);
        sameObjectPreview->close();
        acceptanceRequire(sameObjectPreview&&sameObjectPreview->isVisible(),"Dirty preview closed before its durable save completed");
        acceptanceRequire(acceptanceUntil([&]{return !sameObjectPreview;}),"Dirty preview did not close after its save");
        acceptanceRequire(acceptanceUntil([&]{
            auto *mainEditor=acceptanceVisible<RichEditor>(window,"editor");
            auto *mainHeading=acceptanceVisible<DocumentTitle>(window,"documentTitle");
            return mainEditor&&mainHeading&&mainEditor->toPlainText().contains(previewMarker)&&mainHeading->text()==previewTitle;
        }),"Closing same-object preview left stale title/body in the main editor");
        editor=acceptanceVisible<RichEditor>(window,"editor");editor->setFocus();editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor,mainMarker);saved=false;window.saveCurrent([&]{saved=true;});
        acceptanceRequire(acceptanceUntil([&]{return saved;}),"Main edit after preview did not save");
        auto afterPreview=acceptanceRequest(api,"GET","/v1/objects/"+str(book,"id")).toObject();
        acceptanceRequire(str(afterPreview,"title")==previewTitle&&str(afterPreview,"content").contains(previewMarker)&&str(afterPreview,"content").contains(mainMarker),"Main save overwrote a title/body edited in its same-object preview");

        // Closing a background tab must only remove its navigation state. Rebuilding the active
        // page here used to discard unsaved text from the other tab.
        auto *tabs=window.findChild<QTabBar*>();
        acceptanceRequire(tabs&&tabs->count()==1,"Expected one tab before the background-close regression");
        window.newTab(str(book,"id"));
        acceptanceRequire(acceptanceUntil([&]{
            auto *active=acceptanceVisible<RichEditor>(window,"editor");
            return tabs->count()==2&&tabs->currentIndex()==1&&active&&active->toPlainText().contains(mainMarker);
        }),"Second active book tab did not restore the current page");
        QPointer<RichEditor> activeBeforeBackgroundClose=acceptanceVisible<RichEditor>(window,"editor");
        const QString tabMarker=" active-tab-unsaved-"+nonce;
        activeBeforeBackgroundClose->setFocus();activeBeforeBackgroundClose->moveCursor(QTextCursor::End);
        QTest::keyClicks(activeBeforeBackgroundClose,tabMarker);
        window.closeTab(0);
        acceptanceRequire(tabs->count()==1&&tabs->currentIndex()==0,"Closing the background tab did not preserve the surviving active tab");
        acceptanceRequire(activeBeforeBackgroundClose&&acceptanceVisible<RichEditor>(window,"editor")==activeBeforeBackgroundClose&&activeBeforeBackgroundClose->toPlainText().contains(tabMarker),"Closing a background tab rebuilt the active editor or discarded its unsaved text");
        saved=false;window.saveCurrent([&]{saved=true;});
        acceptanceRequire(acceptanceUntil([&]{return saved;}),"Surviving tab did not save after background close");
        auto afterTabClose=acceptanceRequest(api,"GET","/v1/objects/"+str(book,"id")).toObject();
        if(!str(afterTabClose,"content").contains(previewMarker)||!str(afterTabClose,"content").contains(mainMarker)||!str(afterTabClose,"content").contains(tabMarker))qWarning().noquote()<<"Acceptance fictional tab readback:"<<str(afterTabClose,"content")<<"\nVisible editor:"<<(activeBeforeBackgroundClose?activeBeforeBackgroundClose->toPlainText():QString("deleted"));
        acceptanceRequire(str(afterTabClose,"content").contains(previewMarker)&&str(afterTabClose,"content").contains(mainMarker)&&str(afterTabClose,"content").contains(tabMarker),"Saving after a background tab close lost active or previously saved text");
        snapshot("07-preview-and-background-tab-preserved");
        window.openObject(queryId);acceptanceRequire(acceptanceUntil([&]{auto *v=acceptanceVisible<LumenViews>(window,"workspaceViews");return v&&v->viewSpec().value("presentation")=="wall"&&v->visibleIds()==QStringList{str(book,"id")};}),"Reopening saved query lost its specification");
        views=acceptanceVisible<LumenViews>(window,"workspaceViews");acceptanceRequire(views->applyKanbanMove(str(book,"id"),"Leído"),"Kanban property update rejected");
        acceptanceRequire(acceptanceUntil([&]{return acceptanceRequest(api,"GET","/v1/objects/"+str(book,"id")).toObject().value("properties").toObject().value("status")=="Leído";}),"Kanban change did not reach backend");
        views->undoLastMove();acceptanceRequire(acceptanceUntil([&]{return acceptanceRequest(api,"GET","/v1/objects/"+str(book,"id")).toObject().value("properties").toObject().value("status")=="Leyendo";}),"Undo did not restore the original backend property");
        QTemporaryDir mediaSources;acceptanceRequire(mediaSources.isValid(),"Could not create fictional media sources");
        QImage image(240,360,QImage::Format_ARGB32);image.fill(QColor("#e0d6b8"));{QPainter painter(&image);painter.setPen(QColor("#342f42"));QFont font("Inter",18);font.setBold(true);painter.setFont(font);painter.drawText(image.rect().adjusted(24,24,-24,-24),Qt::AlignCenter|Qt::TextWordWrap,"CUADERNO\nDEL BOSQUE");}
        acceptanceRequire(image.save(mediaSources.path()+"/portada.png"),"Could not write fictional image");
        QByteArray pdfBytes;QBuffer pdfBuffer(&pdfBytes);pdfBuffer.open(QIODevice::WriteOnly);{
            QPdfWriter pdf(&pdfBuffer);pdf.setPageSize(QPageSize(QPageSize::A5));QPainter painter(&pdf);for(int page=1;page<=3;++page){if(page>1)pdf.newPage();painter.drawText(150,250,QString("Documento ficticio: página %1 de 3").arg(page));}painter.end();
        }
        {QFile file(mediaSources.path()+"/lectura.pdf");acceptanceRequire(file.open(QIODevice::WriteOnly)&&file.write(pdfBytes)==pdfBytes.size(),"Could not write fictional PDF");}
        for(const QString &filename:QStringList{"portada.png","lectura.pdf"}){
            bool uploaded=false;QString uploadError;QJsonObject asset;api.upload(str(book,"id"),mediaSources.path()+"/"+filename,[&](QJsonValue result,QString error){asset=result.toObject();uploadError=error;uploaded=true;});
            acceptanceRequire(acceptanceUntil([&]{return uploaded;})&&uploadError.isEmpty()&&!str(asset,"id").isEmpty(),"Native multipart upload failed");
            if(!str(asset,"media_object_id").isEmpty())fixtureObjects<<str(asset,"media_object_id");
            window.previewAsset(str(asset,"id"),asset);MediaViewer *viewer=nullptr;
            acceptanceRequire(acceptanceUntil([&]{viewer=window.findChild<MediaViewer*>("mediaViewer");return viewer&&viewer->isVisible();}),"Authenticated native media preview did not open");
            acceptanceRequire(viewer->errorText().isEmpty(),"Native media viewer has an error");
            acceptanceRequire(viewer->findChild<QPushButton*>("mediaClose")||viewer->findChild<QToolButton*>("mediaClose"),"Native media viewer close action missing");
            if(filename.endsWith("pdf")){acceptanceRequire(acceptanceUntil([&]{return viewer->pageCount()==3;}),"PDF bytes were handled as an image or lost pages");viewer->setPage(2);acceptanceRequire(viewer->currentPage()==2,"PDF page navigation failed");QTest::qWait(140);acceptanceRequire(viewer->currentPage()==2,"Deferred initial PDF restore overwrote manual page navigation");snapshot("06-pdf-viewer",viewer);}
            else{auto *cover=viewer->findChild<QLabel*>("mediaImage");acceptanceRequire(cover,"Image preview absent");snapshot("06-image-viewer",viewer);}
            QTest::keyClick(viewer,Qt::Key_Escape);acceptanceRequire(acceptanceUntil([&]{return !window.findChild<MediaViewer*>("mediaViewer");}),"Escape did not close private media preview");
        }
        window.showTimeline(7);
        acceptanceRequire(acceptanceUntil([&]{return acceptanceVisible<LumenCalendarOverview>(window,"calendarOverview");}),"Calendar overview missing");
        acceptanceRequire(window.findChildren<QFrame*>("calendarDayPanel").size()==7,"Week is not seven content panels");
        snapshot("03-calendar-week");window.showTimeline(3);QTest::qWait(50);acceptanceRequire(window.findChildren<QFrame*>("calendarDayPanel").size()==3,"Three-day view missing");snapshot("04-calendar-three");
        window.navigateJournal(dateA);acceptanceRequire(acceptanceUntil([&]{auto *e=acceptanceVisible<QTextEdit>(window,"editor");return e&&e->toPlainText().contains(markerA);}),"Returning from calendar lost original document");
        fprintf(stdout,"LUMEN_ACCEPTANCE_OK: calendar dates, dirty navigation race, durable origin save, typed inline create/undo/API, mention click-preview/Escape/Control-click-tab, scoped empty create without journal date, scoped text/global reusable image imports and authenticated bytes, inline unlink, six views, saved query/reopen, inline properties, same-object preview close/save and main refresh, dirty active editor after background tab close, kanban/undo API, native authenticated image/PDF upload/viewers, calendar content; measured initial=%lldms rapid=%lldms\n",static_cast<long long>(initialMs),static_cast<long long>(rapidMs));
        return 0;
    }catch(const std::exception &error){fprintf(stderr,"LUMEN_ACCEPTANCE_FAILED: %s\n",error.what());return 1;}
}
