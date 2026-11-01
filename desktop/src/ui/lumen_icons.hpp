#pragma once
#include <QtWidgets>
#include <QSvgRenderer>

// Official packaged SVGs. Badges are ordinary UI surfaces, never icon glyphs.
namespace LumenIcons {
inline QIcon fixedIcon(const QPixmap &pixmap) {
    QIcon result;
    for (auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled})
        for (auto state : {QIcon::Off, QIcon::On}) result.addPixmap(pixmap, mode, state);
    return result;
}
inline QString canonical(QString name) {
    static const QHash<QString,QString> names{
        {"view-calendar-day","calendar"},{"view-list-icons","layout-grid"},{"view-list-details","table"},{"view-list-tree","network"},{"go-previous","chevron-left"},{"go-next","chevron-right"},{"edit-find","search"},{"document-new","plus"},{"list-add","plus"},{"configure","settings"},{"insert-link","link"},{"document-open","file-upload"},{"text-x-generic","file-text"},{"accessories-dictionary","book"},{"avatar-default","user"},{"dialog-information","bulb"},{"web-browser","world"},{"image-x-generic","photo"},{"application-pdf","file-type-pdf"},{"network-connect","link"},{"task","circle-check"},{"task-due","circle-check"},{"format-quote-close","quote"},{"quote","quote"},{"atomic","box"},{"podcast","microphone"},{"tweet","brand-twitter"},{"audio","music"},{"file","file"},{"page","file-text"},{"tag","tag"},{"columns","layout-columns"},{"divider","minus"},{"icon","mood-smile"},{"group","square"}
    };
    name=names.value(name,name);if(!QFile::exists(":/icons/"+name+".svg"))name="file-text";return name;
}
inline QIcon icon(QString name,QColor color=QColor("#a5a4ac"),int size=24) {
    QFile file(":/icons/"+canonical(name)+".svg");if(!file.open(QIODevice::ReadOnly))return {};
    QByteArray svg=file.readAll();svg.replace("currentColor",color.name().toUtf8());svg.replace("#96969f",color.name().toUtf8());
    QSvgRenderer renderer(svg);QPixmap result(size*2,size*2);result.fill(Qt::transparent);result.setDevicePixelRatio(2);
    QPainter painter(&result);renderer.render(&painter,QRectF(0,0,size,size));return fixedIcon(result);
}
inline QPixmap badge(QString name,QColor tint,int size=28,bool dark=true) {
    if(!tint.isValid())tint=QColor("#9584b3");
    QColor background=dark?QColor("#1b1b1f"):QColor("#fcfcfc");const qreal blend=dark?.30:.16;
    background.setRgbF(background.redF()*(1-blend)+tint.redF()*blend,background.greenF()*(1-blend)+tint.greenF()*blend,background.blueF()*(1-blend)+tint.blueF()*blend);
    QColor foreground=dark?tint.lighter(145):tint.darker(125);
    QPixmap result(size*2,size*2);result.fill(Qt::transparent);result.setDevicePixelRatio(2);QPainter painter(&result);painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);painter.setBrush(background);painter.drawRoundedRect(QRectF(0,0,size,size),qMin(7.,size/4.),qMin(7.,size/4.));
    const int inner=qMax(12,qRound(size*.62));icon(name,foreground,inner).paint(&painter,QRect((size-inner)/2,(size-inner)/2,inner,inner));return result;
}
inline QIcon typeIcon(const QJsonObject &type,int size=28,bool dark=true) {
    QString name=type.value("icon").toString("file-text");
    const QString tid=type.value("id").toString();
    if(tid=="web")name="world";
    else if(tid=="task")name="circle-check";
    else if(tid=="quote")name="quote";
    else if(tid=="atomic")name="box";
    else if(tid=="podcast")name="microphone";
    else if(tid=="tweet")name="brand-twitter";
    else if(tid=="audio")name="music";
    else if(tid=="file")name="file";
    else if(tid=="page")name="file-text";
    else if(tid=="tag")name="tag";
    return fixedIcon(badge(name,QColor(type.value("color").toString("#9584b3")),size,dark));
}
}
