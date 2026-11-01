#pragma once
#include <QCalendarWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QRect>
#include <QSet>
#include <functional>
#include <memory>

class LumenMiniCalendar final : public QCalendarWidget {
  public:
    explicit LumenMiniCalendar(QWidget *parent = nullptr);
    void setWorkspaceObjects(QJsonArray objects);
    void setTheme(bool dark);
    std::function<void(QDate)> onDateRequested;
  protected:
    void paintCell(QPainter *painter,const QRect &rect,QDate date) const override;
    bool eventFilter(QObject *watched,QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
  private:
    QJsonArray objects;
    QHash<QDate,int> objectCounts;
    QSet<QDate> noteDates;
    mutable QHash<QDate,QRect> cells;
    QDate hovered;
    bool dark=false;
    void requestSelected();
};

class LumenCalendarOverview final : public QWidget {
  public:
    explicit LumenCalendarOverview(QWidget *parent = nullptr);
    ~LumenCalendarOverview() override;
    void setData(QJsonArray objects,QJsonArray types);
    void setDate(QDate date);
    QDate date() const;
    void setMode(QString mode);
    void setTheme(bool dark,bool reducedMotion=false);
    std::function<void(QDate)> onOpenDate;
    std::function<void(QString)> onOpenObject;
  private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void rebuild();
};
