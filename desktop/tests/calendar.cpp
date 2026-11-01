#include "../src/views/lumen_calendar.hpp"
#include <QtWidgets>
#include <QtTest>
#include <cstdio>

static bool check(bool value,const char *message){if(!value)fprintf(stderr,"FAIL: %s\n",message);return value;}
int main(int argc,char **argv){
    QApplication app(argc,argv);bool pass=true;
    QJsonArray objects{
        QJsonObject{{"id","day-a"},{"type_id","journal"},{"title","2026-11-16"},{"content","<p>Un día entre libros.</p><p>Una segunda idea.</p>"},{"properties",QJsonObject{{"date","2026-11-16"}}},{"links",QJsonArray{"book-a"}}},
        QJsonObject{{"id","day-b"},{"type_id","journal"},{"title","2026-11-19"},{"content","Otra nota."},{"properties",QJsonObject{{"date","2026-11-19"}}}},
        QJsonObject{{"id","book-a"},{"type_id","book"},{"title","El jardín"},{"created_at","2026-11-16T12:00:00Z"},{"properties",QJsonObject{}}}};
    QJsonArray types{QJsonObject{{"id","book"},{"name","Libro"}}};
    LumenMiniCalendar mini;mini.resize(300,280);mini.setSelectedDate(QDate(2026,11,15));mini.setWorkspaceObjects(objects);mini.setTheme(true);
    int requests=0;QDate requested;mini.onDateRequested=[&](QDate date){++requests;requested=date;};mini.show();QTest::qWait(40);
    auto *table=mini.findChild<QAbstractItemView*>();pass&=check(table,"native calendar table exists");
    if(table){
        table->setFocus();QTest::keyClick(table,Qt::Key_Right);QTest::qWait(15);
        pass&=check(mini.selectedDate()==QDate(2026,11,16)&&requests==0,"arrows move selection without opening another diary");
        QTest::keyClick(table,Qt::Key_Return);pass&=check(requests==1&&requested==QDate(2026,11,16),"Enter opens exactly the selected day once");
        QModelIndex target;for(int row=1;row<table->model()->rowCount();++row)for(int col=0;col<table->model()->columnCount();++col){auto index=table->model()->index(row,col);if(index.data().toString()=="18")target=index;}
        pass&=check(target.isValid(),"click target found");
        if(target.isValid()){QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualRect(target).center());QTest::qWait(15);pass&=check(requests==2&&requested==QDate(2026,11,18),"mouse click opens clicked immutable date once");}
    }
    LumenCalendarOverview overview;overview.setObjectName("calendarOverview");overview.resize(1050,850);overview.setTheme(true,true);overview.setData(objects,types);overview.setDate(QDate(2026,11,18));overview.setMode("week");overview.show();QTest::qWait(40);
    int openRequests=0;QDate openDate;QString opened;
    overview.onOpenDate=[&](QDate date){++openRequests;openDate=date;};overview.onOpenObject=[&](QString id){opened=id;};
    auto panels=overview.findChildren<QFrame*>("calendarDayPanel");
    pass&=check(panels.size()==7,"week displays seven real day panels");
    auto texts=overview.findChildren<QLabel*>("calendarExcerpt");pass&=check(texts.size()==2&&texts[0]->text().contains("libros"),"week displays existing journal bodies");
    pass&=check(texts.size()==2&&texts[0]->text().contains('\n'),"journal preview retains paragraph breaks");
    auto *dayHeading=overview.findChild<QPushButton*>("calendarDayHeading");dayHeading->ensurePolished();
    pass&=check(dayHeading->font().pixelSize()==17&&texts[0]->font().pixelSize()==15,"calendar heading and content typography survives external objectName");
    pass&=check(overview.date()==QDate(2026,11,18),"overview exposes its navigated date without mutating the root journal");
    auto emptyButtons=overview.findChildren<QPushButton*>("calendarEmptyDay");pass&=check(!emptyButtons.isEmpty(),"empty days have a create action");
    if(!emptyButtons.isEmpty()){auto *button=emptyButtons.first();const QDate expected=QDate::fromString(button->property("date").toString(),Qt::ISODate);button->click();pass&=check(openRequests==1&&openDate==expected,"empty day opens only that date");}
    auto objectButtons=overview.findChildren<QPushButton*>("calendarObjectLink");pass&=check(objectButtons.size()==2,"related and created sections remain distinct");
    if(!objectButtons.isEmpty()){objectButtons.first()->click();pass&=check(opened=="book-a","related object opens by stable ID");}
    overview.setMode("three");QTest::qWait(30);panels=overview.findChildren<QFrame*>("calendarDayPanel");pass&=check(panels.size()==3,"three day view uses three panels");
    pass&=check(panels.size()==3&&panels.at(0)->property("date")=="2026-11-17"&&panels.at(1)->property("date")=="2026-11-18"&&panels.at(2)->property("date")=="2026-11-19","three day view centers the selected date, matching the reference");
    overview.setMode("month");QTest::qWait(30);panels=overview.findChildren<QFrame*>("calendarDayPanel");pass&=check(panels.size()==2,"month displays existing notes without creating thirty blank journals");
    pass&=check(overview.findChild<LumenMiniCalendar*>()!=nullptr,"month has navigable native mini calendar");
    if(pass)fprintf(stdout,"LUMEN_CALENDAR_OK: arrow/Enter/click dates, seven day content, three columns, month notes, explicit empty action, stable object IDs\n");
    return pass?0:1;
}
