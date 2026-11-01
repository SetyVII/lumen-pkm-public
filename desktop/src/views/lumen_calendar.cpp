#include "lumen_calendar.hpp"
#include "lumen_views.hpp"
#include <QtWidgets>
#include <QTextDocumentFragment>

namespace {
QDate journalDate(const QJsonObject &object) {
    if(object.value("type_id").toString()!="journal") return {};
    QDate date=QDate::fromString(object.value("properties").toObject().value("date").toString(),Qt::ISODate);
    if(!date.isValid()) date=QDate::fromString(object.value("title").toString(),Qt::ISODate);
    return date;
}
QDate createdDate(const QJsonObject &object) {
    auto date=QDateTime::fromString(object.value("created_at").toString(),Qt::ISODateWithMs);
    if(!date.isValid())date=QDateTime::fromString(object.value("created_at").toString(),Qt::ISODate);
    return date.isValid()?date.toLocalTime().date():QDate{};
}
QString journalPreview(const QJsonObject &object){
    QString text=object.value("content").toString();
    if(Qt::mightBeRichText(text))text=QTextDocumentFragment::fromHtml(text).toPlainText();
    return text.trimmed();
}
QString typeName(const QJsonArray &types,const QString &id) {
    for(auto v:types)if(v.toObject().value("id")==id)return v.toObject().value("name").toString();
    return id;
}
bool referencesDate(const QJsonObject &object,const QJsonArray &types,QDate date){
    const auto props=object.value("properties").toObject();const QString value=date.toString(Qt::ISODate);
    if(props.value("date").toString()==value)return true;
    for(auto tv:types){auto type=tv.toObject();if(type.value("id")!=object.value("type_id"))continue;
        for(auto pv:type.value("properties").toArray()){auto property=pv.toObject();if(property.value("type")!="date")continue;
            const QString key=property.value("key").toString(property.value("id").toString(property.value("name").toString()));
            if(props.value(key).toString()==value)return true;
        }
    }return false;
}
QPushButton *dayLink(const QString &text,const QString &name) {
    auto *button=new QPushButton(text);button->setObjectName(name);button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);button->setAccessibleName(text);button->setMinimumHeight(34);return button;
}
}
LumenMiniCalendar::LumenMiniCalendar(QWidget *parent):QCalendarWidget(parent){
    setObjectName("LumenMiniCalendar");setLocale(QLocale(QLocale::Spanish,QLocale::Spain));setFirstDayOfWeek(Qt::Monday);
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);setHorizontalHeaderFormat(QCalendarWidget::SingleLetterDayNames);
    setGridVisible(false);setNavigationBarVisible(true);setFocusPolicy(Qt::StrongFocus);setMinimumWidth(240);
    if(auto *view=findChild<QAbstractItemView*>()){
        view->setMouseTracking(true);view->viewport()->setMouseTracking(true);
        view->installEventFilter(this);view->viewport()->installEventFilter(this);
    }
    connect(this,&QCalendarWidget::clicked,this,[this](QDate date){if(onDateRequested)onDateRequested(date);});
    connect(this,&QCalendarWidget::currentPageChanged,this,[this]{cells.clear();hovered={};});
    setTheme(false);
}
void LumenMiniCalendar::setWorkspaceObjects(QJsonArray data){
    objects=std::move(data);objectCounts.clear();noteDates.clear();
    for(auto v:objects){auto object=v.toObject();const auto date=journalDate(object);
        if(date.isValid()){if(!LumenQuery::plainText(object).isEmpty()||!object.value("links").toArray().isEmpty())noteDates.insert(date);}
        else{const auto created=createdDate(object);if(created.isValid())objectCounts[created]++;}
    }updateCells();
}
void LumenMiniCalendar::setTheme(bool value){
    dark=value;
    setStyleSheet(QString("QCalendarWidget{background:%1;color:%2;font-family:Inter;font-size:12px;}"
        "QCalendarWidget QAbstractItemView{background:%1;color:%2;selection-background-color:transparent;outline:0;border:0;}"
        "QCalendarWidget QWidget#qt_calendar_navigationbar{background:%1;}"
        "QCalendarWidget QToolButton{background:transparent;color:%2;border:0;border-radius:4px;padding:5px;font-size:12px;}"
        "QCalendarWidget QToolButton:hover{background:%3;}QCalendarWidget QToolButton:focus{border:1px solid #d2b7f5;}"
        "QCalendarWidget QSpinBox{background:%1;color:%2;border:1px solid %3;}")
        .arg(dark?"#151518":"#f4f4f5",dark?"#e4e3e2":"#35343a",dark?"#242428":"#f1eff4"));
    QTextCharFormat weekends;weekends.setForeground(QColor(dark?"#bab8c1":"#75737c"));setWeekdayTextFormat(Qt::Saturday,weekends);setWeekdayTextFormat(Qt::Sunday,weekends);updateCells();
}
void LumenMiniCalendar::paintCell(QPainter *p,const QRect &rect,QDate date)const{
    cells[date]=rect;p->save();p->setRenderHint(QPainter::Antialiasing);
    QRectF box=QRectF(rect).adjusted(2,2,-2,-2);const bool selected=date==selectedDate(),today=date==QDate::currentDate(),hover=date==hovered;
    if(selected||hover){p->setPen(selected?QPen(QColor(dark?"#d2b7f5":"#d2b7f5"),1):QPen(Qt::NoPen));p->setBrush(QColor(dark?(selected?"#2d2d31":"#242428"):(selected?"#e6e4e8":"#f1eff4")));p->drawRoundedRect(box,5,5);}
    if(today){p->setPen(QPen(QColor(dark?"#d2b7f5":"#9372b8"),1.2));p->setBrush(Qt::NoBrush);p->drawEllipse(QRectF(rect.center().x()-11,rect.center().y()-11,22,22));}
    const bool inMonth=date.month()==monthShown()&&date.year()==yearShown();
    p->setPen(QColor(dark?(inMonth?"#e4e3e2":"#74727b"):(inMonth?"#35343a":"#afb7ae")));
    QFont font("Inter");font.setPixelSize(12);if(selected||today)font.setWeight(QFont::DemiBold);p->setFont(font);p->drawText(rect,Qt::AlignCenter,QString::number(date.day()));
    if(noteDates.contains(date)||objectCounts.contains(date)){p->setPen(Qt::NoPen);p->setBrush(QColor(dark?"#d2b7f5":"#9372b8"));p->drawEllipse(QPointF(rect.center().x(),rect.bottom()-3),1.7,1.7);}
    if(selected&&hasFocus()){p->setPen(QPen(QColor("#d2b7f5"),1,Qt::DotLine));p->setBrush(Qt::NoBrush);p->drawRoundedRect(box.adjusted(2,2,-2,-2),3,3);}p->restore();
}
void LumenMiniCalendar::requestSelected(){if(onDateRequested)onDateRequested(selectedDate());}
void LumenMiniCalendar::keyPressEvent(QKeyEvent *event){
    if(event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter){requestSelected();event->accept();return;}
    QCalendarWidget::keyPressEvent(event);
}
bool LumenMiniCalendar::eventFilter(QObject *watched,QEvent *event){
    if(event->type()==QEvent::KeyPress){auto *key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter){requestSelected();return true;}}
    if(event->type()==QEvent::MouseMove){auto *mouse=static_cast<QMouseEvent*>(event);QDate next;
        for(auto it=cells.begin();it!=cells.end();++it)if(it.value().contains(mouse->pos())){next=it.key();break;}
        if(next!=hovered){hovered=next;updateCells();}
    }else if(event->type()==QEvent::Leave){hovered={};updateCells();}
    else if(event->type()==QEvent::ToolTip){auto *help=static_cast<QHelpEvent*>(event);for(auto it=cells.begin();it!=cells.end();++it)if(it.value().contains(help->pos())){
        QString text=locale().toString(it.key(),"dddd d 'de' MMMM 'de' yyyy");
        if(noteDates.contains(it.key()))text+="\nTiene una nota diaria";
        if(objectCounts.contains(it.key()))text+=QString("\n%1 objetos creados").arg(objectCounts.value(it.key()));
        text+="\nClic o Enter para abrir";QToolTip::showText(help->globalPos(),text,this);return true;}
    }return QCalendarWidget::eventFilter(watched,event);
}

struct LumenCalendarOverview::Impl{
    QJsonArray objects,types;QDate date=QDate::currentDate();QString mode="week";bool dark=false,reduced=false;
    QVBoxLayout *layout=nullptr;QHash<QDate,QJsonObject> journals;
};
LumenCalendarOverview::LumenCalendarOverview(QWidget *parent):QWidget(parent),d(std::make_unique<Impl>()){
    setObjectName("LumenCalendarOverview");setProperty("lumenComponent","calendarOverview");d->layout=new QVBoxLayout(this);d->layout->setContentsMargins(0,0,0,0);d->layout->setSpacing(12);setTheme(false,false);
}
LumenCalendarOverview::~LumenCalendarOverview()=default;
void LumenCalendarOverview::setData(QJsonArray objects,QJsonArray types){
    if(objects==d->objects&&types==d->types)return;
    int scroll=0;if(d->layout->count()>1)if(auto *area=qobject_cast<QScrollArea*>(d->layout->itemAt(1)->widget()))scroll=area->verticalScrollBar()->value();
    d->objects=std::move(objects);d->types=std::move(types);d->journals.clear();for(auto v:d->objects){const auto object=v.toObject();const auto date=journalDate(object);if(date.isValid())d->journals[date]=object;}rebuild();
    if(d->layout->count()>1)if(auto *area=qobject_cast<QScrollArea*>(d->layout->itemAt(1)->widget()))QTimer::singleShot(0,area,[area,scroll]{area->verticalScrollBar()->setValue(scroll);});
}
void LumenCalendarOverview::setDate(QDate date){if(date.isValid()){d->date=date;rebuild();}}
QDate LumenCalendarOverview::date()const{return d->date;}
void LumenCalendarOverview::setMode(QString mode){if(QStringList{"week","three","month"}.contains(mode)){d->mode=mode;rebuild();}}
void LumenCalendarOverview::setTheme(bool dark,bool reduced){
    d->dark=dark;d->reduced=reduced;
    setStyleSheet(QString("QWidget[lumenComponent=\"calendarOverview\"]{background:%1;color:%2;font-family:Inter;font-size:13px;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QLabel{color:%2;background:transparent;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QFrame#calendarDayPanel{background:%3;border:1px solid %4;border-radius:7px;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QPushButton{background:transparent;color:%2;border:1px solid transparent;border-radius:5px;padding:7px;text-align:left;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QPushButton:hover{background:%5;}QWidget[lumenComponent=\"calendarOverview\"] QPushButton:pressed{background:%4;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QPushButton:focus{border:1px solid #d2b7f5;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QPushButton#calendarDayHeading{font-size:17px;font-weight:600;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QPushButton#calendarObjectLink{border:1px solid %4;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QLabel#calendarExcerpt{font-size:15px;line-height:1.45;padding:4px 7px;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QLabel#calendarSectionLabel{font-size:11px;color:%6;padding:10px 7px 3px;}"
        "QWidget[lumenComponent=\"calendarOverview\"] QScrollArea{border:0;background:transparent;}")
        .arg(dark?"#151518":"#fcfcfc",dark?"#e4e3e2":"#35343a",dark?"#1c1c20":"#fcfcfc",dark?"#29292e":"#e6e4e8",dark?"#242428":"#f1eff4",dark?"#919097":"#7c897a"));rebuild();
}
void LumenCalendarOverview::rebuild(){
    while(auto *item=d->layout->takeAt(0)){if(item->widget()){item->widget()->hide();item->widget()->deleteLater();}delete item;}
    auto *toolbar=new QWidget;auto *bar=new QHBoxLayout(toolbar);bar->setContentsMargins(0,0,0,0);
    const QLocale locale(QLocale::Spanish,QLocale::Spain);
    QDate start=d->mode=="week"?d->date.addDays(1-d->date.dayOfWeek()):d->mode=="month"?QDate(d->date.year(),d->date.month(),1):d->date.addDays(-1);
    QString period=d->mode=="month"?locale.toString(start,"MMMM yyyy"):locale.toString(start,"d MMM")+" – "+locale.toString(start.addDays(d->mode=="three"?2:6),"d MMM yyyy");
    auto *title=new QLabel(period);title->setObjectName("calendarPeriod");title->setStyleSheet("font-size:20px;font-weight:600;");bar->addWidget(title);bar->addStretch();
    auto *prev=dayLink("Anterior","calendarPrev"),*today=dayLink("Hoy","calendarToday"),*next=dayLink("Siguiente","calendarNext");
    prev->setIcon(QIcon(":/icons/chevron-left.svg"));next->setIcon(QIcon(":/icons/chevron-right.svg"));
    bar->addWidget(prev);bar->addWidget(today);bar->addWidget(next);d->layout->addWidget(toolbar);
    connect(prev,&QPushButton::clicked,this,[this]{d->date=d->mode=="month"?d->date.addMonths(-1):d->date.addDays(d->mode=="three"?-3:-7);rebuild();});
    connect(next,&QPushButton::clicked,this,[this]{d->date=d->mode=="month"?d->date.addMonths(1):d->date.addDays(d->mode=="three"?3:7);rebuild();});
    connect(today,&QPushButton::clicked,this,[this]{d->date=QDate::currentDate();rebuild();});
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *canvas=new QWidget;
    auto *list=new QVBoxLayout(canvas);list->setContentsMargins(0,0,0,10);list->setSpacing(13);scroll->setWidget(canvas);d->layout->addWidget(scroll,1);
    auto dayPanel=[this,locale](QDate date,bool compact)->QWidget*{
        auto *panel=new QFrame;panel->setObjectName("calendarDayPanel");panel->setProperty("date",date.toString(Qt::ISODate));
        auto *body=new QVBoxLayout(panel);body->setContentsMargins(compact?12:18,compact?10:15,compact?12:18,14);body->setSpacing(7);
        QString heading=locale.toString(date,compact?"ddd d":"dddd d 'de' MMMM");if(date==QDate::currentDate())heading+=" · Hoy";
        auto *open=dayLink(heading,"calendarDayHeading");open->setProperty("date",date.toString(Qt::ISODate));body->addWidget(open);
        connect(open,&QPushButton::clicked,this,[this,date]{if(onOpenDate)onOpenDate(date);});
        const auto journal=d->journals.value(date);const QString excerpt=journalPreview(journal);
        if(!excerpt.isEmpty()){
            auto *text=new QLabel(excerpt.left(compact?390:600));text->setObjectName("calendarExcerpt");text->setWordWrap(true);text->setTextFormat(Qt::PlainText);QFont font("Inter");font.setPixelSize(15);text->setFont(font);text->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);body->addWidget(text);
            auto *edit=dayLink("Abrir nota diaria","calendarEditDay");edit->setIcon(QIcon(":/icons/file-text.svg"));body->addWidget(edit);connect(edit,&QPushButton::clicked,this,[this,date]{if(onOpenDate)onOpenDate(date);});
        }else{
            auto *empty=dayLink(journal.isEmpty()?"+ Escribir en este día":"+ Continuar esta nota","calendarEmptyDay");empty->setProperty("date",date.toString(Qt::ISODate));body->addWidget(empty);
            connect(empty,&QPushButton::clicked,this,[this,date]{if(onOpenDate)onOpenDate(date);});
        }
        QJsonArray linked,created,dated;const auto ids=LumenQuery::fieldValue(journal,"links",d->objects).toArray();
        for(auto ov:d->objects){auto object=ov.toObject();if(object.value("type_id")=="journal")continue;
            if(ids.contains(object.value("id")))linked.append(object);
            if(createdDate(object)==date)created.append(object);
            if(referencesDate(object,d->types,date))dated.append(object);
        }
        auto section=[this,body,compact](const QString &label,const QJsonArray &objects){
            if(objects.isEmpty()) return;
            auto *heading=new QLabel(label.toUpper()+QString("  %1").arg(objects.size()));heading->setObjectName("calendarSectionLabel");body->addWidget(heading);
            const int max=compact?5:10;int count=0;
            for(auto v:objects){if(count++>=max)break;auto object=v.toObject();
                QString title=object.value("title").toString();if(compact&&title.size()>66)title=title.left(63)+"…";
                auto *button=dayLink(title,"calendarObjectLink");button->setToolTip(typeName(d->types,object.value("type_id").toString())+" · "+object.value("title").toString());
                button->setIcon(QIcon(":/icons/"+QString(object.value("type_id")=="book"?"book":object.value("type_id")=="web"?"link":"file-text")+".svg"));
                const QString id=object.value("id").toString();connect(button,&QPushButton::clicked,this,[this,id]{if(onOpenObject)onOpenObject(id);});body->addWidget(button);
            }
            if(objects.size()>max){auto *more=new QLabel(QString("+ %1 objetos más").arg(objects.size()-max));more->setObjectName("calendarSectionLabel");body->addWidget(more);}
        };
        section("En esta nota",linked);section("Referencias de fecha",dated);section("Creados este día",created);body->addStretch();return panel;
    };
    if(d->mode=="three"){
        auto *columns=new QHBoxLayout;columns->setSpacing(13);for(int i=0;i<3;++i){auto *panel=dayPanel(start.addDays(i),true);panel->setMinimumWidth(220);columns->addWidget(panel,1);}list->addLayout(columns);list->addStretch();
    }else if(d->mode=="week"){
        for(int i=0;i<7;++i) list->addWidget(dayPanel(start.addDays(i),false));
        list->addStretch();
    }else{
        auto *monthTop=new QHBoxLayout;auto *mini=new LumenMiniCalendar;mini->setMaximumWidth(310);mini->setSelectedDate(d->date);mini->setWorkspaceObjects(d->objects);mini->setTheme(d->dark);mini->onDateRequested=[this](QDate date){if(onOpenDate)onOpenDate(date);};
        connect(mini,&QCalendarWidget::currentPageChanged,this,[this](int year,int month){d->date=QDate(year,month,1);rebuild();});
        monthTop->addWidget(mini,0,Qt::AlignTop);
        auto *summary=new QLabel(QString("%1\nNotas del mes y objetos asociados a cada día.\nSelecciona una fecha para escribir.").arg(locale.toString(start,"MMMM yyyy")));summary->setWordWrap(true);monthTop->addWidget(summary,1,Qt::AlignVCenter);list->addLayout(monthTop);
        int notes=0;for(int i=0;i<start.daysInMonth();++i){const auto date=start.addDays(i);if(d->journals.contains(date)&&(!LumenQuery::plainText(d->journals.value(date)).isEmpty()||!d->journals.value(date).value("links").toArray().isEmpty())){list->addWidget(dayPanel(date,false));++notes;}}
        if(!notes){auto *empty=new QLabel("Todavía no hay notas en este mes.\nEscoge un día del calendario para empezar.");empty->setAlignment(Qt::AlignCenter);empty->setMinimumHeight(160);list->addWidget(empty);}list->addStretch();
    }
}
