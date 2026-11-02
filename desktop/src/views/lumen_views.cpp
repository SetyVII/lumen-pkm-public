#include "lumen_views.hpp"
#include "lumen_aliases.hpp"
#include "lumen_calculations.hpp"
#include "lumen_icons.hpp"
#include <QtWidgets>
#include <QCache>
#include <QJsonDocument>
#include <cmath>
#include <algorithm>

namespace {
QString s(const QJsonObject &o, const char *key) { return o.value(key).toString(); }
QJsonObject typeOf(const QJsonArray &types, const QString &id) {
    for (auto v : types) if (s(v.toObject(), "id") == id) return v.toObject();
    return {};
}
QJsonObject propertyDefinition(const QJsonArray &types, const QJsonObject &object, const QString &key) {
    for (auto v : typeOf(types, s(object, "type_id")).value("properties").toArray()) {
        if (v.isString() && v.toString() == key) return {{"key", key}};
        auto p = v.toObject();
        if (p.value("key").toString(p.value("id").toString(p.value("name").toString())) == key) return p;
    }
    return {};
}
QString propertyName(const QString &key) {
    static const QHash<QString, QString> labels{{"title", "Nombre"}, {"type_id", "Tipo"},
        {"created_at", "Creado"}, {"updated_at", "Actualizado"}, {"author", "Autor"},
        {"status", "Estado"}, {"url", "URL"}, {"summary", "Descripción"}, {"description", "Descripción"}, {"date", "Fecha"},
        {"rating", "Valoración"}, {"recommended_by", "Recomendado por"}, {"medium", "Medio"},
        {"links", "Enlaces"}, {"backlinks", "Referencias"}};
    const QString plain = key.startsWith("properties.") ? key.mid(11) : key;
    return labels.value(plain, plain);
}
QString relationNames(const QJsonValue &value,const QJsonArray &objects){
    QStringList names;const auto ids=value.isArray()?value.toArray():value.isString()?QJsonArray{value}:QJsonArray{};
    for(auto id:ids){QString title;for(auto ov:objects){auto object=ov.toObject();if(object.value("id")==id){title=s(object,"title");break;}}names<<(!title.isEmpty()?title:"Objeto no disponible");}
    return names.join(", ");
}
QStringList availableProperties(const QJsonArray &objects, const QJsonArray &types, const QString &scope) {
    QStringList list{"title", "type_id", "created_at", "updated_at", "links", "backlinks"};
    auto add = [&list](QString key) {
        if (QStringList{"assets","rich_text","blocks","image_asset_id","view_spec","view","query","preview","preview_status","preview_error","asset_id","source_object_id","content_checksum","favicon_asset_id"}.contains(key) ||
            key.startsWith("lumen_")||key.startsWith("og_")||key.startsWith('_')) return;
        key = "properties." + key;
        if (!list.contains(key)) list << key;
    };
    for (auto v : types) {
        auto t = v.toObject();
        if (!scope.isEmpty() && s(t, "id") != scope) continue;
        for (auto pv : t.value("properties").toArray()) {
            auto p = pv.toObject();
            add(pv.isString() ? pv.toString() : p.value("key").toString(p.value("id").toString(p.value("name").toString())));
        }
    }
    for (auto v : objects) {
        auto o = v.toObject();
        if (!scope.isEmpty() && s(o, "type_id") != scope) continue;
        const auto props = o.value("properties").toObject();
        for (auto it = props.begin(); it != props.end(); ++it) add(it.key());
    }
    return list;
}
QColor mix(QColor a, QColor b, qreal t) {
    return QColor::fromRgbF(a.redF()*(1-t)+b.redF()*t, a.greenF()*(1-t)+b.greenF()*t,
                           a.blueF()*(1-t)+b.blueF()*t, a.alphaF()*(1-t)+b.alphaF()*t);
}
QIcon typeIcon(const QString &id) {
    QString name = "file-text";
    if (id == "book") name = "book";
    if (id == "web") name = "link";
    if (id == "person") name = "user";
    if (id == "journal") name = "calendar";
    if (id == "idea") name = "bulb";
    if (id == "project") name = "folder";
    return LumenIcons::icon(name);
}
void placePopover(QWidget &popover, QWidget *anchor) {
    QPoint position=anchor->mapToGlobal(QPoint(0,anchor->height()+8));
    if(auto *screen=anchor->screen()){
        const auto available=screen->availableGeometry().adjusted(8,8,-8,-8);
        position.setX(qBound(available.left(),position.x(),qMax(available.left(),available.right()-popover.width())));
        position.setY(qBound(available.top(),position.y(),qMax(available.top(),available.bottom()-popover.height())));
    }
    popover.move(position);
}
bool blank(const QJsonValue &v) {
    return v.isNull() || v.isUndefined() || (v.isString() && v.toString().trimmed().isEmpty()) ||
        (v.isArray() && v.toArray().isEmpty());
}
bool matches(const QJsonObject &object, const QJsonObject &rule, const QJsonArray &all) {
    if (rule.contains("rules")) {
        auto rules = rule.value("rules").toArray();
        if (rules.isEmpty()) return true;
        bool isOr = rule.value("mode").toString("and") == "or";
        for (auto r : rules) {
            const bool result = matches(object, r.toObject(), all);
            if (isOr && result) return true;
            if (!isOr && !result) return false;
        }
        return !isOr;
    }
    const auto got = LumenQuery::fieldValue(object, rule.value("field").toString(), all);
    const auto wanted = rule.value("value");
    const auto op = rule.value("op").toString("contains");
    const auto a = LumenQuery::displayValue(got), b = LumenQuery::displayValue(wanted);
    if (op == "empty") return blank(got);
    if (op == "not_empty" || op == "exists") return !blank(got);
    if (op == "equals") {
        if (got.isArray() && !wanted.isArray()) return got.toArray().contains(wanted);
        if(got.isDouble()){bool valid=wanted.isDouble();double number=wanted.isDouble()?wanted.toDouble():QLocale::c().toDouble(b,&valid);if(valid)return got.toDouble()==number;}
        if(got.isBool()){const auto text=b.toCaseFolded();if(QStringList{"true","1","sí","si","yes"}.contains(text))return got.toBool();if(QStringList{"false","0","no"}.contains(text))return !got.toBool();}
        return a.compare(b, Qt::CaseInsensitive) == 0;
    }
    if (op == "not_equals") return !matches(object, {{"field", rule.value("field")}, {"op", "equals"}, {"value", wanted}}, all);
    if (op == "in") {
        for (auto v : wanted.toArray()) {
            if (got.isArray() ? got.toArray().contains(v) : a.compare(LumenQuery::displayValue(v), Qt::CaseInsensitive) == 0) return true;
        }
        return false;
    }
    if (op == "starts_with") return a.startsWith(b, Qt::CaseInsensitive);
    if (op == "ends_with") return a.endsWith(b, Qt::CaseInsensitive);
    if (op == "before" || op == "after" || op == "lt" || op == "gt" || op == "lte" || op == "gte") {
        bool numeric = wanted.isDouble();
        const double number = wanted.isDouble() ? wanted.toDouble() : QLocale::c().toDouble(b, &numeric);
        const int compare = got.isDouble() && numeric ? (got.toDouble() < number ? -1 : got.toDouble() > number ? 1 : 0)
            : QString::compare(a, b, Qt::CaseInsensitive);
        if (op == "before" || op == "lt") return compare < 0;
        if (op == "after" || op == "gt") return compare > 0;
        return op == "lte" ? compare <= 0 : compare >= 0;
    }
    if (op == "not_contains") return !a.contains(b, Qt::CaseInsensitive);
    return a.contains(b, Qt::CaseInsensitive);
}
}

namespace LumenQuery {
QString plainText(const QJsonObject &object) {
    QString text = object.value("content").toString();
    if (Qt::mightBeRichText(text)) text = QTextDocumentFragment::fromHtml(text).toPlainText();
    return text.simplified();
}
QString displayValue(const QJsonValue &v) {
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("Sí") : QStringLiteral("No");
    if (v.isDouble()) return QString::number(v.toDouble(), 'g', 12);
    if (v.isArray()) {
        QStringList parts;
        for (auto a : v.toArray()) parts << displayValue(a);
        return parts.join(", ");
    }
    if (v.isObject()) return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact));
    return {};
}
QJsonValue fieldValue(const QJsonObject &object, const QString &field, const QJsonArray &universe) {
    if (field == "content" || field == "text") return plainText(object);
    if (field == "links") {
        QJsonArray ids=object.value("links").toArray();
        for(auto id:object.value("property_links").toArray())if(!ids.contains(id))ids.append(id);
        return ids;
    }
    if (field == "backlinks") {
        QJsonArray ids;
        for (auto ov : universe) if (fieldValue(ov.toObject(),"links",universe).toArray().contains(object.value("id"))) ids.append(ov.toObject().value("id"));
        return ids;
    }
    if (field.startsWith("properties.")) return object.value("properties").toObject().value(field.mid(11));
    if (object.contains(field)) return object.value(field);
    return object.value("properties").toObject().value(field);
}
QJsonArray evaluate(const QJsonArray &objects, const QJsonArray &types, const QJsonObject &spec) {
    Q_UNUSED(types);
    QList<QJsonObject> matchesList;
    const auto scope = spec.value("type_id").toString();
    const auto search = spec.value("search").toString().trimmed();
    for (auto ov : objects) {
        const auto object = ov.toObject();
        if (!scope.isEmpty() && s(object, "type_id") != scope) continue;
        if (!spec.value("include_journals").toBool() && scope != "journal" && s(object, "type_id") == "journal") continue;
        // D6 (aliases): the view search surfaces alias matches too (the
        // same matcher the server applies to GET /v1/objects?q=), with no
        // matched-alias semantics here — the view never inserts links.
        if (!search.isEmpty()) {
            const bool text = s(object, "title").contains(search, Qt::CaseInsensitive) ||
                              plainText(object).contains(search, Qt::CaseInsensitive);
            if (!text && LumenAliases::matched(object, search).isEmpty())
                continue;
        }
        if (spec.contains("ids") && !spec.value("ids").toArray().contains(object.value("id"))) continue;
        if (!matches(object, spec.value("filter").toObject(), objects)) continue;
        matchesList << object;
    }
    // "order": "manual" keeps the curated order of the `ids` array (collections).
    // It applies only while the spec carries no explicit sort: choosing a sort in
    // the view toolbar is a deliberate user decision and wins until it is cleared.
    const QJsonArray manualIds =
        spec.value("order").toString() == "manual" && spec.contains("ids") ? spec.value("ids").toArray() : QJsonArray{};
    auto sorts = spec.value("sort").toArray();
    if (!manualIds.isEmpty() && sorts.isEmpty()) {
        QHash<QString, int> rank;
        for (int i = 0; i < manualIds.size(); ++i) rank.insert(manualIds.at(i).toString(), i);
        std::stable_sort(matchesList.begin(), matchesList.end(), [&rank](const QJsonObject &a, const QJsonObject &b) {
            const int ra = rank.value(s(a, "id"), std::numeric_limits<int>::max());
            const int rb = rank.value(s(b, "id"), std::numeric_limits<int>::max());
            return ra == rb ? s(a, "id") < s(b, "id") : ra < rb;
        });
    } else {
        if (sorts.isEmpty()) sorts = QJsonArray{QJsonObject{{"field", "updated_at"}, {"direction", "desc"}}};
        std::stable_sort(matchesList.begin(), matchesList.end(), [&objects, &sorts](const QJsonObject &a, const QJsonObject &b) {
            for (auto sv : sorts) {
                const auto sort = sv.toObject();
                const auto av = fieldValue(a, sort.value("field").toString(), objects), bv = fieldValue(b, sort.value("field").toString(), objects);
                int c = av.isDouble() && bv.isDouble() ? (av.toDouble() < bv.toDouble() ? -1 : av.toDouble() > bv.toDouble() ? 1 : 0)
                    : QString::localeAwareCompare(displayValue(av).toCaseFolded(), displayValue(bv).toCaseFolded());
                if (c) return sort.value("direction").toString() == "desc" ? c > 0 : c < 0;
            }
            return s(a, "id") < s(b, "id");
        });
    }
    QJsonArray result;
    const int limit = spec.value("limit").toInt(0);
    for (auto object : matchesList) {
        if (limit > 0 && result.size() >= limit) break;
        result.append(object);
    }
    return result;
}

EditResult validateEdit(const QJsonObject &object, const QJsonArray &types, const QString &field,
                        const QString &text, const QJsonArray &universe) {
    EditResult result;
    if (field == "title") {
        if (text.trimmed().isEmpty()) { result.error = "El nombre no puede estar vacío."; return result; }
        if (text.size() > 500) { result.error = "El nombre admite como máximo 500 caracteres."; return result; }
        result.valid = true; result.patch = {{"title", text.trimmed()}}; return result;
    }
    if (!field.startsWith("properties.")) { result.error = "Esta columna es de solo lectura."; return result; }
    const QString key = field.mid(11);
    if (key.isEmpty() || key == "assets" || key == "image_asset_id" || key == "view_spec") { result.error = "Esta propiedad se edita desde el objeto."; return result; }
    auto props = object.value("properties").toObject();
    auto definition = propertyDefinition(types, object, key);
    if (definition.isEmpty() && !props.contains(key)) {
        result.error = "Esta propiedad no está definida para este tipo de objeto.";
        return result;
    }
    const auto prior = props.value(key);
    QString kind = definition.value("type").toString(definition.value("kind").toString());
    if (kind.isEmpty()) kind = prior.isDouble() ? "number" : prior.isBool() ? "boolean" : key == "date" ? "date" : key == "url" ? "url" : "text";
    QJsonValue value(text);
    if (text.trimmed().isEmpty()) { props.remove(key); }
    else {
        if (kind == "number" || kind == "integer" || kind == "rating") {
            bool ok = false;
            const double n = QLocale::c().toDouble(text.trimmed(), &ok);
            if (!ok || !std::isfinite(n) || (kind == "integer" && std::floor(n) != n)) { result.error = "Introduce un número válido."; return result; }
            if (definition.contains("min") && n < definition.value("min").toDouble()) { result.error = "El número es menor que el mínimo permitido."; return result; }
            if (definition.contains("max") && n > definition.value("max").toDouble()) { result.error = "El número supera el máximo permitido."; return result; }
            value = n;
        } else if (kind == "boolean" || kind == "checkbox") {
            const QString v = text.trimmed().toLower();
            if (QStringList{"true", "1", "sí", "si", "yes"}.contains(v)) value = true;
            else if (QStringList{"false", "0", "no"}.contains(v)) value = false;
            else { result.error = "Usa Sí/No o true/false."; return result; }
        } else if (kind == "date") {
            if (!QDate::fromString(text.trimmed(), Qt::ISODate).isValid()) { result.error = "Usa una fecha válida con formato AAAA-MM-DD."; return result; }
            value = text.trimmed();
        } else if (kind == "url") {
            QUrl url(text.trimmed());
            if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http")) { result.error = "Introduce una URL http o https válida."; return result; }
            value = url.toString();
        } else if (kind == "relation" || kind == "object") {
            QJsonArray ids;
            for (const QString &id : text.split(',', Qt::SkipEmptyParts)) {
                bool found = false;
                for (auto v : universe) {
                    auto o = v.toObject();
                    if (s(o, "id") == id.trimmed()) {
                        found = definition.value("target_type").toString().isEmpty() || s(o, "type_id") == definition.value("target_type").toString();
                        break;
                    }
                }
                if (!found) { result.error = "La relación apunta a un objeto inexistente o de otro tipo."; return result; }
                if (!ids.contains(id.trimmed())) ids.append(id.trimmed());
            }
            value = definition.value("multiple").toBool(prior.isArray()) ? QJsonValue(ids) : ids.isEmpty() ? QJsonValue(QJsonValue::Null) : ids.first();
        } else if (kind == "select" || kind == "enum") {
            const auto options = definition.value("options").toArray();
            bool found = options.isEmpty();
            for (auto ov : options) {
                const QString option = ov.isString() ? ov.toString() : ov.toObject().value("value").toString(ov.toObject().value("name").toString());
                if (option == text) found = true;
            }
            if (!found) { result.error = "Elige uno de los valores definidos para esta propiedad."; return result; }
        } else if (prior.isArray() && kind == "text") {
            QJsonArray values;
            for (const auto &part : text.split(',', Qt::SkipEmptyParts)) values.append(part.trimmed());
            value = values;
        }
        props[key] = value;
    }
    result.valid = true; result.patch = {{"properties", props}}; return result;
}
}

namespace {
class NativeCard final : public QAbstractButton {
  public:
    QJsonObject object, type;
    QJsonArray universe;
    QPixmap image, favicon;
    QString excerpt;
    QStringList properties;
    bool dark = false, reduced = false, compact = false, dragEnabled = false, gallery=false, hasCover=false;
    qreal hover = 0;
    QPoint pressPoint;
    QVariantAnimation animation;
    QToolButton *copyUrl = nullptr;
    NativeCard(QJsonObject o, QJsonObject t, QJsonArray all, QStringList visible, bool d, bool r, QWidget *parent = nullptr)
        : QAbstractButton(parent), object(o), type(t), universe(all), properties(visible), dark(d), reduced(r) {
        setObjectName("lumenObjectCard");
        setProperty("objectId", s(object,"id"));
        setFocusPolicy(Qt::StrongFocus); setCursor(Qt::PointingHandCursor);
        setAccessibleName(s(object, "title"));
        const QString url = object.value("properties").toObject().value("url").toString();
        setToolTip(s(object,"title") + (url.isEmpty() ? QString() : "\n"+url));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        excerpt=object.value("properties").toObject().value("summary").toString();
        if(excerpt.isEmpty()) excerpt=object.value("properties").toObject().value("description").toString();
        if(excerpt.isEmpty()) excerpt=LumenQuery::plainText(object);
        if (!url.isEmpty()) {
            copyUrl = new QToolButton(this); copyUrl->setObjectName("cardCopyUrl");
            copyUrl->setIcon(LumenIcons::icon("copy")); copyUrl->setIconSize(QSize(14,14));
            copyUrl->setToolTip("Copiar URL"); copyUrl->setAccessibleName("Copiar URL");
            copyUrl->setFixedSize(26,26); copyUrl->setAutoRaise(true);
            copyUrl->setStyleSheet("QToolButton{padding:0;min-height:0;border:0;background:transparent;}QToolButton:hover{background:rgba(128,128,128,0.16);}");
            connect(copyUrl,&QToolButton::clicked,this,[url]{QApplication::clipboard()->setText(url);});
        }
        connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){hover=v.toReal();update();});
    }
    QStringList propertyLines() const {
        QStringList lines;
        for (const auto &key : properties) {
            if (key=="title" || key=="type_id" || key=="properties.url") continue;
            const auto raw=LumenQuery::fieldValue(object,key,universe);
            QString value=LumenQuery::displayValue(raw);
            const QString kind=propertyDefinition(QJsonArray{type},object,key.mid(11)).value("type").toString();
            if(key=="links"||key=="backlinks"||kind=="object"||kind=="relation") value=relationNames(raw,universe);
            if ((key=="created_at"||key=="updated_at") && value.size()>10) value=value.left(10);
            if(!value.isEmpty()) lines << propertyName(key)+"  "+value;
        }
        return lines;
    }
    QSize sizeHint() const override { return QSize(compact?420:248,compact?74:384); }
    void animate(qreal target) {
        animation.stop(); if(reduced){hover=target;update();return;}
        animation.setDuration(160);animation.setStartValue(hover);animation.setEndValue(target);
        animation.setEasingCurve(QEasingCurve::OutCubic);animation.start();
    }
    void enterEvent(QEnterEvent *e) override { animate(1);QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e) override { animate(0);QAbstractButton::leaveEvent(e); }
    void mousePressEvent(QMouseEvent *e) override { pressPoint=e->pos();QAbstractButton::mousePressEvent(e); }
    void mouseMoveEvent(QMouseEvent *e) override {
        if(dragEnabled&&e->buttons().testFlag(Qt::LeftButton)&&(e->pos()-pressPoint).manhattanLength()>=QApplication::startDragDistance()){
            auto *drag=new QDrag(this);auto *mime=new QMimeData;
            mime->setData("application/x-lumen-object",s(object,"id").toUtf8());drag->setMimeData(mime);
            drag->setPixmap(grab().scaledToWidth(210,Qt::SmoothTransformation));setDown(false);drag->exec(Qt::MoveAction);return;
        }
        QAbstractButton::mouseMoveEvent(e);
    }
    static QFont pixelFont(int px, QFont::Weight weight=QFont::Normal) {
        QFont font("Inter");font.setPixelSize(px);font.setWeight(weight);return font;
    }
    int mediaHeight() const { return gallery?qMax(1,width()-28):dragEnabled?92:128; }
    int titleBlockHeight() const { return gallery?31:53; }
    int urlTop() const { return compact?38:48+(hasCover?mediaHeight()+12:0)+titleBlockHeight(); }
    void resizeEvent(QResizeEvent *e) override {
        QAbstractButton::resizeEvent(e);
        if(gallery&&hasCover){
            const int target=mediaHeight()+164;
            if(height()!=target)setFixedHeight(target);
            setProperty("coverHeight",mediaHeight());
        }
        if(copyUrl){copyUrl->setVisible(!compact);copyUrl->move(width()-40,urlTop()-3);}
    }
    void drawTitle(QPainter &p,QRect box,const QColor &text) {
        p.setPen(text);const QFont font=pixelFont(16,QFont::DemiBold);p.setFont(font);
        const auto fm=QFontMetrics(font);QString title=s(object,"title");
        if(gallery){p.drawText(QRect(box.x(),box.y(),box.width(),24),Qt::AlignVCenter,fm.elidedText(title,Qt::ElideRight,box.width()));return;}
        if(fm.horizontalAdvance(title)<=box.width()){p.drawText(box,Qt::AlignTop,title);return;}
        QTextLayout layout(title,font);layout.beginLayout();auto line=layout.createLine();line.setLineWidth(box.width());
        const int consumed=line.textLength();layout.endLayout();
        p.drawText(QRect(box.x(),box.y(),box.width(),22),Qt::AlignVCenter,title.left(consumed).trimmed());
        p.drawText(QRect(box.x(),box.y()+22,box.width(),22),Qt::AlignVCenter,fm.elidedText(title.mid(consumed).trimmed(),Qt::ElideRight,box.width()));
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QColor tint(type.value("color").toString());if(!tint.isValid())tint=QColor("#8191ad");
        QColor bg=dark?QColor("#1b1b1e"):QColor("#fcfcfc");
        bg=mix(bg,dark?QColor("#252528"):QColor("#f2f2f4"),isDown()?1.0:hover*.8);
        const QColor text=dark?QColor("#e4e3e2"):QColor("#35343a");
        const QColor muted=dark?QColor("#9b999f"):QColor("#77757e");
        const QColor border=dark?QColor("#303035"):QColor("#e2e1e6");
        // D5: flagged templates carry a small badge wherever cards render
        // them (the Templates dashboard section is the usual place).
        const bool isTemplate=object.value("properties").toObject().value("is_template").toBool();
        const QRectF bounds=QRectF(rect()).adjusted(1,1,-1,-1);
        p.setBrush(bg);p.setPen(QPen(hasFocus()?QColor("#c4b3dc"):border,hasFocus()?2:1));p.drawRoundedRect(bounds,12,12);
        if(compact){
            typeIcon(s(object,"type_id")).paint(&p,QRect(18,24,22,22));
            const int right=width()-(!image.isNull()?79:16);
            p.setPen(text);p.setFont(pixelFont(16,QFont::DemiBold));p.drawText(QRect(56,13,right-56,24),Qt::AlignVCenter,QFontMetrics(p.font()).elidedText(s(object,"title"),Qt::ElideRight,right-56));
            p.setFont(pixelFont(12));p.setPen(muted);QString meta=s(type,"name");if(isTemplate)meta+="  ·  Template";const auto lines=propertyLines();if(!lines.isEmpty())meta+="  ·  "+lines.first();
            else if(!excerpt.isEmpty())meta+="  ·  "+excerpt;
            p.drawText(QRect(56,41,right-56,20),QFontMetrics(p.font()).elidedText(meta,Qt::ElideRight,right-56));
            if(!image.isNull()){QSize size=image.size();size.scale(54,56,Qt::KeepAspectRatio);p.drawPixmap(QRect(QPoint(width()-41-size.width()/2,37-size.height()/2),size),image);}return;
        }
        // Every card starts with its type, including those with covers.
        auto badgeType=type;if(!badgeType.contains("icon")){
            const QHash<QString,QString> icons{{"book","book"},{"web","world"},{"person","user"},{"project","folder"},{"idea","bulb"},{"journal","calendar"}};
            badgeType["icon"]=icons.value(s(object,"type_id"),"file-text");
        }
        LumenIcons::typeIcon(badgeType,23,dark).paint(&p,QRect(14,15,23,23));
        p.setFont(pixelFont(13));p.setPen(muted);p.drawText(QRect(44,15,width()-58,23),Qt::AlignVCenter,s(type,"name"));
        if(isTemplate){
            // Small pill next to the type name: the only template-specific
            // rendering on cards, painted so every presentation gets it free.
            const QFont badgeFont=pixelFont(11,QFont::DemiBold);
            const int badgeWidth=QFontMetrics(badgeFont).horizontalAdvance("Template")+14;
            const QRect pill(qMin(44+QFontMetrics(pixelFont(13)).horizontalAdvance(s(type,"name"))+10,width()-badgeWidth-14),15,badgeWidth,22);
            p.setPen(Qt::NoPen);p.setBrush(mix(bg,QColor("#73519b"),dark?.5:.12));p.drawRoundedRect(pill,11,11);
            p.setPen(dark?QColor("#e6d9f7"):QColor("#5c3f85"));p.setFont(badgeFont);p.drawText(pill,Qt::AlignCenter,"Template");
        }
        int top=48;
        if(hasCover){
            const QRect media(14,top,width()-28,mediaHeight());
            p.setBrush(dark?QColor("#141416"):QColor("#f5f5f7"));p.setPen(border);p.drawRoundedRect(media,8,8);
            if(!image.isNull()){
                QSize fit=image.size();fit.scale(media.adjusted(1,1,-1,-1).size(),Qt::KeepAspectRatio);
                const QRect destination(QPoint(media.center().x()-fit.width()/2,media.center().y()-fit.height()/2),fit);
                p.save();QPainterPath clip;clip.addRoundedRect(media,8,8);p.setClipPath(clip);p.drawPixmap(destination,image);p.restore();
            }else typeIcon(s(object,"type_id")).paint(&p,QRect(media.center()-QPoint(15,15),QSize(30,30)));
            top+=mediaHeight()+12;
        }
        drawTitle(p,QRect(14,top,width()-28,44),text);top+=titleBlockHeight();
        p.setFont(pixelFont(12));p.setPen(muted);
        const auto props=object.value("properties").toObject();const QString url=props.value("url").toString();
        if(!url.isEmpty()){
            const QRect iconRect(15,top+1,20,20);
            if(!favicon.isNull())p.drawPixmap(iconRect,favicon);else LumenIcons::icon("world",muted,20).paint(&p,iconRect);
            QString label=QUrl(url).host();if(label.startsWith("www."))label=label.mid(4);
            const QString path=QUrl(url).path();if(path!="/"&&!path.isEmpty())label+=path;
            p.setFont(pixelFont(13));p.drawText(QRect(44,top,width()-86,22),Qt::AlignVCenter,QFontMetrics(p.font()).elidedText(label,Qt::ElideRight,width()-86));
            top+=34;
        }
        // Select options and tags retain their semantic colors instead of becoming a text blob.
        bool drewChip=false;
        for(const auto &key:properties){
            if(!key.startsWith("properties.")||key=="properties.url")continue;
            const QString plain=key.mid(11);const auto def=propertyDefinition(QJsonArray{type},object,plain);const QString kind=def.value("type").toString();
            if(kind!="select"&&kind!="enum"&&kind!="tags"&&plain!="tags")continue;
            const auto raw=props.value(plain);const QJsonArray values=raw.isArray()?raw.toArray():raw.isString()&&!raw.toString().isEmpty()?QJsonArray{raw}:QJsonArray{};
            if(values.isEmpty())continue;
            int x=39;LumenIcons::icon("tag").paint(&p,QRect(15,top+4,14,14));
            for(auto value:values){
                const QString label=LumenQuery::displayValue(value);QColor color("#766197");
                for(auto option:def.value("options").toArray())if(option.isObject()){auto o=option.toObject();if(o.value("value").toString(o.value("name").toString())==label){QColor configured(o.value("color").toString());if(configured.isValid())color=configured;}}
                const int chipWidth=qMin(width()-x-14,QFontMetrics(pixelFont(12)).horizontalAdvance(label)+16);if(chipWidth<28)break;
                p.setPen(Qt::NoPen);p.setBrush(mix(bg,color,dark?.65:.22));p.drawRoundedRect(QRect(x,top,chipWidth,25),5,5);
                p.setPen(dark?QColor("#f0eaf4"):color.darker(135));p.setFont(pixelFont(12));p.drawText(QRect(x+8,top,chipWidth-16,25),Qt::AlignVCenter,QFontMetrics(p.font()).elidedText(label,Qt::ElideRight,chipWidth-16));x+=chipWidth+5;
            }
            top+=32;drewChip=true;if(top+22>height()-10)break;
        }
        p.setFont(pixelFont(12));p.setPen(muted);
        for(const auto &line:propertyLines()){
            bool chipLine=false;for(const auto &key:properties){const auto kind=propertyDefinition(QJsonArray{type},object,key.mid(11)).value("type").toString();if((kind=="select"||kind=="enum"||kind=="tags"||key=="properties.tags")&&line.startsWith(propertyName(key)+"  "))chipLine=true;}
            if(chipLine||top+20>height()-10)continue;
            p.drawText(QRect(15,top,width()-30,20),Qt::AlignVCenter,QFontMetrics(p.font()).elidedText(line,Qt::ElideRight,width()-30));top+=24;
        }
        if(!drewChip&&!excerpt.isEmpty()&&top+22<height()-10){
            p.drawText(QRect(15,top,width()-30,height()-top-14),Qt::TextWordWrap,QFontMetrics(p.font()).elidedText(excerpt,Qt::ElideRight,(width()-30)*2));
        }
    }
};

class EmptyIllustration final : public QWidget {
  public:
    bool dark;
    explicit EmptyIllustration(bool isDark,QWidget *parent=nullptr):QWidget(parent),dark(isDark){setObjectName("collectionEmptyIllustration");setFixedSize(210,190);}
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);
        QFile file(":/icons/package-open.svg");
        if(file.open(QIODevice::ReadOnly)){
            auto svg=file.readAll();svg.replace("#96969f",dark?"#3c3c42":"#c6c4cf");svg.replace("stroke-width=\"2\"","stroke-width=\"0.45\"");
            QSvgRenderer renderer(svg);renderer.render(&painter,QRectF(20,10,170,170));
        }
    }
};

class ResizeArea final : public QScrollArea {
  public:
    std::function<void()> onResize;
    void resizeEvent(QResizeEvent *e) override { QScrollArea::resizeEvent(e); if (onResize) onResize(); }
};
class DropColumn final : public QFrame {
  public:
    QString value;
    std::function<void(QString, QString)> moved;
    explicit DropColumn(QWidget *parent = nullptr) : QFrame(parent) { setAcceptDrops(true); setObjectName("kanbanColumn"); }
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasFormat("application/x-lumen-object")) { setProperty("dragOver", true); style()->unpolish(this); style()->polish(this); e->acceptProposedAction(); }
    }
    void dragLeaveEvent(QDragLeaveEvent *e) override { setProperty("dragOver", false); style()->unpolish(this); style()->polish(this); e->accept(); }
    void dropEvent(QDropEvent *e) override {
        setProperty("dragOver", false); style()->unpolish(this); style()->polish(this);
        if (moved) moved(QString::fromUtf8(e->mimeData()->data("application/x-lumen-object")), value);
        e->acceptProposedAction();
    }
};

class ObjectTable final : public QAbstractTableModel {
  public:
    QJsonArray objects, types, all;
    QStringList fields;
    // D7 (view calculations): one footer row per configured calculation,
    // aligned to the calculated property's column; `footerSummaries` carries
    // the "Sum (pages): 878" tooltips. Empty list = no footer at all.
    QList<QStringList> footerRows, footerSummaries;
    std::function<bool(QString, QString, QString)> edited;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : objects.size() + footerRows.size(); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : fields.size(); }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role != Qt::DisplayRole) return {};
        return orientation == Qt::Horizontal ? QVariant(propertyName(fields.value(section))) : QVariant(section+1);
    }
    QVariant data(const QModelIndex &index, int role = {}) const override {
        if (!index.isValid()) return {};
        if (index.row() >= objects.size()) {
            const int footer = index.row() - objects.size();
            const QString text = footerRows.value(footer).value(index.column());
            if (text.isEmpty()) return {};
            if (role == Qt::ToolTipRole) return QVariant(footerSummaries.value(footer).value(index.column()));
            if (role == Qt::DisplayRole || role == Qt::EditRole) return QVariant(text);
            if (role == Qt::FontRole) { QFont font; font.setWeight(QFont::DemiBold); return QVariant(font); }
            return {};
        }
        const auto object = objects.at(index.row()).toObject(); const auto field = fields.value(index.column());
        auto value = LumenQuery::fieldValue(object, field, all);
        if (role == Qt::UserRole) return s(object, "id");
        if (role == Qt::EditRole || role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            if (field == "type_id" && role != Qt::EditRole) return s(typeOf(types, value.toString()), "name");
            if ((field == "created_at" || field == "updated_at") && role != Qt::EditRole) return value.toString().left(10);
            if ((field == "links" || field == "backlinks") && role != Qt::EditRole) {
                QStringList names;
                for (auto id : value.toArray()) for (auto ov : all) if (ov.toObject().value("id") == id) names << s(ov.toObject(), "title");
                return names.join(", ");
            }
            if(field.startsWith("properties.")&&role!=Qt::EditRole){const auto kind=propertyDefinition(types,object,field.mid(11)).value("type").toString();if(kind=="object"||kind=="relation")return relationNames(value,all);}
            return LumenQuery::displayValue(value);
        }
        if (role == Qt::ForegroundRole && field == "type_id") { QColor color(s(typeOf(types, s(object, "type_id")), "color")); if (color.isValid()) return color; }
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &index) const override {
        // The footer is a summary, not a row: nothing selects or edits it.
        if (index.isValid() && index.row() >= objects.size()) return Qt::NoItemFlags;
        auto f = QAbstractTableModel::flags(index); const auto field = fields.value(index.column());
        if (field == "title" || field.startsWith("properties.")) f |= Qt::ItemIsEditable;
        return f;
    }
    bool setData(const QModelIndex &index, const QVariant &value, int role) override {
        if (!index.isValid() || role != Qt::EditRole || !edited) return false;
        if (index.row() >= objects.size()) return false;
        return edited(s(objects.at(index.row()).toObject(), "id"), fields.value(index.column()), value.toString());
    }
};

class TypedCellDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent,const QStyleOptionViewItem &option,const QModelIndex &index)const override{
        auto *model=dynamic_cast<const ObjectTable*>(index.model());if(!model)return QStyledItemDelegate::createEditor(parent,option,index);
        const auto object=model->objects.at(index.row()).toObject();const QString field=model->fields.value(index.column());
        if(!field.startsWith("properties."))return QStyledItemDelegate::createEditor(parent,option,index);
        const auto definition=propertyDefinition(model->types,object,field.mid(11));const auto value=LumenQuery::fieldValue(object,field,model->all);
        const QString kind=definition.value("type").toString(value.isDouble()?"number":value.isBool()?"boolean":"text");
        if(kind=="select"||kind=="enum"||kind=="boolean"||kind=="checkbox"){
            auto *combo=new QComboBox(parent);
            if(kind=="boolean"||kind=="checkbox"){combo->addItem("Sí","true");combo->addItem("No","false");}
            else{combo->addItem("Sin valor","");for(auto ov:definition.value("options").toArray()){const auto option=ov.isString()?ov.toString():ov.toObject().value("value").toString(ov.toObject().value("name").toString());combo->addItem(option,option);}}
            return combo;
        }
        if(kind=="date"){
            auto *date=new QDateEdit(parent);date->setCalendarPopup(true);date->setDisplayFormat("yyyy-MM-dd");date->setMinimumDate(QDate(1752,1,1));date->setMaximumDate(QDate(9999,12,31));return date;
        }
        if(kind=="number"||kind=="integer"||kind=="rating"){
            auto *number=new QDoubleSpinBox(parent);number->setLocale(QLocale::c());number->setDecimals(kind=="integer"?0:6);number->setRange(definition.value("min").toDouble(-1e12),definition.value("max").toDouble(1e12));return number;
        }
        return QStyledItemDelegate::createEditor(parent,option,index);
    }
    void setEditorData(QWidget *editor,const QModelIndex &index)const override{
        const QString value=index.data(Qt::EditRole).toString();
        if(auto *combo=qobject_cast<QComboBox*>(editor)){QString normalized=value;if(value=="Sí")normalized="true";if(value=="No")normalized="false";combo->setCurrentIndex(qMax(0,combo->findData(normalized)));return;}
        if(auto *date=qobject_cast<QDateEdit*>(editor)){const auto parsed=QDate::fromString(value,Qt::ISODate);date->setDate(parsed.isValid()?parsed:QDate::currentDate());return;}
        if(auto *number=qobject_cast<QDoubleSpinBox*>(editor)){number->setValue(value.toDouble());return;}
        QStyledItemDelegate::setEditorData(editor,index);
    }
    void setModelData(QWidget *editor,QAbstractItemModel *model,const QModelIndex &index)const override{
        if(auto *combo=qobject_cast<QComboBox*>(editor)){model->setData(index,combo->currentData().toString(),Qt::EditRole);return;}
        if(auto *date=qobject_cast<QDateEdit*>(editor)){model->setData(index,date->date().toString(Qt::ISODate),Qt::EditRole);return;}
        if(auto *number=qobject_cast<QDoubleSpinBox*>(editor)){model->setData(index,QString::number(number->value(),'g',12),Qt::EditRole);return;}
        QStyledItemDelegate::setModelData(editor,model,index);
    }
};

// Recursive editor: nested groups are ordinary native widgets, never JSON for the user.
class FilterGroup final : public QFrame {
  public:
    QComboBox *mode;
    QVBoxLayout *rows;
    QStringList fields;
    QList<std::function<QJsonObject()>> values;
    explicit FilterGroup(QStringList fs, QJsonObject initial = {}, int depth = 0, QWidget *parent = nullptr)
        : QFrame(parent), fields(fs) {
        setObjectName("filterGroup"); auto *layout = new QVBoxLayout(this); layout->setSpacing(8);
        auto *header = new QHBoxLayout; mode = new QComboBox;
        mode->addItem("Todas las condiciones (Y)", "and"); mode->addItem("Cualquier condición (O)", "or");
        mode->setCurrentIndex(initial.value("mode").toString() == "or" ? 1 : 0); header->addWidget(mode); header->addStretch();
        auto *add = new QPushButton("Condición"); add->setIcon(LumenIcons::icon("plus")); header->addWidget(add);
        auto *group = new QPushButton("Grupo"); group->setIcon(LumenIcons::icon("plus")); header->addWidget(group); group->setEnabled(depth < 3); layout->addLayout(header);
        rows = new QVBoxLayout; rows->setSpacing(7); layout->addLayout(rows);
        connect(add, &QPushButton::clicked, this, [this] { addRule({}); });
        connect(group, &QPushButton::clicked, this, [this, depth] { addGroup({}, depth+1); });
        for (auto v : initial.value("rules").toArray()) {
            auto rule = v.toObject(); if (rule.contains("rules")) addGroup(rule, depth+1); else addRule(rule);
        }
    }
    void addGroup(QJsonObject rule, int depth) {
        auto *wrapper=new QWidget;auto *line=new QHBoxLayout(wrapper);line->setContentsMargins(0,0,0,0);
        auto *group = new FilterGroup(fields, rule, depth);line->addWidget(group,1);auto *remove=new QPushButton;remove->setIcon(LumenIcons::icon("x"));remove->setAccessibleName("Quitar grupo");remove->setFixedWidth(28);line->addWidget(remove,0,Qt::AlignTop);rows->addWidget(wrapper);
        auto alive=std::make_shared<bool>(true);connect(remove,&QPushButton::clicked,wrapper,[wrapper,alive]{*alive=false;wrapper->hide();});
        values << [group,alive] { return *alive?group->value():QJsonObject{}; };
    }
    void addRule(QJsonObject rule) {
        auto *row = new QWidget; auto *line = new QHBoxLayout(row); line->setContentsMargins(0, 0, 0, 0);
        auto *field = new QComboBox; field->setMinimumWidth(130);
        QStringList allFields = fields; if (!allFields.contains("content")) allFields << "content";
        for (const auto &key : allFields) field->addItem(key == "content" ? "Contenido" : propertyName(key), key);
        auto *op = new QComboBox;
        for (auto pair : QList<QPair<QString,QString>>{{"contiene","contains"},{"no contiene","not_contains"},{"es","equals"},{"no es","not_equals"},{"empieza por","starts_with"},{"termina en","ends_with"},{"está vacío","empty"},{"no está vacío","not_empty"},{"anterior / menor","before"},{"posterior / mayor","after"}}) op->addItem(pair.first, pair.second);
        auto *text = new QLineEdit; text->setPlaceholderText("Valor; relaciones por ID");
        field->setCurrentIndex(qMax(0, field->findData(rule.value("field").toString("title"))));
        op->setCurrentIndex(qMax(0, op->findData(rule.value("op").toString("contains")))); text->setText(LumenQuery::displayValue(rule.value("value")));
        auto *remove = new QPushButton; remove->setIcon(LumenIcons::icon("x")); remove->setFixedWidth(30); remove->setAccessibleName("Quitar condición");
        line->addWidget(field); line->addWidget(op); line->addWidget(text, 1); line->addWidget(remove); rows->addWidget(row);
        auto alive = std::make_shared<bool>(true);
        connect(remove, &QPushButton::clicked, row, [row, alive] { *alive = false; row->hide(); });
        auto update = [op,text] { const bool enabled = op->currentData().toString() != "empty" && op->currentData().toString() != "not_empty"; text->setEnabled(enabled); };
        connect(op, &QComboBox::currentIndexChanged, row, update); update();
        values << [field,op,text,alive] {
            if (!*alive) return QJsonObject{};
            return QJsonObject{{"field",field->currentData().toString()},{"op",op->currentData().toString()},{"value",text->text()}};
        };
    }
    QJsonObject value() const {
        QJsonArray rules; for (auto get : values) { auto v = get(); if (!v.isEmpty()&&(!v.contains("rules")||!v.value("rules").toArray().isEmpty())) rules.append(v); }
        return {{"mode",mode->currentData().toString()},{"rules",rules}};
    }
};
}

struct LumenViews::Impl {
    QJsonArray objects, types, result;
    QJsonObject spec;
    bool dark = false, reduced = false, rebuilding = false, overview = false, collectionNavigation = false;
    bool showOpened = true, showCreated = true;
    QStringList recentIds;
    QLineEdit *search = nullptr;
    QComboBox *presentation = nullptr, *sort = nullptr, *group = nullptr;
    QLabel *count = nullptr, *message = nullptr;
    QVBoxLayout *content = nullptr;
    QToolButton *overviewTab = nullptr, *allTab = nullptr;
    QToolButton *filters = nullptr, *undo = nullptr, *sortButton = nullptr, *renderButton = nullptr, *propertiesButton = nullptr, *personalize = nullptr;
    QFrame *tools = nullptr;
    QMenu *sortMenu = nullptr, *renderMenu = nullptr;
    QCache<QString,QPixmap> images{64*1024}; // Cost in KiB; plaintext lives only in RAM.
    QSet<QString> requested, failed;
    struct AssetTarget { QPointer<NativeCard> card; bool favicon = false; };
    QHash<QString,QList<AssetTarget>> waiting;
    QHash<QString,int> widths;
    QStringList columnOrder;
    QJsonArray deferredObjects,deferredTypes;
    bool refreshScheduled=false;
    struct Move { QString id, field; QJsonValue old; };
    QList<Move> undoStack;
};

LumenViews::LumenViews(QWidget *parent) : QWidget(parent), d(std::make_unique<Impl>()) {
    setObjectName("LumenViews");setProperty("lumenComponent","views");
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(16);
    auto *toolbar=new QHBoxLayout;toolbar->setSpacing(8);
    auto makeTab=[toolbar](const QString &name,const QString &label,const QString &icon){
        auto *tab=new QToolButton;tab->setObjectName(name);tab->setText(label);tab->setIcon(LumenIcons::icon(icon));tab->setIconSize(QSize(16,16));
        tab->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);tab->setCheckable(true);tab->setProperty("collectionTab",true);tab->setAccessibleName(label);tab->hide();toolbar->addWidget(tab);return tab;
    };
    d->overviewTab=makeTab("collectionOverview","Vista general","layout-grid");d->allTab=makeTab("collectionAll","Todo","list");
    auto *tabs=new QButtonGroup(this);tabs->setExclusive(true);tabs->addButton(d->overviewTab);tabs->addButton(d->allTab);
    connect(d->overviewTab,&QToolButton::clicked,this,[this]{setCollectionMode(true);});
    connect(d->allTab,&QToolButton::clicked,this,[this]{setCollectionMode(false);});
    toolbar->addStretch();d->count=new QLabel;d->count->setObjectName("viewCount");toolbar->addWidget(d->count);
    d->search=new QLineEdit;d->search->setObjectName("viewSearch");d->search->setPlaceholderText("Buscar…");d->search->setClearButtonEnabled(true);
    d->search->addAction(LumenIcons::icon("search"),QLineEdit::LeadingPosition);d->search->setAccessibleName("Buscar en esta vista");d->search->setMinimumWidth(72);d->search->setMaximumWidth(180);d->search->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);toolbar->addWidget(d->search);
    // Hidden native selectors retain a single source of truth for programmatic views.
    d->presentation=new QComboBox(this);d->presentation->setObjectName("viewPresentation");d->presentation->hide();
    d->sort=new QComboBox(this);d->sort->setObjectName("viewSort");d->sort->hide();
    d->group=new QComboBox(this);d->group->setObjectName("viewGroup");d->group->hide();
    d->tools=new QFrame;d->tools->setObjectName("collectionTools");
    auto *tools=new QHBoxLayout(d->tools);tools->setContentsMargins(3,2,3,2);tools->setSpacing(0);
    auto makeTool=[tools](const QString &name,const QString &icon,const QString &label){
        auto *button=new QToolButton;button->setObjectName(name);button->setIcon(LumenIcons::icon(icon));button->setIconSize(QSize(16,16));
        button->setToolTip(label);button->setAccessibleName(label);button->setAutoRaise(true);button->setFixedSize(34,30);tools->addWidget(button);return button;
    };
    d->filters=makeTool("viewFilters","filter","Filtrar");d->filters->setCheckable(true);
    d->sortButton=makeTool("viewSortButton","sort-descending","Ordenar y agrupar");d->sortMenu=new QMenu(d->sortButton);d->sortButton->setMenu(d->sortMenu);d->sortButton->setPopupMode(QToolButton::InstantPopup);
    d->renderButton=makeTool("viewRenderButton","layout-grid","Cambiar representación");d->renderMenu=new QMenu(d->renderButton);d->renderButton->setMenu(d->renderMenu);d->renderButton->setPopupMode(QToolButton::InstantPopup);
    const QList<QPair<QString,QString>> modes{{"Galería","gallery"},{"Lista","list"},{"Tabla","table"},{"Muro","wall"},{"Kanban","kanban"},{"Incrustación","embed"}};
    const QStringList icons{"layout-grid","list","table","layout-board-split","layout-kanban","file-text"};
    for(int i=0;i<modes.size();++i){
        d->presentation->addItem(modes[i].first,modes[i].second);
        auto *action=d->renderMenu->addAction(LumenIcons::icon(icons[i]),modes[i].first);action->setData(modes[i].second);action->setCheckable(true);
        connect(action,&QAction::triggered,this,[this,mode=modes[i].second]{setPresentation(mode);});
    }
    d->renderMenu->addSeparator();auto *save=d->renderMenu->addAction(LumenIcons::icon("file-text"),"Guardar como vista…");connect(save,&QAction::triggered,this,[this]{saveView();});
    d->propertiesButton=makeTool("viewPropertiesButton","adjustments-horizontal","Propiedades visibles");toolbar->addWidget(d->tools);
    d->personalize=new QToolButton;d->personalize->setObjectName("overviewPersonalize");d->personalize->setIcon(LumenIcons::icon("settings"));
    d->personalize->setText("Personalizar");d->personalize->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);d->personalize->hide();
    auto *personalizeMenu=new QMenu(d->personalize);d->personalize->setMenu(personalizeMenu);d->personalize->setPopupMode(QToolButton::InstantPopup);
    auto *opened=personalizeMenu->addAction("Recientemente abierto");opened->setCheckable(true);opened->setChecked(true);
    auto *created=personalizeMenu->addAction("Recientemente creado");created->setCheckable(true);created->setChecked(true);
    connect(opened,&QAction::toggled,this,[this,created](bool show){d->showOpened=show;if(!show&&!d->showCreated)created->setChecked(true);rebuild();});
    connect(created,&QAction::toggled,this,[this,opened](bool show){d->showCreated=show;if(!show&&!d->showOpened)opened->setChecked(true);rebuild();});
    toolbar->addWidget(d->personalize);
    d->undo=new QToolButton;d->undo->setText("Deshacer movimiento");d->undo->hide();toolbar->addWidget(d->undo);layout->addLayout(toolbar);
    d->message=new QLabel;d->message->setWordWrap(true);d->message->hide();layout->addWidget(d->message);
    d->content=new QVBoxLayout;d->content->setContentsMargins(0,0,0,0);layout->addLayout(d->content,1);
    connect(d->search,&QLineEdit::textChanged,this,[this](QString text){if(!d->rebuilding){d->spec["search"]=text;rebuild();}});
    connect(d->presentation,&QComboBox::currentIndexChanged,this,[this]{if(!d->rebuilding)setPresentation(d->presentation->currentData().toString());});
    connect(d->filters,&QToolButton::clicked,this,[this]{editFilters();});
    connect(d->propertiesButton,&QToolButton::clicked,this,[this]{editProperties();});
    connect(d->undo,&QToolButton::clicked,this,[this]{undoLastMove();});
    auto *undoAction=new QAction(this);undoAction->setShortcut(QKeySequence::Undo);undoAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);addAction(undoAction);connect(undoAction,&QAction::triggered,this,[this]{undoLastMove();});
    connect(d->sort,&QComboBox::currentIndexChanged,this,[this]{
        if(d->rebuilding)return;
        const auto data=d->sort->currentData().toMap();
        d->spec["sort"]=QJsonArray{QJsonObject{{"field",data.value("field").toString()},{"direction",data.value("direction").toString()}}};rebuild();
    });
    connect(d->group,&QComboBox::currentIndexChanged,this,[this]{if(!d->rebuilding){d->spec["group_by"]=d->group->currentData().toString();rebuild();}});
    setTheme(false,false);
}
LumenViews::~LumenViews() = default;
void LumenViews::setData(QJsonArray objects, QJsonArray types) {
    if(objects==d->objects&&types==d->types)return;
    // Polling must not tear down a native editor while the user is typing in a cell.
    auto *table=findChild<QTableView*>("objectsTable");auto *focus=QApplication::focusWidget();
    if(table&&focus&&table->isAncestorOf(focus)&&(qobject_cast<QLineEdit*>(focus)||qobject_cast<QAbstractSpinBox*>(focus)||qobject_cast<QComboBox*>(focus))){
        d->deferredObjects=std::move(objects);d->deferredTypes=std::move(types);
        if(!d->refreshScheduled){d->refreshScheduled=true;QTimer::singleShot(200,this,[this]{if(!d->refreshScheduled)return;d->refreshScheduled=false;setData(d->deferredObjects,d->deferredTypes);});}
        return;
    }
    d->refreshScheduled=false;
    const auto newResult = LumenQuery::evaluate(objects, types, d->spec);
    const bool unchanged = (d->content->count() > 0 && newResult == d->result && types == d->types);
    d->objects = std::move(objects);
    d->types = std::move(types);
    if(unchanged) return;
    rebuild();
}
void LumenViews::setScope(QString typeId, QJsonObject spec) {
    if (!typeId.isEmpty()) spec["type_id"] = typeId;
    if (!spec.contains("presentation")) spec["presentation"] = "gallery";
    if (!spec.contains("visible_properties")) {
        QJsonArray props{"title", "type_id"};
        if (typeId == "book") {
            props.append("properties.author");
            props.append("properties.rating");
            props.append("properties.recommended_by");
            props.append("properties.medium");
            props.append("properties.status");
        }
        else if (typeId == "web") props.append("properties.url");
        else props.append("updated_at");
        for(auto value:typeOf(d->types,typeId).value("properties").toArray()){
            const auto definition=value.toObject();const QString key=definition.value("key").toString(definition.value("id").toString());
            const QString kind=definition.value("type").toString();
            if((kind=="select"||kind=="enum"||kind=="tags")&&!key.isEmpty()&&!props.contains("properties."+key))props.append("properties."+key);
        }
        spec["visible_properties"] = props;
    }
    if (d->spec == spec) return;
    d->spec = std::move(spec);
    d->widths.clear();const auto widths=d->spec.value("column_widths").toObject();for(auto it=widths.begin();it!=widths.end();++it)d->widths[it.key()]=qBound(60,it.value().toInt(170),1200);
    d->columnOrder.clear(); rebuild();
}
QJsonObject LumenViews::viewSpec() const {
    QJsonObject spec=d->spec,widths;for(auto it=d->widths.begin();it!=d->widths.end();++it)widths[it.key()]=it.value();spec["column_widths"]=widths;return spec;
}
QStringList LumenViews::visibleIds() const { QStringList ids; for (auto v : d->result) ids << s(v.toObject(), "id"); return ids; }
void LumenViews::setPresentation(const QString &name) {
    if (!QStringList{"gallery","wall","list","table","kanban","embed"}.contains(name)) return;
    d->spec["presentation"] = name;
    if (name == "kanban" && d->spec.value("group_by").toString().isEmpty()) d->spec["group_by"] = "properties.status";
    rebuild();
}
void LumenViews::setCollectionMode(bool overview) {
    const QString mode=overview?"overview":"all";
    const bool changed=d->overview!=overview||!d->collectionNavigation||d->spec.value("collection_mode").toString()!=mode;
    d->overview=overview;d->collectionNavigation=true;d->spec["collection_mode"]=mode;
    if(changed)rebuild();
}
bool LumenViews::collectionMode() const { return d->overview; }
void LumenViews::clearCollectionMode() {
    const bool changed=d->overview||d->collectionNavigation||d->spec.contains("collection_mode");
    if(!changed) return;
    d->overview=false;d->collectionNavigation=false;d->spec.remove("collection_mode");
    rebuild();
}
void LumenViews::setRecentIds(QStringList ids) {
    ids.removeDuplicates();if(ids==d->recentIds)return;d->recentIds=std::move(ids);if(d->overview)rebuild();
}
void LumenViews::setTheme(bool dark, bool reducedMotion) {
    d->dark = dark; d->reduced = reducedMotion;
    const QString bg = dark ? "#151518" : "#fcfcfc", panel = dark ? "#1c1c20" : "#fcfcfc", fg = dark ? "#e4e3e2" : "#363d38", border = dark ? "#29292e" : "#e6e4e8", hover = dark ? "#242428" : "#f1eff4";
    setStyleSheet(QString("QWidget[lumenComponent=\"views\"]{background:%1;color:%2;font-family:Inter;font-size:15.5px;}"
        "QWidget[lumenComponent=\"views\"] QLineEdit,QWidget[lumenComponent=\"views\"] QComboBox,QWidget[lumenComponent=\"views\"] QToolButton{font-family:Inter;font-size:13px;background:%3;color:%2;border:1px solid %4;border-radius:5px;padding:6px 9px;min-height:18px;}"
        "QWidget[lumenComponent=\"views\"] QToolButton:hover,QWidget[lumenComponent=\"views\"] QComboBox:hover{background:%5;}"
        "QWidget[lumenComponent=\"views\"] QToolButton:pressed{background:%4;}"
        "QWidget[lumenComponent=\"views\"] QToolButton:focus,QWidget[lumenComponent=\"views\"] QComboBox:focus,QWidget[lumenComponent=\"views\"] QLineEdit:focus{border:1px solid #d2b7f5;}"
        "QWidget[lumenComponent=\"views\"] QLabel{color:%2;background:transparent;}"
        "QWidget[lumenComponent=\"views\"] QLabel#viewCount{color:%6;font-size:13px;padding:0 8px;}"
        "QWidget[lumenComponent=\"views\"] QFrame#collectionTools{background:%3;border:1px solid %4;border-radius:12px;}"
        "QWidget[lumenComponent=\"views\"] QFrame#collectionTools QToolButton{border:0;background:transparent;padding:0;min-height:0;border-radius:8px;}"
        "QWidget[lumenComponent=\"views\"] QFrame#collectionTools QToolButton:hover,QWidget[lumenComponent=\"views\"] QFrame#collectionTools QToolButton:checked{background:%5;}"
        "QWidget[lumenComponent=\"views\"] QToolButton::menu-indicator{image:none;}"
        "QWidget[lumenComponent=\"views\"] QToolButton[collectionTab=true]{border:0;background:transparent;border-radius:12px;padding:7px 13px;color:%6;}"
        "QWidget[lumenComponent=\"views\"] QToolButton[collectionTab=true]:checked{background:%5;color:%2;}"
        "QWidget[lumenComponent=\"views\"] QLabel#viewEmpty{font-size:17px;font-weight:600;}"
        "QWidget[lumenComponent=\"views\"] QLabel#viewEmptyDescription{font-size:15px;color:%6;}"
        // D7 (view calculations): the summary strip at the top of the card
        // presentations and the per-group lines under group headers.
        "QWidget[lumenComponent=\"views\"] QLabel#viewCalculations,QWidget[lumenComponent=\"views\"] QLabel#groupCalculations{color:%6;font-size:13px;padding:0 2px;}"
        "QWidget[lumenComponent=\"views\"] QLabel#overviewSectionTitle{font-size:16px;font-weight:600;}"
        "QWidget[lumenComponent=\"views\"] QToolButton#emptyCreate{background:%2;color:%1;border:0;}"
        "QWidget[lumenComponent=\"views\"] QToolButton#emptyImport{background:transparent;}"
        "QWidget[lumenComponent=\"views\"] QScrollArea{border:0;background:transparent;}"
        "QWidget[lumenComponent=\"views\"] QWidget#collectionCanvas,QWidget[lumenComponent=\"views\"] QWidget#overviewCardRow,QWidget[lumenComponent=\"views\"] QWidget#collectionViewport{background:%1;}"
        "QWidget[lumenComponent=\"views\"] QFrame#kanbanColumn{background:%3;border:1px solid %4;border-radius:7px;}"
        "QWidget[lumenComponent=\"views\"] QFrame#kanbanColumn[dragOver=true]{border:2px solid #d2b7f5;}"
        "QWidget[lumenComponent=\"views\"] QTableView{background:%3;alternate-background-color:%1;color:%2;gridline-color:%4;border:1px solid %4;border-radius:5px;selection-background-color:%5;selection-color:%2;}"
        "QWidget[lumenComponent=\"views\"] QHeaderView::section{background:%1;color:%2;border:0;border-bottom:1px solid %4;padding:9px;font-weight:500;}"
        "QWidget[lumenComponent=\"views\"] QTableView::item{padding:7px;}QWidget[lumenComponent=\"views\"] QTableView::item:hover{background:%5;}"
        "QWidget[lumenComponent=\"views\"] QScrollBar:vertical{background:transparent;width:8px;margin:0;}"
        "QWidget[lumenComponent=\"views\"] QScrollBar:horizontal{background:transparent;height:8px;margin:0;}"
        "QWidget[lumenComponent=\"views\"] QScrollBar::handle{background:%4;border-radius:4px;min-width:24px;min-height:24px;}"
        "QWidget[lumenComponent=\"views\"] QScrollBar::add-line,QWidget[lumenComponent=\"views\"] QScrollBar::sub-line{width:0;height:0;}"
        "QWidget[lumenComponent=\"views\"] QScrollBar::add-page,QWidget[lumenComponent=\"views\"] QScrollBar::sub-page{background:transparent;}")
        .arg(bg,fg,panel,border,hover,dark ? "#919097" : "#7b857e"));
    rebuild();
}

void LumenViews::rebuild() {
    if (d->rebuilding) return;
    d->rebuilding = true;
    d->result = LumenQuery::evaluate(d->objects,d->types,d->spec);
    d->presentation->setCurrentIndex(qMax(0,d->presentation->findData(d->spec.value("presentation").toString("gallery"))));
    if (d->search->text()!=d->spec.value("search").toString()) d->search->setText(d->spec.value("search").toString());
    d->overviewTab->setVisible(d->collectionNavigation);d->allTab->setVisible(d->collectionNavigation);
    d->overviewTab->setChecked(d->overview);d->allTab->setChecked(!d->overview);
    d->count->setText(QString::number(d->result.size()));
    d->count->setToolTip(QString("%1 objetos").arg(d->result.size()));
    d->tools->setVisible(!d->overview);d->search->setVisible(!d->overview);d->count->setVisible(!d->overview);d->personalize->setVisible(d->overview);
    const QString selectedMode=d->spec.value("presentation").toString("gallery");
    const QHash<QString,QString> renderIcons{{"gallery","layout-grid"},{"list","list"},{"table","table"},{"wall","layout-board-split"},{"kanban","layout-kanban"},{"embed","file-text"}};
    d->renderButton->setIcon(LumenIcons::icon(renderIcons.value(selectedMode,"layout-grid")));
    for(auto *action:d->renderMenu->actions())if(action->isCheckable())action->setChecked(action->data().toString()==selectedMode);
    const auto filter = d->spec.value("filter").toObject();
    const int filterCount = filter.value("rules").toArray().size();
    d->filters->setChecked(filterCount>0);d->filters->setToolTip(filterCount?QString("Filtrar · %1 condiciones").arg(filterCount):"Filtrar");
    const auto fields = availableProperties(d->objects,d->types,d->spec.value("type_id").toString());
    d->sort->clear(); d->group->clear(); d->group->addItem("Sin agrupar", "");
    for (const auto &field : fields) {
        if (field=="links" || field=="backlinks") continue;
        for (auto direction : {QString("asc"),QString("desc")}) d->sort->addItem(propertyName(field)+(direction=="asc" ? " (ascendente)" : " (descendente)"),QVariantMap{{"field",field},{"direction",direction}});
        if (field!="title" && field!="created_at" && field!="updated_at") d->group->addItem(propertyName(field),field);
    }
    auto selectedSort=d->spec.value("sort").toArray();
    const auto sort = selectedSort.isEmpty() ? QJsonObject{{"field","updated_at"},{"direction","desc"}} : selectedSort.first().toObject();
    for(int i=0;i<d->sort->count();++i) {
        auto data=d->sort->itemData(i).toMap();
        if(data.value("field").toString()==sort.value("field").toString() && data.value("direction").toString()==sort.value("direction").toString()) d->sort->setCurrentIndex(i);
    }
    d->group->setCurrentIndex(qMax(0,d->group->findData(d->spec.value("group_by").toString())));
    d->sortMenu->clear();
    auto *sortHeading=d->sortMenu->addSection("Ordenar por");Q_UNUSED(sortHeading);
    for(const auto &field:fields){
        if(field=="links"||field=="backlinks")continue;
        auto *fieldMenu=d->sortMenu->addMenu(propertyName(field));
        for(const QString &direction:QStringList{"asc","desc"}){
            auto *action=fieldMenu->addAction(direction=="asc"?"Ascendente":"Descendente");action->setCheckable(true);
            action->setChecked(sort.value("field").toString()==field&&sort.value("direction").toString()==direction);
            connect(action,&QAction::triggered,this,[this,field,direction]{d->spec["sort"]=QJsonArray{QJsonObject{{"field",field},{"direction",direction}}};QTimer::singleShot(0,this,[this]{rebuild();});});
        }
    }
    d->sortMenu->addSeparator();auto *groupMenu=d->sortMenu->addMenu("Agrupar por");
    for(int i=0;i<d->group->count();++i){const QString field=d->group->itemData(i).toString();auto *action=groupMenu->addAction(d->group->itemText(i));action->setCheckable(true);action->setChecked(field==d->spec.value("group_by").toString());connect(action,&QAction::triggered,this,[this,field]{d->spec["group_by"]=field;QTimer::singleShot(0,this,[this]{rebuild();});});}
    const auto calculations=d->spec.value("calculations").toArray();
    d->sortMenu->addSeparator();
    auto *calculationMenu=d->sortMenu->addMenu("Calculations");
    auto *addCalculation=calculationMenu->addAction(LumenIcons::icon("plus"),"Add calculation…");
    connect(addCalculation,&QAction::triggered,this,[this]{editCalculation(-1);});
    if(!calculations.isEmpty())calculationMenu->addSeparator();
    for(int i=0;i<calculations.size();++i){
        const auto calculation=calculations.at(i).toObject();
        const QString field=calculation.value("property").toString();
        auto *action=calculationMenu->addAction(LumenCalculations::menuLabel(LumenCalculations::kindOf(d->types,d->objects,field),calculation.value("op").toString())+" · "+propertyName(field));
        connect(action,&QAction::triggered,this,[this,i]{editCalculation(i);});
    }

    d->undo->setVisible(!d->undoStack.isEmpty());
    int scrollY=0,scrollX=0;QString selectedId,selectedField,focusedCard;bool tableFocus=false;
    if(d->content->count()){
        if(auto *old=qobject_cast<QScrollArea*>(d->content->itemAt(0)->widget())){scrollY=old->verticalScrollBar()->value();scrollX=old->horizontalScrollBar()->value();}
        if(auto *old=qobject_cast<QTableView*>(d->content->itemAt(0)->widget())){
            scrollY=old->verticalScrollBar()->value();scrollX=old->horizontalScrollBar()->value();tableFocus=old->hasFocus();
            if(old->currentIndex().isValid()){selectedId=old->currentIndex().data(Qt::UserRole).toString();if(auto *model=dynamic_cast<ObjectTable*>(old->model()))selectedField=model->fields.value(old->currentIndex().column());}
        }
        if(auto *card=dynamic_cast<NativeCard*>(QApplication::focusWidget()))focusedCard=s(card->object,"id");
    }
    QList<QWidget*> oldWidgets;
    while(auto *item=d->content->takeAt(0)) { if(item->widget()) oldWidgets.append(item->widget()); delete item; }
    auto cleanupOld = [&oldWidgets]() {
        for (auto *w : oldWidgets) { w->deleteLater(); }
        oldWidgets.clear();
    };
    QStringList visible;
    for(auto v:d->spec.value("visible_properties").toArray()) visible << v.toString();
    if(!visible.contains("title")) visible.prepend("title");
    const QString mode=d->spec.value("presentation").toString("gallery");
    if(d->result.isEmpty()) {
        QJsonObject sourceSpec=d->spec;sourceSpec.remove("search");sourceSpec.remove("filter");sourceSpec.remove("limit");
        const bool filtered=!LumenQuery::evaluate(d->objects,d->types,sourceSpec).isEmpty();
        // Hosts can scope the empty message per view (dashboard sections):
        // "empty_title"/"empty_description" only apply when the section's own
        // base set is empty, never when a user search/filter emptied it.
        const QString customTitle=filtered?QString():d->spec.value("empty_title").toString();
        const QString customDescription=filtered?QString():d->spec.value("empty_description").toString();
        auto *empty=new QWidget;empty->setObjectName("collectionEmptyState");auto *emptyLayout=new QVBoxLayout(empty);emptyLayout->setSpacing(12);emptyLayout->addStretch(2);
        if(!filtered)emptyLayout->addWidget(new EmptyIllustration(d->dark),0,Qt::AlignHCenter);
        auto *title=new QLabel(!customTitle.isEmpty()?customTitle:(filtered?"No hay resultados":"Aquí no hay nada (todavía)."));title->setObjectName("viewEmpty");title->setAlignment(Qt::AlignCenter);emptyLayout->addWidget(title);
        auto *description=new QLabel(!customDescription.isEmpty()?customDescription:(filtered?"Prueba a cambiar la búsqueda o los filtros.":"Puedes cambiar esto creando un nuevo objeto."));description->setObjectName("viewEmptyDescription");description->setAlignment(Qt::AlignCenter);description->setWordWrap(true);emptyLayout->addWidget(description);
        emptyLayout->addSpacing(15);auto *actions=new QHBoxLayout;actions->setSpacing(10);actions->addStretch();
        if(filtered){
            auto *clear=new QToolButton;clear->setObjectName("clearViewFilters");clear->setText("Limpiar filtros");clear->setIcon(LumenIcons::icon("x"));clear->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);actions->addWidget(clear);
            connect(clear,&QToolButton::clicked,this,[this]{d->spec.remove("search");d->spec.remove("filter");d->spec.remove("limit");rebuild();});
        }else{
            auto *import=new QToolButton;import->setObjectName("emptyImport");import->setText("Importar archivo(s)");import->setIcon(LumenIcons::icon("file-upload"));import->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);actions->addWidget(import);
            connect(import,&QToolButton::clicked,this,[this]{if(onImport)onImport();});
            auto *create=new QToolButton;create->setObjectName("emptyCreate");create->setText("Nuevo");create->setIcon(LumenIcons::icon("plus",d->dark?QColor("#454349"):QColor("#f3f1f5")));create->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);actions->addWidget(create);
            connect(create,&QToolButton::clicked,this,[this]{if(onCreate)onCreate(d->spec.value("type_id").toString());});
        }
        actions->addStretch();emptyLayout->addLayout(actions);emptyLayout->addStretch(3);d->content->addWidget(empty,1);cleanupOld();d->rebuilding=false;return;
    }
    if(mode=="table"&&!d->overview) {
        auto *table=new QTableView; table->setObjectName("objectsTable");
        auto *model=new ObjectTable; model->setParent(table); model->objects=d->result; model->all=d->objects; model->types=d->types;
        for(const auto &key:d->columnOrder) if(visible.contains(key)) model->fields<<key;
        for(const auto &key:visible) if(!model->fields.contains(key)) model->fields<<key;
        // D7 (view calculations): a footer row per calculation, under the
        // calculated property's column and aligned with it (same model, so
        // resizes/moves keep the value under its column). Calculations whose
        // property has no visible column render nothing here.
        for(auto cv:d->spec.value("calculations").toArray()){
            const auto calculation=cv.toObject();
            const int column=model->fields.indexOf(calculation.value("property").toString());
            if(column<0) continue;
            const auto result=LumenCalculations::compute(calculation,d->result,d->objects,d->types);
            if(!result.valid) continue;
            QStringList texts,summaries;
            for(int c=0;c<model->fields.size();++c){texts<<QString();summaries<<QString();}
            texts[column]=result.text;summaries[column]=result.summary;
            model->footerRows<<texts;model->footerSummaries<<summaries;
        }
        model->edited=[this](QString id,QString field,QString value){return patchField(id,field,value,false);};
        table->setModel(model);table->setItemDelegate(new TypedCellDelegate(table)); table->setAlternatingRowColors(false); table->setShowGrid(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::EditKeyPressed|QAbstractItemView::SelectedClicked|QAbstractItemView::DoubleClicked);
        table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(42);
        auto *header=table->horizontalHeader(); header->setSectionsMovable(true); header->setSectionsClickable(true); header->setStretchLastSection(false);
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        for(int i=0;i<model->fields.size();++i) table->setColumnWidth(i,d->widths.value(model->fields[i],model->fields[i]=="title" ? 290 : 170));
        connect(header,&QHeaderView::sectionResized,table,[this,model](int col,int,int size){d->widths[model->fields.value(col)]=size;});
        connect(header,&QHeaderView::sectionMoved,table,[this,header,model]{d->columnOrder.clear();QJsonArray columns;for(int i=0;i<header->count();++i){d->columnOrder<<model->fields.value(header->logicalIndex(i));columns.append(model->fields.value(header->logicalIndex(i)));}d->spec["visible_properties"]=columns;});
        connect(header,&QHeaderView::customContextMenuRequested,table,[this]{editProperties();});
        connect(header,&QHeaderView::sectionClicked,table,[this,model](int column){
            const QString field=model->fields.value(column);
            const auto previous=d->spec.value("sort").toArray(); const auto old=previous.isEmpty()?QJsonObject{}:previous.first().toObject();
            d->spec["sort"]=QJsonArray{QJsonObject{{"field",field},{"direction",old.value("field").toString()==field&&old.value("direction").toString()=="asc"?"desc":"asc"}}};
            rebuild();
        });
        auto *open=new QAction(table);open->setShortcut(QKeySequence(Qt::CTRL|Qt::Key_Return));open->setShortcutContext(Qt::WidgetWithChildrenShortcut);table->addAction(open);
        connect(open,&QAction::triggered,table,[this,table,model]{if(table->currentIndex().isValid()&&table->currentIndex().row()<model->objects.size()&&onOpen)onOpen(s(model->objects.at(table->currentIndex().row()).toObject(),"id"));});
        if(onObjectMenu){
            table->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(table,&QTableView::customContextMenuRequested,table,[this,table,model](const QPoint &pos){
                const auto index=table->indexAt(pos);
                // Footer rows are summaries: the object menu never opens on them.
                if(index.isValid()&&index.row()<model->objects.size()&&onObjectMenu)onObjectMenu(s(model->objects.at(index.row()).toObject(),"id"),table->viewport()->mapToGlobal(pos));
            });
        }
        table->setToolTip("Doble clic o F2 para editar · Ctrl+Enter para abrir · Arrastra las cabeceras para reordenar");
        d->content->addWidget(table,1);
        if(!selectedId.isEmpty())for(int i=0;i<model->objects.size();++i)if(s(model->objects[i].toObject(),"id")==selectedId){table->setCurrentIndex(model->index(i,qMax(0,model->fields.indexOf(selectedField))));break;}
        QTimer::singleShot(0,table,[table,scrollY,scrollX,tableFocus]{table->verticalScrollBar()->setValue(scrollY);table->horizontalScrollBar()->setValue(scrollX);if(tableFocus)table->setFocus();});
        cleanupOld();d->rebuilding=false;return;
    }
    auto *scroll=new ResizeArea; scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->viewport()->setObjectName("collectionViewport");
    scroll->setHorizontalScrollBarPolicy(mode=="kanban"?Qt::ScrollBarAsNeeded:Qt::ScrollBarAlwaysOff);
    auto *canvas=new QWidget;canvas->setObjectName("collectionCanvas");canvas->setAutoFillBackground(false);
    auto *outer=new QVBoxLayout(canvas);outer->setContentsMargins(1,2,1,12);outer->setSpacing(16);
    scroll->setWidget(canvas);d->content->addWidget(scroll,1);
    QTimer::singleShot(0,scroll,[scroll,scrollY,scrollX]{scroll->verticalScrollBar()->setValue(scrollY);scroll->horizontalScrollBar()->setValue(scrollX);});
    auto makeCard=[this,visible,mode,focusedCard](const QJsonObject &object,QWidget *parent)->NativeCard*{
        const auto type=typeOf(d->types,s(object,"type_id"));
        auto *card=new NativeCard(object,type,d->objects,visible,d->dark,d->reduced,parent);
        card->compact=!d->overview&&mode=="list";card->dragEnabled=!d->overview&&mode!="embed";card->gallery=d->overview||mode=="gallery";
        if(d->overview)card->setFixedHeight(384);
        else if(card->compact)card->setFixedHeight(74);
        else if(mode=="embed")card->setFixedHeight(200);
        else if(mode=="wall")card->setFixedHeight(165+qMin(130,LumenQuery::plainText(object).size()/2)+qMin(66,card->propertyLines().size()*22));
        else card->setFixedHeight(mode=="gallery"?384:302);
        connect(card,&QAbstractButton::clicked,this,[this,id=s(object,"id")]{if(onOpen)onOpen(id);});
        if(onObjectMenu){
            card->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(card,&QWidget::customContextMenuRequested,this,[this,card,id=s(object,"id")](const QPoint &pos){
                if(onObjectMenu)onObjectMenu(id,card->mapToGlobal(pos));
            });
        }
        if(!focusedCard.isEmpty()&&s(object,"id")==focusedCard)QTimer::singleShot(0,card,[card]{card->setFocus();});
        QString asset=object.value("properties").toObject().value("image_asset_id").toString();
        if(asset.isEmpty()&&(s(object,"type_id")=="image"||s(object,"type_id")=="photo")) {
            const auto props=object.value("properties").toObject();asset=props.value("asset_id").toString();
            if(asset.isEmpty())for(auto av:props.value("assets").toArray()){auto a=av.toObject();if(a.value("mime").toString().startsWith("image/")){asset=s(a,"id");break;}}
        }
        card->hasCover=!asset.isEmpty();card->setProperty("coverHeight",card->hasCover?card->mediaHeight():0);
        if(mode=="wall"&&!d->overview&&!asset.isEmpty())card->setFixedHeight(card->height()+128);
        auto loadAsset=[this,card](const QString &asset,bool isFavicon){
        if(asset.isEmpty())return;
        if(!asset.isEmpty()) {
            if(auto *image=d->images.object(asset)){if(isFavicon)card->favicon=*image;else card->image=*image;}
            else if(!d->failed.contains(asset)) {
                auto &waiting=d->waiting[asset];for(int i=waiting.size()-1;i>=0;--i)if(waiting[i].card.isNull())waiting.removeAt(i);waiting<<Impl::AssetTarget{QPointer<NativeCard>(card),isFavicon};
                if(!d->requested.contains(asset)&&onAssetRequest) {
                    d->requested.insert(asset);QPointer<LumenViews> guard(this);
                    onAssetRequest(asset,[guard,asset](QByteArray bytes,QString error){
                        if(!guard)return;
                        auto *self=guard.data();QPixmap image;
                        if(error.isEmpty()&&bytes.size()<=20*1024*1024&&!bytes.startsWith("%PDF"))image.loadFromData(bytes);
                        if(!image.isNull()) {
                            if(image.width()>2048||image.height()>2048)image=image.scaled(2048,2048,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                            const int cost=qMax(1,static_cast<int>(image.width()*static_cast<qint64>(image.height())*4/1024));
                            self->d->images.insert(asset,new QPixmap(image),cost);
                            for(auto target:self->d->waiting.take(asset))if(target.card){if(target.favicon)target.card->favicon=image;else target.card->image=image;target.card->update();}
                        }else{self->d->failed.insert(asset);self->d->waiting.remove(asset);}
                        self->d->requested.remove(asset);
                    });
                }
            }
        }
        };
        loadAsset(asset,false);loadAsset(object.value("properties").toObject().value("favicon_asset_id").toString(),true);
        return card;
    };
    if(d->overview){
        QHash<QString,QJsonObject> candidates;for(auto ov:d->result)candidates.insert(s(ov.toObject(),"id"),ov.toObject());
        QJsonArray opened;for(const auto &id:d->recentIds)if(candidates.contains(id)){opened.append(candidates.value(id));if(opened.size()>=12)break;}
        QList<QJsonObject> created;for(auto ov:d->result)created<<ov.toObject();
        std::stable_sort(created.begin(),created.end(),[](const QJsonObject &a,const QJsonObject &b){const auto first=s(a,"created_at"),second=s(b,"created_at");return first==second?s(a,"id")<s(b,"id"):first>second;});
        auto addSection=[this,outer,canvas,&makeCard](const QString &name,const QString &title,const QJsonArray &items){
            auto *section=new QWidget(canvas);section->setObjectName(name);auto *sectionLayout=new QVBoxLayout(section);sectionLayout->setContentsMargins(0,0,0,0);sectionLayout->setSpacing(17);
            auto *heading=new QLabel(title);heading->setObjectName("overviewSectionTitle");sectionLayout->addWidget(heading);
            auto *strip=new QScrollArea;strip->setObjectName("overviewCardStrip");strip->viewport()->setObjectName("collectionViewport");strip->setWidgetResizable(true);strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);strip->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);strip->setFrameShape(QFrame::NoFrame);strip->setFixedHeight(404);
            auto *row=new QWidget;row->setObjectName("overviewCardRow");auto *cards=new QHBoxLayout(row);cards->setContentsMargins(0,0,0,5);cards->setSpacing(12);
            QStringList ids;for(auto ov:items){auto *card=makeCard(ov.toObject(),row);card->setFixedWidth(248);cards->addWidget(card);ids<<s(ov.toObject(),"id");}cards->addStretch();
            row->setMinimumWidth(qMax(1,items.size()*260-12));strip->setWidget(row);sectionLayout->addWidget(strip);section->setProperty("objectIds",ids);outer->addWidget(section);
        };
        if(d->showOpened&&!opened.isEmpty())addSection("overviewRecentlyOpened","Recientemente abierto",opened);
        if(d->showCreated||opened.isEmpty()){
            QJsonArray recent;const int maximum=opened.isEmpty()?4:12;for(const auto &object:created){recent.append(object);if(recent.size()>=maximum)break;}
            addSection("overviewRecentlyCreated","Recientemente creado",recent);
        }
        outer->addStretch();cleanupOld();d->rebuilding=false;return;
    }
    const QString groupField=d->spec.value("group_by").toString();
    QStringList groupOrder;QHash<QString,QJsonArray> groups;
    for(auto ov:d->result){const auto object=ov.toObject();QString key=groupField.isEmpty()?QString():LumenQuery::displayValue(LumenQuery::fieldValue(object,groupField,d->objects));if(!groups.contains(key))groupOrder<<key;groups[key].append(ov);}
    if(mode=="kanban") {
        if(!groups.contains("")){groups.insert("",{});groupOrder<<"";}
        auto *columns=new QHBoxLayout;columns->setSpacing(14);outer->addLayout(columns);outer->addStretch();
        // Known status options are useful even before a card has that value.
        for(auto tv:d->types){auto type=tv.toObject();if(!d->spec.value("type_id").toString().isEmpty()&&s(type,"id")!=d->spec.value("type_id").toString())continue;
            for(auto pv:type.value("properties").toArray()){auto def=pv.toObject();QString key=def.value("key").toString(def.value("id").toString(def.value("name").toString()));if("properties."+key!=groupField)continue;
                for(auto opt:def.value("options").toArray()){QString val=opt.isString()?opt.toString():opt.toObject().value("value").toString(opt.toObject().value("name").toString());if(!groups.contains(val)){groups.insert(val,{});groupOrder<<val;}}
            }
        }
        if(groupField=="properties.status"&&groups.size()==1)for(const QString &status:QStringList{"Por empezar","En curso","Terminado"}){groups.insert(status,{});groupOrder<<status;}
        for(const auto &key:groupOrder){
            auto *column=new DropColumn;column->value=key;column->setFixedWidth(262);column->moved=[this](QString id,QString value){applyKanbanMove(id,value);};
            auto *col=new QVBoxLayout(column);col->setContentsMargins(10,12,10,12);col->setSpacing(10);
            QString title=key.isEmpty()?"Sin valor":key;if(groupField=="type_id"&&!key.isEmpty())title=s(typeOf(d->types,key),"name");
            auto *heading=new QLabel(title+QString("  %1").arg(groups[key].size()));heading->setTextFormat(Qt::PlainText);heading->setStyleSheet("font-weight:600;padding:2px 3px 8px;");col->addWidget(heading);
            if(!calculations.isEmpty()){
                const QString text=LumenCalculations::strip(calculations,groups[key],d->objects,d->types);
                if(!text.isEmpty()){auto *line=new QLabel(text);line->setObjectName("groupCalculations");line->setTextFormat(Qt::PlainText);line->setWordWrap(true);col->addWidget(line);}
            }
            for(auto ov:groups[key]){auto *card=makeCard(ov.toObject(),column);card->setFixedHeight(230);col->addWidget(card);}col->addStretch();columns->addWidget(column);
        }columns->addStretch();
    }else{
        // A newly constructed scroll viewport has Qt's provisional 638 px width.
        // Use the stable collection width both here and during resize; otherwise
        // rebuilding can oscillate forever between two and four columns.
        const int availableWidth=qMax(1,width()-style()->pixelMetric(QStyle::PM_ScrollBarExtent)-4);
        const int columns=mode=="list"||mode=="embed"?1:qMax(1,(availableWidth+14)/244);
        if(groupField.isEmpty()&&!calculations.isEmpty()){
            const QString text=LumenCalculations::strip(calculations,d->result,d->objects,d->types);
            if(!text.isEmpty()){auto *strip=new QLabel(text);strip->setObjectName("viewCalculations");strip->setTextFormat(Qt::PlainText);strip->setWordWrap(true);outer->addWidget(strip);}
        }
        for(const auto &key:groupOrder){
            if(!groupField.isEmpty()){
                QString title=key.isEmpty()?"Sin valor":key;if(groupField=="type_id"&&!key.isEmpty())title=s(typeOf(d->types,key),"name");auto *header=new QLabel(title+QString("  ·  %1").arg(groups[key].size()));header->setStyleSheet("font-weight:600;padding-top:8px;");outer->addWidget(header);
                if(!calculations.isEmpty()){
                    const QString text=LumenCalculations::strip(calculations,groups[key],d->objects,d->types);
                    if(!text.isEmpty()){auto *line=new QLabel(text);line->setObjectName("groupCalculations");line->setTextFormat(Qt::PlainText);line->setWordWrap(true);outer->addWidget(line);}
                }
            }
            auto *rows=new QWidget;auto *rowLayout=new QHBoxLayout(rows);rowLayout->setContentsMargins(0,0,0,0);rowLayout->setSpacing(14);
            QList<QVBoxLayout*> lanes;QVector<int> laneHeights(columns,0);
            for(int i=0;i<columns;++i){auto *lane=new QVBoxLayout;lane->setSpacing(mode=="list"?6:14);rowLayout->addLayout(lane,1);lanes<<lane;}
            int n=0;
            for(auto ov:groups[key]){auto *card=makeCard(ov.toObject(),rows);int lane=n%columns;if(mode=="wall")lane=static_cast<int>(std::min_element(laneHeights.begin(),laneHeights.end())-laneHeights.begin());lanes[lane]->addWidget(card);laneHeights[lane]+=card->height()+14;++n;}
            for(auto *lane:lanes) lane->addStretch();
            outer->addWidget(rows);
        }outer->addStretch();
        QPointer<LumenViews> guard(this);QPointer<ResizeArea> area(scroll);auto scheduled=std::make_shared<bool>(false);
        scroll->onResize=[guard,area,columns,mode,scheduled]{
            if(!guard||!area||mode=="list"||mode=="embed")return;
            const int available=qMax(1,guard->width()-guard->style()->pixelMetric(QStyle::PM_ScrollBarExtent)-4);
            const int next=qMax(1,(available+14)/244);
            if(next!=columns&&!*scheduled){*scheduled=true;QTimer::singleShot(0,guard,[guard]{if(guard)guard->rebuild();});}
        };
    }
    cleanupOld();
    d->rebuilding=false;
}

bool LumenViews::patchField(const QString &id,const QString &field,const QString &value,bool remember){
    for(int i=0;i<d->objects.size();++i){auto object=d->objects[i].toObject();if(s(object,"id")!=id)continue;
        const auto edit=LumenQuery::validateEdit(object,d->types,field,value,d->objects);
        if(!edit.valid){d->message->setText(edit.error);d->message->show();return false;}
        if(remember)d->undoStack<<Impl::Move{id,field,LumenQuery::fieldValue(object,field,d->objects)};
        d->refreshScheduled=false;
        for(auto it=edit.patch.begin();it!=edit.patch.end();++it) object[it.key()]=it.value();
        d->objects[i]=object;
        d->message->hide();if(onPatch)onPatch(id,edit.patch);
        QTimer::singleShot(0,this,[this]{rebuild();});return true;
    }return false;
}
bool LumenViews::applyKanbanMove(const QString &id,const QString &value){
    const QString field=d->spec.value("group_by").toString();
    if(!field.startsWith("properties.")){d->message->setText("Para mover tarjetas, agrupa Kanban por una propiedad editable.");d->message->show();return false;}
    for(auto ov:d->objects)if(s(ov.toObject(),"id")==id&&LumenQuery::displayValue(LumenQuery::fieldValue(ov.toObject(),field,d->objects))==value)return true;
    return patchField(id,field,value,true);
}
void LumenViews::undoLastMove(){
    if(d->undoStack.isEmpty()) return;
    const auto move=d->undoStack.takeLast();
    // Restore the original typed value exactly, and retain every other property.
    for(int i=0;i<d->objects.size();++i){auto object=d->objects[i].toObject();if(s(object,"id")!=move.id)continue;
        auto properties=object.value("properties").toObject();const QString key=move.field.mid(11);
        if(move.old.isUndefined()||move.old.isNull())properties.remove(key);else properties[key]=move.old;
        object["properties"]=properties;d->objects[i]=object;if(onPatch)onPatch(move.id,{{"properties",properties}});break;
    }rebuild();
}

void LumenViews::editFilters(){
    QDialog dialog(this,Qt::Popup);dialog.setObjectName("viewFilterPopover");dialog.setWindowTitle("Filtrar esta vista");dialog.resize(720,340);
    placePopover(dialog,d->filters);
    auto *layout=new QVBoxLayout(&dialog);auto *intro=new QLabel("Mostrar objetos que cumplan estas condiciones");intro->setWordWrap(true);layout->addWidget(intro);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto *filters=new FilterGroup(availableProperties(d->objects,d->types,d->spec.value("type_id").toString()),d->spec.value("filter").toObject());scroll->setWidget(filters);layout->addWidget(scroll,1);
    auto *more=new QToolButton;more->setText("Más opciones");more->setCheckable(true);more->setChecked(d->spec.value("limit").toInt()>0);layout->addWidget(more,0,Qt::AlignLeft);
    auto *limitOptions=new QWidget;auto *limitRow=new QHBoxLayout(limitOptions);limitRow->setContentsMargins(0,0,0,0);limitRow->addWidget(new QLabel("Máximo de objetos"));auto *limit=new QSpinBox;limit->setRange(0,10000);limit->setSpecialValueText("Sin límite");limit->setValue(d->spec.value("limit").toInt());limitRow->addWidget(limit);limitRow->addStretch();layout->addWidget(limitOptions);limitOptions->setVisible(more->isChecked());connect(more,&QToolButton::toggled,limitOptions,&QWidget::setVisible);
    auto *error=new QLabel;error->setWordWrap(true);error->hide();layout->addWidget(error);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel|QDialogButtonBox::Reset);layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Reset),&QPushButton::clicked,&dialog,[this,&dialog]{d->spec.remove("filter");d->spec.remove("limit");dialog.accept();rebuild();});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,[this,filters,limit,error,&dialog]{
        QString problem;
        std::function<QJsonObject(QJsonObject)> normalize=[this,&normalize,&problem](QJsonObject rule)->QJsonObject{
            if(rule.contains("rules")){QJsonArray normalized;for(auto v:rule.value("rules").toArray())normalized.append(normalize(v.toObject()));rule["rules"]=normalized;return rule;}
            const QString field=rule.value("field").toString(),op=rule.value("op").toString();
            if((field=="links"||field=="backlinks")&&(op=="equals"||op=="not_equals"||op=="contains"||op=="not_contains")){
                const auto value=rule.value("value").toString();QString found;int count=0;
                for(auto ov:d->objects){auto o=ov.toObject();if(s(o,"id")==value||s(o,"title")==value){found=s(o,"id");++count;}}
                if(count==1)rule["value"]=found;else problem=count>1?"Hay varios objetos con ese nombre. Usa su ID.":"La relación elegida no existe. Elige un objeto actual.";
            }return rule;
        };
        const auto filter=normalize(filters->value());if(!problem.isEmpty()){error->setText(problem);error->show();return;}
        d->spec["filter"]=filter;d->spec["limit"]=limit->value();dialog.accept();rebuild();
    });dialog.exec();d->filters->setChecked(!d->spec.value("filter").toObject().value("rules").toArray().isEmpty());
}
void LumenViews::editProperties(){
    QDialog dialog(this,Qt::Popup);dialog.setObjectName("viewPropertiesPopover");dialog.setWindowTitle("Propiedades visibles");dialog.resize(360,440);placePopover(dialog,d->propertiesButton);auto *layout=new QVBoxLayout(&dialog);
    auto *hint=new QLabel("Arrastra para ordenar. Nombre permanece visible.");hint->setWordWrap(true);layout->addWidget(hint);
    auto *list=new QListWidget;list->setDragDropMode(QAbstractItemView::InternalMove);layout->addWidget(list,1);
    QStringList fields;for(auto v:d->spec.value("visible_properties").toArray())fields<<v.toString();
    for(const auto &key:availableProperties(d->objects,d->types,d->spec.value("type_id").toString()))if(!fields.contains(key))fields<<key;
    for(const auto &field:fields){auto *item=new QListWidgetItem(propertyName(field),list);item->setData(Qt::UserRole,field);item->setFlags(item->flags()|Qt::ItemIsUserCheckable|Qt::ItemIsDragEnabled);item->setCheckState(d->spec.value("visible_properties").toArray().contains(field)||field=="title"?Qt::Checked:Qt::Unchecked);}
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted) return;
    QJsonArray visible;d->columnOrder.clear();for(int i=0;i<list->count();++i){auto *item=list->item(i);if(item->checkState()==Qt::Checked||item->data(Qt::UserRole).toString()=="title"){visible.append(item->data(Qt::UserRole).toString());d->columnOrder<<item->data(Qt::UserRole).toString();}}
    d->spec["visible_properties"]=visible;rebuild();
}
void LumenViews::editCalculation(int index){
    QJsonArray calculations=d->spec.value("calculations").toArray();
    const bool editing=index>=0&&index<calculations.size();
    const QJsonObject initial=editing?calculations.at(index).toObject():QJsonObject{};
    QDialog dialog(this);
    dialog.setObjectName("calculationDialog");
    dialog.setWindowTitle(editing?"Edit calculation":"Add calculation");dialog.resize(400,190);
    auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(14,14,14,14);layout->setSpacing(10);
    auto *intro=new QLabel("Summarize one property across the objects in this view.");intro->setWordWrap(true);layout->addWidget(intro);
    auto *propertyRow=new QHBoxLayout;auto *propertyLabel=new QLabel("Property");propertyRow->addWidget(propertyLabel);
    auto *property=new QComboBox;property->setObjectName("calculationProperty");property->setMinimumWidth(170);
    const QStringList fields=availableProperties(d->objects,d->types,d->spec.value("type_id").toString());
    for(const auto &field:fields)property->addItem(propertyName(field),field);
    property->setCurrentIndex(qMax(0,property->findData(initial.value("property").toString(fields.value(0)))));
    propertyRow->addWidget(property,1);layout->addLayout(propertyRow);
    auto *opRow=new QHBoxLayout;auto *opLabel=new QLabel("Calculation");opRow->addWidget(opLabel);
    auto *op=new QComboBox;op->setObjectName("calculationOp");op->setMinimumWidth(170);opRow->addWidget(op,1);layout->addLayout(opRow);
    const QString wantedOp=initial.value("op").toString();
    auto refreshOps=[this,op,property,wantedOp]{
        op->clear();
        const QString kind=LumenCalculations::kindOf(d->types,d->objects,property->currentData().toString());
        for(const auto &pair:LumenCalculations::opsForKind(kind))op->addItem(pair.second,pair.first);
        const int existing=op->findData(wantedOp);
        op->setCurrentIndex(existing>=0?existing:0);
    };
    connect(property,&QComboBox::currentIndexChanged,this,[refreshOps]{refreshOps();});refreshOps();
    // Button-level connects (the alias editor pattern): the button click
    // drives QDialog::accept/reject directly.
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Ok),&QPushButton::clicked,&dialog,&QDialog::accept);
    connect(buttons->button(QDialogButtonBox::Cancel),&QPushButton::clicked,&dialog,&QDialog::reject);
    bool removed=false;
    if(editing){
        auto *remove=buttons->addButton("Remove",QDialogButtonBox::DestructiveRole);remove->setObjectName("calculationRemove");
        connect(remove,&QPushButton::clicked,&dialog,[&dialog,&removed]{removed=true;dialog.accept();});
    }
    const int outcome = dialog.exec();
    if (outcome != QDialog::Accepted) return;
    if (removed){
        // A removed last calculation drops the key entirely: saved specs
        // never carry an empty "calculations" array.
        calculations.removeAt(index);
        if(calculations.isEmpty())d->spec.remove("calculations");else d->spec["calculations"]=calculations;
        rebuild();return;
    }
    const QJsonObject calculation{{"property",property->currentData().toString()},{"op",op->currentData().toString()}};
    bool duplicate=false;
    for(auto v:calculations){const auto other=v.toObject();if(other.value("property")==calculation.value("property")&&other.value("op")==calculation.value("op"))duplicate=true;}
    if(duplicate) return;
    if(editing)calculations[index]=calculation;else calculations.append(calculation);
    d->spec["calculations"]=calculations;rebuild();
}
void LumenViews::saveView(){
    bool ok=false;const QString title=QInputDialog::getText(this,"Guardar consulta","Nombre de esta vista",QLineEdit::Normal,"Mi consulta",&ok).trimmed();
    if(ok&&!title.isEmpty()&&onSaveView){onSaveView(title,viewSpec());d->message->setText("Consulta enviada para guardar. Su estado de sincronización aparece en el objeto.");d->message->show();}
}
