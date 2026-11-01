#include "lumen_properties.hpp"
#include <QtWidgets>
#include <QJsonDocument>
#include <cmath>

namespace {
QString propertyKey(const QJsonValue &value){
    if(value.isString()) return value.toString();
    const auto prop=value.toObject();return prop.value("key").toString(prop.value("id").toString(prop.value("name").toString()));
}
QString propertyLabel(const QString &key,const QJsonObject &definition){
    const QString name=definition.value("name").toString();if(!name.isEmpty())return name;
    static const QHash<QString,QString> labels{
        {"author","Autor"},
        {"status","Estado"},
        {"summary","Descripción"},
        {"description","Descripción"},
        {"url","URL"},
        {"date","Fecha"},
        {"rating","Valoración"},
        {"recommended_by","Recomendado por"},
        {"medium","Medio"}
    };
    return labels.value(key,key);
}
bool internalProperty(const QString &key){
    return key.startsWith("og_")||key.startsWith("lumen_")||QStringList{"assets","blocks","rich_text","view_spec","view","query","image_asset_id","asset_id","source_object_id","source_url","preview","preview_status","preview_error","content_checksum","_lumen","favicon_asset_id"}.contains(key)||key.startsWith('_');
}
QString valueText(const QJsonValue &value){
    if(value.isString()) return value.toString();
    if(value.isDouble()) return QString::number(value.toDouble(),'g',12);
    if(value.isBool()) return value.toBool()?"Sí":"No";
    if(value.isArray()){QStringList values;for(auto v:value.toArray())values<<valueText(v);return values.join(", ");}return {};
}
QString inferredKind(const QString &key,const QJsonValue &value,const QJsonObject &definition){
    auto kind=definition.value("type").toString(definition.value("kind").toString());if(!kind.isEmpty())return kind;
    if(value.isBool()) return "boolean";
    if(value.isDouble()) return "number";
    if(key=="date") return "date";
    if(key=="url") return "url";
    return "text";
}
class RelationSearch final:public QLineEdit{
  public:
    QListWidget *choices=nullptr;
    std::function<void()> accept;
    void keyPressEvent(QKeyEvent *event)override{
        if(choices&&(event->key()==Qt::Key_Down||event->key()==Qt::Key_Up)){
            const int step=event->key()==Qt::Key_Down?1:-1;int row=choices->currentRow();
            if(row<0)row=step>0?-1:choices->count();
            for(int i=row+step;i>=0&&i<choices->count();i+=step)if(!choices->item(i)->isHidden()){choices->setCurrentRow(i);choices->scrollToItem(choices->item(i));break;}
            event->accept();return;
        }
        if((event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)&&accept){accept();event->accept();return;}
        QLineEdit::keyPressEvent(event);
    }
};
}
struct LumenProperties::Impl{
    QJsonObject object,type;QJsonArray objects,types;QFormLayout *form=nullptr;bool dark=true;
};
LumenProperties::LumenProperties(QWidget *parent):QWidget(parent),d(std::make_unique<Impl>()){
    setObjectName("LumenProperties");setProperty("lumenComponent","properties");d->form=new QFormLayout(this);d->form->setContentsMargins(0,0,0,0);d->form->setHorizontalSpacing(20);d->form->setVerticalSpacing(7);d->form->setLabelAlignment(Qt::AlignLeft|Qt::AlignVCenter);d->form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);setTheme(true);
}
LumenProperties::~LumenProperties()=default;
void LumenProperties::setData(QJsonObject object,QJsonObject type,QJsonArray objects,QJsonArray types){
    const bool unchanged=d->form->rowCount()>0&&object.value("id")==d->object.value("id")&&object.value("properties")==d->object.value("properties")&&type==d->type;
    if(unchanged){d->object=std::move(object);d->objects=std::move(objects);d->types=std::move(types);return;}
    d->object=std::move(object);d->type=std::move(type);d->objects=std::move(objects);d->types=std::move(types);rebuild();
}
void LumenProperties::setTheme(bool dark){
    d->dark=dark;
    setStyleSheet(QString("QWidget[lumenComponent=\"properties\"]{background:transparent;font-family:Inter;font-size:13px;}"
        "QWidget[lumenComponent=\"properties\"] QLabel{background:transparent;color:%1;min-width:95px;}"
        "QWidget[lumenComponent=\"properties\"] QLineEdit,QWidget[lumenComponent=\"properties\"] QDateEdit,QWidget[lumenComponent=\"properties\"] QComboBox,QWidget[lumenComponent=\"properties\"] QPushButton,QWidget[lumenComponent=\"properties\"] QToolButton{font-family:Inter;font-size:13px;color:%2;background:transparent;border:1px solid transparent;border-radius:4px;padding:5px 7px;min-height:19px;}"
        "QWidget[lumenComponent=\"properties\"] QLineEdit:hover,QWidget[lumenComponent=\"properties\"] QDateEdit:hover,QWidget[lumenComponent=\"properties\"] QComboBox:hover,QWidget[lumenComponent=\"properties\"] QPushButton:hover,QWidget[lumenComponent=\"properties\"] QToolButton:hover{background:%3;}"
        "QWidget[lumenComponent=\"properties\"] QLineEdit:focus,QWidget[lumenComponent=\"properties\"] QDateEdit:focus,QWidget[lumenComponent=\"properties\"] QComboBox:focus,QWidget[lumenComponent=\"properties\"] QPushButton:focus,QWidget[lumenComponent=\"properties\"] QToolButton:focus{background:%3;border:1px solid #d2b7f5;}"
        "QWidget[lumenComponent=\"properties\"] QPushButton{text-align:left;}QWidget[lumenComponent=\"properties\"] QComboBox::drop-down{border:0;width:18px;}")
        .arg(dark?"#919097":"#849082",dark?"#e4e3e2":"#35343a",dark?"#242428":"#f1eff4"));
}
void LumenProperties::change(const QString &key,const QJsonValue &value){
    auto properties=d->object.value("properties").toObject();const auto previous=properties.value(key);
    if((value.isNull()||value.isUndefined())&&(previous.isNull()||previous.isUndefined()))return;
    if(previous==value)return;
    if(value.isNull()||value.isUndefined())properties.remove(key);else properties[key]=value;
    // `description` fue el nombre heredado del preview web. La descripción
    // editable es `summary`; al guardar se elimina el alias para no duplicar.
    if(key=="summary")properties.remove("description");
    d->object["properties"]=properties;
    if(onChange)onChange(key,value);
}
void LumenProperties::rebuild(){
    while(d->form->rowCount())d->form->removeRow(0);
    auto properties=d->object.value("properties").toObject();
    // Compatibilidad: los enlaces web antiguos guardaban la descripción en
    // `description`. Se muestra bajo la única fila "Descripción" (`summary`).
    if(properties.value("summary").toString().isEmpty()&&properties.contains("description"))
        properties["summary"]=properties.value("description");
    QStringList keys;QHash<QString,QJsonObject> definitions;
    for(auto value:d->type.value("properties").toArray()){const QString key=propertyKey(value);if(key.isEmpty()||internalProperty(key))continue;keys<<key;definitions[key]=value.toObject();}
    const QString typeId=d->object.value("type_id").toString();
    if(typeId=="book")for(const QString &key:QStringList{"author","rating","recommended_by","medium","status"})if(!keys.contains(key))keys<<key;
    if(typeId=="web"&&!keys.contains("url"))keys<<"url";
    if(typeId!="journal"&&!keys.contains("summary"))keys<<"summary";
    for(auto it=properties.begin();it!=properties.end();++it)if(it.key()!="description"&&!internalProperty(it.key())&&!keys.contains(it.key()))keys<<it.key();
    for(const auto &key:keys){
        const auto definition=definitions.value(key);const auto value=properties.value(key);const QString kind=inferredKind(key,value,definition),name=propertyLabel(key,definition);
        auto *label=new QLabel(name);label->setTextFormat(Qt::PlainText);QWidget *editor=nullptr;
        if(kind=="object"||kind=="relation"){
            auto *button=new QPushButton;button->setObjectName("property_"+key);QStringList names;
            QJsonArray ids=value.isArray()?value.toArray():value.isString()?QJsonArray{value}:QJsonArray{};
            for(auto id:ids){QString title;for(auto ov:d->objects){auto object=ov.toObject();if(object.value("id")==id){title=object.value("title").toString();break;}}names<<(!title.isEmpty()?title:"Objeto no disponible");}
            button->setText(names.isEmpty()?"Elegir objeto…":names.join("  ·  "));button->setIcon(QIcon(":/icons/link.svg"));button->setCursor(Qt::PointingHandCursor);button->setAccessibleName(name);button->setToolTip(names.join('\n'));
            connect(button,&QPushButton::clicked,this,[this,key,definition]{chooseRelation(key,definition);});editor=button;
        }else if(kind=="boolean"||kind=="checkbox"){
            auto *combo=new QComboBox;combo->addItem("Sin valor",QVariant());combo->addItem("Sí",true);combo->addItem("No",false);combo->setCurrentIndex(value.isBool()?(value.toBool()?1:2):0);
            connect(combo,&QComboBox::currentIndexChanged,this,[this,key,combo]{change(key,combo->currentIndex()==0?QJsonValue(QJsonValue::Null):QJsonValue(combo->currentData().toBool()));});editor=combo;
        }else if(kind=="select"||kind=="enum"){
            auto *combo=new QComboBox;combo->addItem("Sin valor",QString());for(auto ov:definition.value("options").toArray()){const QString option=ov.isString()?ov.toString():ov.toObject().value("value").toString(ov.toObject().value("name").toString());combo->addItem(option,option);}
            if(combo->findData(value.toString())<0&&!value.toString().isEmpty()) combo->addItem(value.toString(),value.toString());
            combo->setCurrentIndex(qMax(0,combo->findData(value.toString())));
            connect(combo,&QComboBox::currentIndexChanged,this,[this,key,combo]{change(key,combo->currentIndex()==0?QJsonValue(QJsonValue::Null):QJsonValue(combo->currentData().toString()));});editor=combo;
        }else if(kind=="date"){
            auto *date=new QDateEdit;date->setDisplayFormat("yyyy-MM-dd");date->setCalendarPopup(true);date->setMinimumDate(QDate(1752,1,1));date->setSpecialValueText("Sin fecha");const QDate parsed=QDate::fromString(value.toString(),Qt::ISODate);date->setDate(parsed.isValid()?parsed:date->minimumDate());
            connect(date,&QDateEdit::dateChanged,this,[this,key,date](QDate selected){change(key,selected==date->minimumDate()?QJsonValue(QJsonValue::Null):QJsonValue(selected.toString(Qt::ISODate)));});editor=date;
        }else{
            auto *input=new QLineEdit;input->setText(valueText(value));input->setClearButtonEnabled(true);input->setPlaceholderText(key=="author"?"Autor":key=="summary"?"Añadir descripción…":kind=="url"?"https://…":"Vacío");
            if(kind=="number"||kind=="integer"||kind=="rating"){
                auto *validator=new QDoubleValidator(definition.value("min").toDouble(-1e12),definition.value("max").toDouble(1e12),kind=="integer"?0:8,input);validator->setLocale(QLocale::c());validator->setNotation(QDoubleValidator::StandardNotation);input->setValidator(validator);
                connect(input,&QLineEdit::textEdited,this,[this,key,input,kind](QString text){if(text.trimmed().isEmpty()){change(key,QJsonValue(QJsonValue::Null));return;}bool ok=false;double number=QLocale::c().toDouble(text,&ok);if(input->hasAcceptableInput()&&ok&&std::isfinite(number)&&(kind!="integer"||std::floor(number)==number))change(key,number);});
            }else{
                connect(input,&QLineEdit::textEdited,this,[this,key,input,kind,value](QString text){
                    if(kind=="url"&&!text.trimmed().isEmpty()){QUrl url(text.trimmed());if(!url.isValid()||url.host().isEmpty()||(url.scheme()!="https"&&url.scheme()!="http")){input->setToolTip("Introduce una URL http o https válida.");return;}}
                    input->setToolTip({});if(text.isEmpty()){change(key,QJsonValue(QJsonValue::Null));return;}
                    if(value.isArray()){QJsonArray parts;for(const auto &part:text.split(',',Qt::SkipEmptyParts))parts.append(part.trimmed());change(key,parts);}else change(key,text);
                });
            }editor=input;
        }
        editor->setObjectName("property_"+key);editor->setAccessibleName(name);
        if(typeId=="journal"&&key=="date"){editor->setEnabled(false);editor->setToolTip("La fecha identifica la nota diaria. Abre otro día desde el calendario.");}
        label->setBuddy(editor);d->form->addRow(label,editor);
    }
    auto *actions=new QWidget;auto *row=new QHBoxLayout(actions);row->setContentsMargins(0,5,0,0);row->setSpacing(10);
    auto *add=new QToolButton;add->setText("Añadir propiedad");add->setIcon(QIcon(":/icons/plus.svg"));add->setObjectName("propertyAdd");
    auto *edit=new QToolButton;edit->setText("Editar tipo");edit->setIcon(QIcon(":/icons/adjustments-horizontal.svg"));edit->setObjectName("propertyEditType");row->addWidget(add);row->addWidget(edit);row->addStretch();d->form->addRow("",actions);
    connect(add,&QToolButton::clicked,this,[this]{if(onAddProperty)onAddProperty();});connect(edit,&QToolButton::clicked,this,[this]{if(onEditType)onEditType();});
}
void LumenProperties::chooseRelation(QString key,QJsonObject definition){
    const auto prior=d->object.value("properties").toObject().value(key);
    QJsonValue result;
    if(!chooseRelationValue(this,"Elegir "+propertyLabel(key,definition),d->objects,d->types,definition,prior,d->object.value("id").toString(),result))return;
    change(key,result);rebuild();
}
bool LumenProperties::chooseRelationValue(QWidget *parent,const QString &title,const QJsonArray &objects,
                                          const QJsonArray &types,const QJsonObject &definition,
                                          const QJsonValue &prior,const QString &excludeId,QJsonValue &result){
    QDialog dialog(parent);dialog.setWindowTitle(title);dialog.resize(480,440);auto *layout=new QVBoxLayout(&dialog);
    auto *search=new RelationSearch;search->setPlaceholderText("Buscar un objeto por nombre…");search->setClearButtonEnabled(true);search->setObjectName("propertyRelationSearch");layout->addWidget(search);
    const bool multiple=definition.value("multiple").toBool(prior.isArray());
    const auto selected=prior.isArray()?prior.toArray():prior.isString()?QJsonArray{prior}:QJsonArray{};
    auto *list=new QListWidget;list->setObjectName("propertyRelationChoices");list->setSelectionMode(QAbstractItemView::SingleSelection);layout->addWidget(list,1);
    search->choices=list;search->accept=[&]{if(list->currentItem()||multiple)dialog.accept();};
    const QString target=definition.value("target_type").toString();
    for(auto ov:objects){const auto object=ov.toObject();if(!excludeId.isEmpty()&&object.value("id")==excludeId)continue;if(!target.isEmpty()&&object.value("type_id").toString()!=target)continue;
        QString type;for(auto tv:types)if(tv.toObject().value("id")==object.value("type_id")){type=tv.toObject().value("name").toString();break;}
        auto *item=new QListWidgetItem(object.value("title").toString()+(type.isEmpty()?"":"  ·  "+type),list);item->setData(Qt::UserRole,object.value("id").toString());item->setToolTip(object.value("title").toString());
        if(multiple){item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(selected.contains(object.value("id"))?Qt::Checked:Qt::Unchecked);}else if(selected.contains(object.value("id")))list->setCurrentItem(item);
    }
    if(list->count()==0){auto *empty=new QLabel("No hay objetos de este tipo disponibles.");empty->setWordWrap(true);layout->addWidget(empty);}
    connect(search,&QLineEdit::textChanged,&dialog,[list](QString text){int first=-1;for(int i=0;i<list->count();++i){const bool hidden=!list->item(i)->text().contains(text,Qt::CaseInsensitive);list->item(i)->setHidden(hidden);if(!hidden&&first<0)first=i;}if(!list->currentItem()||list->currentItem()->isHidden())list->setCurrentRow(first);});
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::Reset);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    bool clear=false;connect(buttons->button(QDialogButtonBox::Reset),&QPushButton::clicked,&dialog,[&]{clear=true;dialog.accept();});
    if(!multiple)connect(list,&QListWidget::itemDoubleClicked,&dialog,[&]{dialog.accept();});
    search->setFocus();if(dialog.exec()!=QDialog::Accepted)return false;
    result=QJsonValue(QJsonValue::Null);
    if(!clear){if(multiple){QJsonArray ids;for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->checkState()==Qt::Checked)ids.append(item->data(Qt::UserRole).toString());}result=ids;}else if(list->currentItem())result=list->currentItem()->data(Qt::UserRole).toString();}
    return true;
}
