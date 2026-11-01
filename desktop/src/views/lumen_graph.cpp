#include "lumen_graph.hpp"
#include <cmath>
#include <limits>
#include <QCryptographicHash>

namespace {
QString graphString(const QJsonObject &o, const char *key) { return o.value(key).toString(); }
QToolButton *graphTool(QWidget *parent, const QString &title, const QString &iconName) {
    auto *button = new QToolButton(parent);
    const auto path = ":/icons/" + iconName + ".svg";
    if (QFile::exists(path)) button->setIcon(QIcon(path));
    button->setText(title);
    button->setToolTip(title);
    button->setAccessibleName(title);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setMinimumSize(32, 32);
    button->setToolButtonStyle(button->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
    return button;
}
quint32 seedFor(const QString &id) {
    const auto hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256);
    return (quint32(quint8(hash[0])) << 24) | (quint32(quint8(hash[1])) << 16) |
           (quint32(quint8(hash[2])) << 8) | quint32(quint8(hash[3]));
}
}
LumenGraph::LumenGraph(QWidget *parent) : QWidget(parent) {
    setObjectName("objectGraph");
    setAccessibleName("Grafo de objetos y relaciones. Flechas para seleccionar, Enter para abrir, rueda para ampliar y arrastre para desplazar.");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(260, 280);
    controls_ = new QWidget(this);
    controls_->setObjectName("graphControls");
    auto *row = new QHBoxLayout(controls_);
    row->setContentsMargins(9, 4, 9, 4);
    row->setSpacing(3);
    countLabel_ = new QLabel(controls_);
    countLabel_->setTextFormat(Qt::PlainText);
    row->addWidget(countLabel_, 1);
    auto *out = graphTool(controls_, "Alejar · −", "zoom-out");
    auto *in = graphTool(controls_, "Acercar · +", "zoom-in");
    auto *fit = graphTool(controls_, "Ajustar todos los objetos · Inicio", "arrows-maximize");
    auto *save = graphTool(controls_, "Exportar imagen PNG", "photo");
    zoomLabel_ = new QLabel(controls_);
    zoomLabel_->setMinimumWidth(42);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    row->addWidget(out); row->addWidget(zoomLabel_); row->addWidget(in); row->addWidget(fit); row->addWidget(save);
    connect(out, &QToolButton::clicked, this, [this] { zoomOut(); });
    connect(in, &QToolButton::clicked, this, [this] { zoomIn(); });
    connect(fit, &QToolButton::clicked, this, [this] { fitToContents(); });
    connect(save, &QToolButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Exportar grafo como imagen", "lumen-conexiones.png", "Imagen PNG (*.png)");
        if (path.isEmpty()) return;
        if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
        if (!exportImage(path)) QMessageBox::warning(this, "No se pudo exportar", "Comprueba el permiso de escritura en la carpeta elegida.");
    });
    layoutTimer_.setInterval(16);
    connect(&layoutTimer_, &QTimer::timeout, this, [this] {
        if (!isVisible()) { layoutTimer_.stop(); return; }
        QElapsedTimer budget;
        budget.start();
        int steps = 0;
        do { stepLayout(); ++steps; } while (steps < (reducedMotion_ ? 15 : 2) && budget.elapsed() < 8 && iterations_ < 180);
        if (fitPending_) fitToContents();
        if (iterations_ >= 180) { layoutTimer_.stop(); fitPending_ = false; }
        update();
    });
    setTheme(false);
}
void LumenGraph::setTheme(bool dark, bool reducedMotion) {
    dark_ = dark; reducedMotion_ = reducedMotion;
    const auto foreground = dark ? "#e4e3e2" : "#303034";
    const auto surface = dark ? "#1c1c20" : "#fcfcfc";
    const auto border = dark ? "#29292e" : "#e3e3e7";
    const auto hover = dark ? "#242428" : "#f4f4f5";
    controls_->setStyleSheet(QString("QWidget#graphControls{background:%1;border:1px solid %2;border-radius:8px;}QLabel{color:%3;background:transparent;border:0;}QToolButton{color:%3;background:transparent;border:1px solid transparent;border-radius:4px;padding:3px;}QToolButton:hover{background:%4;}QToolButton:focus{border:1px solid #b499d4;}").arg(surface, border, foreground, hover));
    updateControls(); update();
}
QString LumenGraph::typeName(const QString &id) const {
    for (const auto &entry : types_) {
        const auto type = entry.toObject();
        if (graphString(type, "id") == id) {
            const auto name = graphString(type, "name");
            return name.isEmpty() ? graphString(type, "title") : name;
        }
    }
    return id;
}
void LumenGraph::setData(const QJsonArray &objects, const QJsonArray &types) {
    for (const auto &node : nodes_) previousPositions_.insert(node.id, node.position);
    const auto selectedId = selectedObjectId();
    nodes_.clear(); edges_.clear(); index_.clear();
    selected_ = hovered_ = -1; types_ = types;
    const QList<QColor> palette{QColor("#8d78ad"), QColor("#689c91"), QColor("#b89462"), QColor("#7194b2"), QColor("#ad7f89"), QColor("#87a369"), QColor("#888ab7")};
    const double spread = qMax(130., std::sqrt(double(objects.size())) * 40.);
    for (const auto &entry : objects) {
        const auto object = entry.toObject();
        const auto id = graphString(object, "id");
        if (id.isEmpty() || index_.contains(id)) continue;
        Node node;
        node.id = id; node.title = graphString(object, "title"); node.type = graphString(object, "type_id");
        node.summary = object.value("properties").toObject().value("summary").toString();
        if (node.summary.isEmpty()) node.summary = object.value("properties").toObject().value("description").toString();
        if (node.summary.isEmpty()) node.summary = graphString(object, "content").left(220);
        const auto seed = seedFor(id);
        const double x = (double(seed & 65535) / 65535. - .5) * spread * 2;
        const double y = (double(seed >> 16) / 65535. - .5) * spread * 1.5;
        node.position = previousPositions_.value(id, QPointF(x, y));
        node.color = palette.at(seedFor(node.type) % palette.size());
        for (const auto &type : types_) if (graphString(type.toObject(), "id") == node.type) {
            const QColor custom(graphString(type.toObject(), "color"));
            if (custom.isValid()) node.color = custom;
        }
        index_.insert(id, nodes_.size()); nodes_.append(node);
    }
    QHash<QString, int> seenEdges;
    auto addEdge = [this, &seenEdges](const QString &source, const QString &target, const QString &label) {
        if (source == target || !index_.contains(source) || !index_.contains(target)) return;
        const auto key = source + '\n' + target;
        if (seenEdges.contains(key)) {
            auto &existing = edges_[seenEdges.value(key)];
            if (label != "Relación" && !existing.label.split(" · ").contains(label)) {
                if (existing.label == "Relación") existing.label = label;
                else existing.label += " · " + label;
            }
            return;
        }
        seenEdges.insert(key, edges_.size());
        const int from = index_.value(source), to = index_.value(target);
        edges_.append(Edge{from, to, label}); ++nodes_[from].degree; ++nodes_[to].degree;
    };
    for (const auto &entry : objects) {
        const auto object = entry.toObject();
        const auto id = graphString(object, "id");
        for (const auto &link : object.value("links").toArray()) addEdge(id, link.toString(), "Mención");
        const auto properties = object.value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            if (it.key() == "blocks" || it.key() == "assets" || it.key() == "rich_text") continue;
            if (it.value().isString()) addEdge(id, it.value().toString(), it.key());
            else if (it.value().isArray()) for (const auto &part : it.value().toArray()) {
                if (part.isString()) addEdge(id, part.toString(), it.key());
                else if (part.isObject()) addEdge(id, graphString(part.toObject(), "id"), it.key());
            }
            else if (it.value().isObject()) addEdge(id, graphString(it.value().toObject(), "id"), it.key());
        }
        for (const auto &link : object.value("property_links").toArray()) addEdge(id, link.toString(), "Relación");
    }
    if (index_.contains(selectedId)) selected_ = index_.value(selectedId);
    iterations_ = 0; fitPending_ = true;
    updateFilters();
    // Settle initial positions before showing, then animate only the remaining relaxation.
    QElapsedTimer budget; budget.start();
    for (int i = 0; i < (reducedMotion_ ? 100 : 20) && budget.elapsed() < 25; ++i) stepLayout();
    fitToContents();
    if (!reducedMotion_ && iterations_ < 180 && isVisible()) layoutTimer_.start();
    update();
}
void LumenGraph::stepLayout() {
    if (nodes_.isEmpty()) { iterations_ = 180; return; }
    QVector<QPointF> forces(nodes_.size());
    // Local spatial cells bound the repulsion work for large collections.
    constexpr double cellSize = 150.;
    QHash<QPair<int, int>, QList<int>> cells;
    for (int i = 0; i < nodes_.size(); ++i) {
        const auto &position = nodes_[i].position;
        cells[{int(std::floor(position.x() / cellSize)), int(std::floor(position.y() / cellSize))}].append(i);
    }
    for (int i = 0; i < nodes_.size(); ++i) {
        const auto point = nodes_[i].position;
        const int cellX = int(std::floor(point.x() / cellSize)), cellY = int(std::floor(point.y() / cellSize));
        for (int x = cellX - 1; x <= cellX + 1; ++x) for (int y = cellY - 1; y <= cellY + 1; ++y) {
            const auto found = cells.constFind({x, y});
            if (found == cells.cend()) continue;
            for (const int j : *found) {
                if (j <= i) continue;
                auto delta = point - nodes_[j].position;
                double squared = QPointF::dotProduct(delta, delta);
                if (squared < .01) { delta = QPointF(.8, .6); squared = 1.; }
                const auto push = delta * (2400. / (qMax(100., squared) * std::sqrt(squared)));
                forces[i] += push; forces[j] -= push;
            }
        }
        forces[i] -= point * .0007;
    }
    for (const auto &edge : edges_) {
        const auto delta = nodes_[edge.target].position - nodes_[edge.source].position;
        const double distance = std::sqrt(QPointF::dotProduct(delta, delta));
        if (distance < .01) continue;
        const auto pull = delta * ((distance - 120.) * .006 / distance);
        forces[edge.source] += pull; forces[edge.target] -= pull;
    }
    double movement = 0;
    for (int i = 0; i < nodes_.size(); ++i) {
        if (i == dragging_) continue;
        auto &node = nodes_[i];
        node.velocity = (node.velocity + forces[i]) * .82;
        const double speed = std::hypot(node.velocity.x(), node.velocity.y());
        if (speed > 6) node.velocity *= 6. / speed;
        node.position += node.velocity;
        movement += speed;
    }
    ++iterations_;
    if (iterations_ > 30 && movement / nodes_.size() < .025) iterations_ = 180;
}
void LumenGraph::setSearch(const QString &search) { search_ = search.trimmed(); updateFilters(); }
void LumenGraph::setTypeFilter(const QString &typeId) { typeFilter_ = typeId; updateFilters(); }
void LumenGraph::updateFilters() {
    for (auto &node : nodes_) node.visible = (typeFilter_.isEmpty() || node.type == typeFilter_) &&
        (search_.isEmpty() || node.title.contains(search_, Qt::CaseInsensitive) || node.summary.contains(search_, Qt::CaseInsensitive));
    if (selected_ >= 0 && !nodes_[selected_].visible) selected_ = -1;
    hovered_ = -1;
    updateControls(); update();
}
void LumenGraph::updateControls() {
    int visible = 0, relations = 0;
    for (const auto &node : nodes_) if (node.visible) ++visible;
    for (const auto &edge : edges_) if (nodes_[edge.source].visible && nodes_[edge.target].visible) ++relations;
    countLabel_->setText(QString::number(visible) + " objetos · " + QString::number(relations) + " relaciones");
    zoomLabel_->setText(QString::number(qRound(zoom_ * 100)) + " %");
}
int LumenGraph::nodeCount() const { return nodes_.size(); }
int LumenGraph::edgeCount() const { return edges_.size(); }
QStringList LumenGraph::visibleObjectIds() const {
    QStringList ids;
    for (const auto &node : nodes_) if (node.visible) ids.append(node.id);
    return ids;
}
QString LumenGraph::selectedObjectId() const { return selected_ >= 0 && selected_ < nodes_.size() ? nodes_[selected_].id : QString(); }
void LumenGraph::focusObject(const QString &id) {
    selected_ = index_.value(id, -1);
    if (selected_ >= 0) {
        offset_ = QPointF(width() / 2., (height() - 54) / 2.) - nodes_[selected_].position * zoom_;
        setFocus(Qt::OtherFocusReason);
    }
    update();
}
double LumenGraph::radius(int index) const { return qBound(7., 7. + std::sqrt(double(nodes_[index].degree)) * 1.4, 16.); }
QPointF LumenGraph::toScreen(const QPointF &point) const { return point * zoom_ + offset_; }
QPointF LumenGraph::toWorld(const QPointF &point) const { return (point - offset_) / zoom_; }
void LumenGraph::fitToContents() {
    QRectF bounds;
    bool first = true;
    for (const auto &node : nodes_) if (node.visible) {
        const QRectF area(node.position - QPointF(50, 38), QSizeF(100, 76));
        bounds = first ? area : bounds.united(area); first = false;
    }
    if (first) { zoom_ = 1.; offset_ = QPointF(width() / 2., height() / 2.); }
    else {
        const auto available = QSizeF(qMax(80, width() - 70), qMax(80, height() - 120));
        zoom_ = qBound(.08, qMin(available.width() / bounds.width(), available.height() / bounds.height()), 1.6);
        offset_ = QPointF(width() / 2., (height() - 50) / 2.) - bounds.center() * zoom_;
    }
    updateControls(); update();
}
void LumenGraph::zoomAt(double factor, const QPointF &point) {
    const auto before = toWorld(point);
    zoom_ = qBound(.06, zoom_ * factor, 4.);
    offset_ = point - before * zoom_;
    fitPending_ = false;
    updateControls(); update();
}
void LumenGraph::zoomIn() { zoomAt(1.2, QPointF(width() / 2., (height() - 50) / 2.)); }
void LumenGraph::zoomOut() { zoomAt(1. / 1.2, QPointF(width() / 2., (height() - 50) / 2.)); }
int LumenGraph::hitNode(const QPointF &point) const {
    int closest = -1;
    double distance = std::numeric_limits<double>::max();
    for (int i = 0; i < nodes_.size(); ++i) if (nodes_[i].visible) {
        const auto delta = point - toScreen(nodes_[i].position);
        const double squared = QPointF::dotProduct(delta, delta);
        const double limit = qMax(11., radius(i) * zoom_ + 5.);
        if (squared < limit * limit && squared < distance) { closest = i; distance = squared; }
    }
    return closest;
}
void LumenGraph::paintGraph(QPainter &painter, const QRect &bounds, bool exporting) const {
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(bounds, dark_ ? QColor("#151518") : QColor("#fcfcfc"));
    const int focus = hovered_ >= 0 ? hovered_ : selected_;
    QSet<int> neighborhood;
    if (focus >= 0) {
        neighborhood.insert(focus);
        for (const auto &edge : edges_) {
            if (edge.source == focus) neighborhood.insert(edge.target);
            if (edge.target == focus) neighborhood.insert(edge.source);
        }
    }
    for (const auto &edge : edges_) {
        if (!nodes_[edge.source].visible || !nodes_[edge.target].visible) continue;
        const auto from = toScreen(nodes_[edge.source].position), to = toScreen(nodes_[edge.target].position);
        const bool active = edge.source == focus || edge.target == focus;
        const auto color = active ? (dark_ ? QColor("#d2b7f5") : QColor("#a184bd")) : (dark_ ? QColor("#39343f") : QColor("#ded8e4"));
        painter.setPen(QPen(color, active ? 1.6 : .8));
        painter.drawLine(from, to);
        if (active && !exporting) {
            const auto delta = to - from;
            const double distance = std::hypot(delta.x(), delta.y());
            if (distance > 20) {
                const auto unit = delta / distance;
                const auto arrow = to - unit * (radius(edge.target) * zoom_ + 4);
                const QPointF normal(-unit.y(), unit.x());
                painter.setBrush(color);
                painter.drawPolygon(QPolygonF{arrow, arrow - unit * 7 + normal * 3, arrow - unit * 7 - normal * 3});
            }
            const auto middle = (from + to) / 2.;
            QFont small = font(); small.setPointSizeF(qMax(9., font().pointSizeF() - 1.)); painter.setFont(small);
            const int textWidth = painter.fontMetrics().horizontalAdvance(edge.label) + 12;
            const QRectF label(middle.x() - textWidth / 2., middle.y() - 10, textWidth, 20);
            painter.setPen(Qt::NoPen); painter.setBrush(dark_ ? QColor("#242428") : QColor("#f4f4f5"));
            painter.drawRoundedRect(label, 4, 4);
            painter.setPen(dark_ ? QColor("#d7c6e7") : QColor("#805d9f")); painter.drawText(label, Qt::AlignCenter, edge.label);
        }
    }
    for (int i = 0; i < nodes_.size(); ++i) {
        const auto &node = nodes_[i];
        if (!node.visible) continue;
        const auto point = toScreen(node.position);
        if (!bounds.adjusted(-100, -100, 100, 100).contains(point.toPoint())) continue;
        const bool active = i == focus;
        const bool dimmed = focus >= 0 && !neighborhood.contains(i);
        auto color = node.color;
        if (dimmed) color.setAlpha(75);
        const double size = qMax(3., radius(i) * zoom_);
        painter.setPen(QPen(active ? (dark_ ? QColor("#e6d6f5") : QColor("#8b69ab")) : color.darker(115), active ? 2.2 : 1.));
        painter.setBrush(color); painter.drawEllipse(point, size, size);
        if (i == selected_ && hasFocus()) {
            painter.setBrush(Qt::NoBrush); painter.setPen(QPen(dark_ ? QColor("#d2b7f5") : QColor("#9f7abb"), 1., Qt::DashLine));
            painter.drawEllipse(point, size + 5, size + 5);
        }
        if (zoom_ > .4 || active || (focus >= 0 && neighborhood.contains(i))) {
            QFont labelFont = font(); labelFont.setPointSizeF(qMax(9., font().pointSizeF() - 1.)); labelFont.setWeight(active ? QFont::DemiBold : QFont::Normal); painter.setFont(labelFont);
            const auto text = painter.fontMetrics().elidedText(node.title, Qt::ElideRight, active ? 230 : 160);
            const int textWidth = painter.fontMetrics().horizontalAdvance(text);
            painter.setPen(dimmed ? (dark_ ? QColor("#69666e") : QColor("#aaa6ae")) : (dark_ ? QColor("#d4d1d7") : QColor("#615b68")));
            painter.drawText(QRectF(point.x() - textWidth / 2., point.y() + size + 6, textWidth + 3, 23), Qt::AlignHCenter | Qt::AlignTop, text);
        }
    }
    if (nodes_.isEmpty() || visibleObjectIds().isEmpty()) {
        painter.setPen(dark_ ? QColor("#919097") : QColor("#919097")); painter.setFont(font());
        painter.drawText(bounds.adjusted(30, 30, -30, -70), Qt::AlignCenter, nodes_.isEmpty() ? "Tus ideas empiezan aquí.\nCrea objetos y enlázalos para descubrir sus conexiones." : "No hay objetos que coincidan con este filtro.");
    }
    if (focus >= 0 && !exporting) {
        const auto &node = nodes_[focus];
        const int cardWidth = qMin(280, bounds.width() - 28);
        const QRect card(bounds.right() - cardWidth - 14, bounds.top() + 14, cardWidth, node.summary.isEmpty() ? 104 : 154);
        painter.setPen(QPen(dark_ ? QColor("#29292e") : QColor("#e3e3e7"), 1.));
        painter.setBrush(dark_ ? QColor("#1c1c20") : QColor("#fcfcfc")); painter.drawRoundedRect(card, 10, 10);
        QFont small = font(); small.setPointSizeF(qMax(9., font().pointSizeF() - 1.)); painter.setFont(small);
        painter.setPen(node.color); painter.drawText(card.adjusted(14, 9, -14, -9), Qt::AlignTop, typeName(node.type) + " · " + QString::number(node.degree) + " relaciones");
        QFont title = font(); title.setWeight(QFont::DemiBold); painter.setFont(title); painter.setPen(dark_ ? QColor("#e4e3e2") : QColor("#303034"));
        painter.drawText(QRect(card.left() + 14, card.top() + 32, card.width() - 28, 42), Qt::TextWordWrap, painter.fontMetrics().elidedText(node.title, Qt::ElideRight, (card.width() - 28) * 2));
        if (!node.summary.isEmpty()) {
            painter.setFont(small); painter.setPen(dark_ ? QColor("#919097") : QColor("#85818c"));
            painter.drawText(QRect(card.left() + 14, card.top() + 78, card.width() - 28, 43), Qt::TextWordWrap, painter.fontMetrics().elidedText(node.summary.simplified(), Qt::ElideRight, (card.width() - 28) * 2));
        }
        painter.setFont(small); painter.setPen(dark_ ? QColor("#c4addf") : QColor("#9073ac"));
        painter.drawText(card.adjusted(14, 0, -14, -10), Qt::AlignBottom, "Enter o clic para abrir");
    }
}
void LumenGraph::paintEvent(QPaintEvent *) { QPainter painter(this); paintGraph(painter, rect()); }
void LumenGraph::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    controls_->setGeometry(12, height() - 54, qMax(100, width() - 24), 42);
    if (fitPending_) fitToContents();
}
void LumenGraph::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) return;
    setFocus(Qt::MouseFocusReason);
    pressed_ = hitNode(event->position());
    selected_ = pressed_;
    pressPoint_ = lastPoint_ = event->position(); moved_ = false;
    panning_ = pressed_ < 0 || event->button() == Qt::MiddleButton;
    dragging_ = panning_ ? -1 : pressed_;
    fitPending_ = false; setCursor(panning_ ? Qt::ClosedHandCursor : Qt::PointingHandCursor); update();
}
void LumenGraph::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & (Qt::LeftButton | Qt::MiddleButton)) {
        const auto delta = event->position() - lastPoint_;
        if ((event->position() - pressPoint_).manhattanLength() > 5) moved_ = true;
        if (panning_) offset_ += delta;
        else if (dragging_ >= 0) { nodes_[dragging_].position = toWorld(event->position()); nodes_[dragging_].velocity = {}; }
        lastPoint_ = event->position(); update(); return;
    }
    const auto hovered = hitNode(event->position());
    if (hovered_ != hovered) { hovered_ = hovered; update(); }
    setCursor(hovered >= 0 ? Qt::PointingHandCursor : Qt::OpenHandCursor);
    if (hovered >= 0) setToolTip(nodes_[hovered].title.toHtmlEscaped() + "<br>" + typeName(nodes_[hovered].type).toHtmlEscaped() + " · " + QString::number(nodes_[hovered].degree) + " relaciones");
    else setToolTip("Rueda para ampliar · Arrastra para desplazar · Flechas para elegir un objeto");
}
void LumenGraph::mouseReleaseEvent(QMouseEvent *event) {
    const int node = pressed_;
    const bool open = !moved_ && event->button() == Qt::LeftButton && node >= 0 && hitNode(event->position()) == node;
    panning_ = false; dragging_ = pressed_ = -1;
    setCursor(Qt::OpenHandCursor);
    if (open && onOpen) onOpen(nodes_[node].id);
}
void LumenGraph::wheelEvent(QWheelEvent *event) {
    const double steps = event->angleDelta().y() / 120.;
    if (std::abs(steps) > .001) zoomAt(std::pow(1.15, steps), event->position());
    event->accept();
}
void LumenGraph::selectDirection(const QPointF &direction) {
    if (selected_ < 0) {
        for (int i = 0; i < nodes_.size(); ++i) if (nodes_[i].visible) { selected_ = i; break; }
        update(); return;
    }
    int best = -1; double score = std::numeric_limits<double>::max();
    for (int i = 0; i < nodes_.size(); ++i) if (i != selected_ && nodes_[i].visible) {
        const auto delta = nodes_[i].position - nodes_[selected_].position;
        const double along = QPointF::dotProduct(delta, direction);
        if (along <= .1) continue;
        const double across = std::abs(delta.x() * direction.y() - delta.y() * direction.x());
        const double candidate = along + across * 3 + across * across / along;
        if (candidate < score) { score = candidate; best = i; }
    }
    if (best >= 0) selected_ = best;
    hovered_ = -1;
    if (selected_ >= 0) {
        const auto point = toScreen(nodes_[selected_].position);
        if (!rect().adjusted(35, 35, -35, -85).contains(point.toPoint())) offset_ += QPointF(width() / 2., (height() - 54) / 2.) - point;
    }
    update();
}
void LumenGraph::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Left) { selectDirection(QPointF(-1, 0)); return; }
    if (event->key() == Qt::Key_Right) { selectDirection(QPointF(1, 0)); return; }
    if (event->key() == Qt::Key_Up) { selectDirection(QPointF(0, -1)); return; }
    if (event->key() == Qt::Key_Down) { selectDirection(QPointF(0, 1)); return; }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && selected_ >= 0 && onOpen) { onOpen(nodes_[selected_].id); return; }
    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_0) { fitPending_ = false; fitToContents(); return; }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) { zoomIn(); return; }
    if (event->key() == Qt::Key_Minus) { zoomOut(); return; }
    if (event->key() == Qt::Key_Escape) { selected_ = hovered_ = -1; update(); return; }
    QWidget::keyPressEvent(event);
}
void LumenGraph::leaveEvent(QEvent *event) { hovered_ = -1; update(); QWidget::leaveEvent(event); }
void LumenGraph::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!reducedMotion_ && iterations_ < 180 && !nodes_.isEmpty()) layoutTimer_.start();
}
bool LumenGraph::exportImage(const QString &path) {
    if (path.isEmpty()) return false;
    QImage image(size() * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2.);
    QPainter painter(&image); paintGraph(painter, rect(), true); painter.end();
    return image.save(path, "PNG");
}
