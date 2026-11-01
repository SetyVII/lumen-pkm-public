#pragma once
#include <QtWidgets>
#include <functional>

class LumenGraph : public QWidget {
  public:
    explicit LumenGraph(QWidget *parent = nullptr);
    void setData(const QJsonArray &objects, const QJsonArray &types);
    void setTheme(bool dark, bool reducedMotion = false);
    void setSearch(const QString &search);
    void setTypeFilter(const QString &typeId);
    void fitToContents();
    void zoomIn();
    void zoomOut();
    bool exportImage(const QString &path);
    int nodeCount() const;
    int edgeCount() const;
    QStringList visibleObjectIds() const;
    QString selectedObjectId() const;
    void focusObject(const QString &id);
    std::function<void(QString)> onOpen;
  protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
  private:
    struct Node {
        QString id, title, type, summary;
        QPointF position, velocity;
        QColor color;
        int degree = 0;
        bool visible = true;
    };
    struct Edge { int source, target; QString label; };
    QList<Node> nodes_;
    QList<Edge> edges_;
    QHash<QString, int> index_;
    QHash<QString, QPointF> previousPositions_;
    QJsonArray types_;
    QString search_, typeFilter_;
    bool dark_ = false, reducedMotion_ = false, panning_ = false, moved_ = false, fitPending_ = true;
    int hovered_ = -1, selected_ = -1, pressed_ = -1, dragging_ = -1, iterations_ = 0;
    QPointF offset_, pressPoint_, lastPoint_;
    double zoom_ = 1.;
    QTimer layoutTimer_;
    QWidget *controls_ = nullptr;
    QLabel *countLabel_ = nullptr, *zoomLabel_ = nullptr;
    void stepLayout();
    void updateFilters();
    void updateControls();
    void zoomAt(double factor, const QPointF &point);
    QPointF toScreen(const QPointF &point) const;
    QPointF toWorld(const QPointF &point) const;
    int hitNode(const QPointF &point) const;
    double radius(int index) const;
    void paintGraph(QPainter &painter, const QRect &bounds, bool exporting = false) const;
    QString typeName(const QString &id) const;
    void selectDirection(const QPointF &direction);
};
