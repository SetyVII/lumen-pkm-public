#include "../src/objects/lumen_properties.hpp"
#include <QDir>
#include <QtWidgets>
#include <QtTest>
#include <cstdio>

static bool check(bool value,const char *message){if(!value)fprintf(stderr,"FAIL: %s\n",message);return value;}
int main(int argc,char **argv){
    QApplication app(argc,argv);bool pass=true;
    QJsonObject type{{"id","book"},{"name","Libro"},{"properties",QJsonArray{
        "author",QJsonObject{{"id","pages"},{"name","Páginas"},{"type","number"},{"min",0}},
        QJsonObject{{"id","published"},{"name","Publicación"},{"type","date"}},
        QJsonObject{{"id","finished"},{"name","Terminado"},{"type","boolean"}},
        QJsonObject{{"id","status"},{"name","Estado"},{"type","select"},{"options",QJsonArray{"Pendiente","Leyendo","Leído"}}},
        QJsonObject{{"id","collaborator"},{"name","Colaborador"},{"type","object"},{"multiple",false},{"target_type","person"}},
        QJsonObject{{"id","related"},{"name","Relacionados"},{"type","object"},{"multiple",true},{"target_type","book"}}}}};
    QJsonArray types{type,QJsonObject{{"id","person"},{"name","Persona"}}};
    QJsonObject object{{"id","book-a"},{"type_id","book"},{"title","Bosque"},{"properties",QJsonObject{{"author","Autor"},{"pages",100},{"published","2026-10-01"},{"finished",false},{"status","Leyendo"},{"assets",QJsonArray{QJsonObject{{"id","private-id"}}}},{"blocks",QJsonArray{}},{"rich_text","<p>private</p>"},{"view_spec",QJsonObject{}},{"og_title","Hidden"}}}};
    QJsonArray objects{object,QJsonObject{{"id","person-a"},{"type_id","person"},{"title","Ada"}},QJsonObject{{"id","book-b"},{"type_id","book"},{"title","El río"}}};
    LumenProperties properties;properties.setObjectName("objectProperties");properties.resize(680,500);properties.setTheme(true);int changes=0,adds=0,edits=0;QString lastKey;QJsonValue lastValue;
    properties.onChange=[&](QString key,QJsonValue value){++changes;lastKey=key;lastValue=value;};properties.onAddProperty=[&]{++adds;};properties.onEditType=[&]{++edits;};
    properties.setData(object,type,objects,types);properties.show();QTest::qWait(30);
    pass&=check(changes==0,"loading properties emits no changes");
    pass&=check(!properties.findChild<QWidget*>("property_assets")&&!properties.findChild<QWidget*>("property_blocks")&&!properties.findChild<QWidget*>("property_rich_text")&&!properties.findChild<QWidget*>("property_view_spec")&&!properties.findChild<QWidget*>("property_og_title"),"internal metadata is not exposed as editable properties");
    QJsonObject webType{{"id","web"},{"name","Enlace web"},{"properties",QJsonArray{"url","summary"}}};
    QJsonObject webObject{{"id","web-a"},{"type_id","web"},{"title","Artículo"},{"properties",QJsonObject{{"url","https://example.com"},{"summary","Resumen nuevo"},{"description","Resumen viejo"},{"og_description","OG viejo"}}}};
    LumenProperties webProperties;webProperties.resize(680,300);webProperties.setTheme(true);QString webLastKey;QJsonValue webLastValue;
    webProperties.onChange=[&](QString key,QJsonValue value){webLastKey=key;webLastValue=value;};
    webProperties.setData(webObject,webType,objects,types);webProperties.show();QTest::qWait(30);
    auto *webSummary=webProperties.findChild<QLineEdit*>("property_summary");
    pass&=check(webSummary&&webSummary->text()=="Resumen nuevo","web summary is shown once");
    pass&=check(!webProperties.findChild<QWidget*>("property_description"),"legacy description is not a second row");
    pass&=check(!webProperties.findChild<QWidget*>("property_og_description"),"og_description stays internal");
    QJsonObject legacyWeb=webObject;legacyWeb["properties"]=QJsonObject{{"url","https://example.com"},{"description","Solo heredado"}};
    webProperties.setData(legacyWeb,webType,objects,types);QTest::qWait(30);
    webSummary=webProperties.findChild<QLineEdit*>("property_summary");
    pass&=check(webSummary&&webSummary->text()=="Solo heredado","legacy description is shown under Descripción");
    if(webSummary){webSummary->setFocus();webSummary->selectAll();QTest::keyClicks(webSummary,"Editado");}
    pass&=check(webLastKey=="summary"&&webLastValue=="Editado","editing Descripción writes summary");
    const auto captureDir=qEnvironmentVariable("LUMEN_PROPERTIES_CAPTURE_DIR");
    if(!captureDir.isEmpty()){
        QDir().mkpath(captureDir);
        webProperties.setData(webObject,webType,objects,types);webProperties.resize(680,300);webProperties.show();QTest::qWait(50);
        pass&=check(webProperties.grab().save(captureDir+"/web-description.png"),"web description screenshot must save");
    }
    auto *author=properties.findChild<QLineEdit*>("property_author");pass&=check(author&&author->placeholderText()=="Autor","legacy author property is native and labelled");
    if(author){author->ensurePolished();pass&=check(author->font().pixelSize()==13,"inline property typography survives external objectName");}
    if(author){author->setFocus();author->selectAll();QTest::keyClicks(author,"Ada Lovelace");pass&=check(lastKey=="author"&&lastValue=="Ada Lovelace","real text edits emit a property key and value");}
    auto *pages=properties.findChild<QLineEdit*>("property_pages");pass&=check(pages&&pages->validator(),"numeric editor validates native input");
    if(pages){pages->setFocus();pages->selectAll();QTest::keyClicks(pages,"250");pass&=check(lastKey=="pages"&&lastValue.isDouble()&&lastValue.toDouble()==250,"numeric editor emits JSON numbers");}
    auto *finished=properties.findChild<QComboBox*>("property_finished");finished->setCurrentIndex(1);pass&=check(lastKey=="finished"&&lastValue.isBool()&&lastValue.toBool(),"boolean editor emits booleans");
    auto *date=properties.findChild<QDateEdit*>("property_published");date->setDate(QDate(2026,10,29));pass&=check(lastKey=="published"&&lastValue=="2026-10-29","date editor emits ISO dates");
    auto *status=properties.findChild<QComboBox*>("property_status");status->setCurrentIndex(status->findData("Leído"));pass&=check(lastKey=="status"&&lastValue=="Leído","select editor emits only declared choices");
    bool picker=false;
    QTimer::singleShot(30,&properties,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;auto *list=dialog->findChild<QListWidget*>("propertyRelationChoices");auto *search=dialog->findChild<QLineEdit*>("propertyRelationSearch");picker=list&&search&&list->count()==1&&list->item(0)->text().contains("Ada")&&!list->item(0)->text().contains("person-a");if(search){QTest::keyClicks(search,"Ada");QTest::keyClick(search,Qt::Key_Down);QTest::keyClick(search,Qt::Key_Return);}});
    properties.findChild<QPushButton*>("property_collaborator")->click();pass&=check(picker&&lastKey=="collaborator"&&lastValue=="person-a","searchable relation chooser hides IDs and validates target type");
    QTimer::singleShot(30,&properties,[&]{auto *dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog)return;auto *list=dialog->findChild<QListWidget*>("propertyRelationChoices");if(list&&list->count()==1)list->item(0)->setCheckState(Qt::Checked);dialog->accept();});
    properties.findChild<QPushButton*>("property_related")->click();pass&=check(lastKey=="related"&&lastValue.toArray()==QJsonArray{"book-b"},"multiple relation chooser emits an array of stable IDs");
    properties.findChild<QToolButton*>("propertyAdd")->click();properties.findChild<QToolButton*>("propertyEditType")->click();pass&=check(adds==1&&edits==1,"schema actions are explicit callbacks");
    const int previous=changes;properties.setData(object,type,objects,types);pass&=check(changes==previous,"refreshing properties emits no accidental writes");
    if(pass)fprintf(stdout,"LUMEN_PROPERTIES_OK: no load writes, hidden internals, typed text/number/date/boolean/select, searchable single/multiple relations, schema actions\n");
    return pass?0:1;
}
