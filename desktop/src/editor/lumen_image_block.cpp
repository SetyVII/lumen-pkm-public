#include "lumen_document.hpp"
#include "lumen_editor.hpp"
#include "lumen_icons.hpp"
#include <memory>

namespace {
class ImageResizeHandle : public QWidget {
  public:
    std::function<void(QMouseEvent *)> press, move, release;
    using QWidget::QWidget;

  protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && press) {
            press(e);
            e->accept();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if ((e->buttons() & Qt::LeftButton) && move) {
            move(e);
            e->accept();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && release) {
            release(e);
            e->accept();
        }
    }
};
struct ImageDrag {
    QTextCursor cursor;
    qreal width = 0, height = 0;
    QPoint start;
    bool active = false;
};
constexpr int NativeBlockProperty = QTextFormat::UserProperty + 714;
QJsonObject imageNode(const QTextBlock &block) {
    return QJsonDocument::fromJson(block.blockFormat().property(NativeBlockProperty).toByteArray())
        .object()
        .value("canonical_node")
        .toObject();
}
} // namespace

void RichEditor::updateImageBar() {
    const auto node = imageNode(textCursor().block());
    if (isReadOnly() || node.value("type") != "image") {
        if (imageBar_)
            imageBar_->hide();
        if (imageLeftHandle_)
            imageLeftHandle_->hide();
        if (imageRightHandle_)
            imageRightHandle_->hide();
        return;
    }
    if (!imageBar_) {
        auto *bar = new QFrame(viewport());
        imageBar_ = bar;
        bar->setObjectName("imageBlockToolbar");
        bar->setStyleSheet(dark_ ? "QFrame#imageBlockToolbar { background:#25252b; border:1px solid #42424a; "
                                   "border-radius:6px; }"
                                 : "QFrame#imageBlockToolbar { background:#fafafa; border:1px solid #d4d4d8; "
                                   "border-radius:6px; }");
        auto *row = new QHBoxLayout(bar);
        row->setContentsMargins(4, 2, 4, 2);
        row->setSpacing(2);
        auto action = [&](QString title, QString icon, const std::function<void()> &run) {
            auto *button = new QToolButton(bar);
            button->setAutoRaise(true);
            button->setFocusPolicy(Qt::NoFocus);
            button->setIcon(LumenIcons::icon(icon, QColor(dark_ ? "#e4e4e7" : "#303034"), 18));
            button->setText(title);
            if (icon.startsWith("align-")) {
                button->setToolButtonStyle(Qt::ToolButtonTextOnly);
                button->setText(icon == "align-left" ? "Izq." : icon == "align-center" ? "Centro" : "Der.");
            }
            button->setToolTip(title);
            button->setAccessibleName(title);
            button->setObjectName("image" + icon);
            row->addWidget(button);
            connect(button, &QToolButton::clicked, this, run);
        };
        auto setAttribute = [this](const QString &key, const QJsonValue &value) {
            const auto image = imageNode(textCursor().block());
            applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "set_attrs"},
                                                          {"node_id", image.value("id")},
                                                          {"attrs", QJsonObject{{key, value}}}}});
        };
        action("Alinear imagen a la izquierda", "align-left", [setAttribute] {
            setAttribute("alignment", "left");
        });
        action("Centrar imagen", "align-center", [setAttribute] {
            setAttribute("alignment", "center");
        });
        action("Alinear imagen a la derecha", "align-right", [setAttribute] {
            setAttribute("alignment", "right");
        });
        action("Añadir o editar leyenda", "typography", [this, setAttribute] {
            const auto current = imageNode(textCursor().block()).value("attrs").toObject();
            bool ok = false;
            const auto caption =
                QInputDialog::getText(this, "Leyenda de imagen", "Leyenda", QLineEdit::Normal,
                                      current.value("caption").toString(), &ok);
            if (ok)
                setAttribute("caption", caption);
        });
        action("Opciones de imagen", "dots", [this] {
            const auto current = imageNode(textCursor().block());
            const auto attrs = current.value("attrs").toObject();
            QMenu menu(this);
            if (!attrs.value("object_id").toString().isEmpty() && openReference)
                menu.addAction("Abrir objeto imagen", this, [this, attrs] {
                    openReference(attrs.value("object_id").toString());
                });
            menu.addAction("Cambiar ancho", this, [this, current, attrs] {
                bool ok = false;
                const auto width = QInputDialog::getInt(this, "Ancho de imagen", "Ancho en píxeles",
                                                        attrs.value("width").toInt(320), 32, 4096, 1, &ok);
                if (!ok)
                    return;
                auto patch = QJsonObject{{"width", width}};
                if (attrs.value("width").toDouble() > 0 && attrs.value("height").toDouble() > 0)
                    patch["height"] =
                        attrs.value("height").toDouble() * width / attrs.value("width").toDouble();
                applyCanonicalCommands(QJsonArray{
                    QJsonObject{{"type", "set_attrs"}, {"node_id", current.value("id")}, {"attrs", patch}}});
            });
            menu.addSeparator();
            menu.addAction("Eliminar bloque imagen", this, [this, current] {
                applyCanonicalCommands(
                    QJsonArray{QJsonObject{{"type", "delete"}, {"node_id", current.value("id")}}});
            });
            menu.exec(QCursor::pos());
        });
    }
    auto imageCursor = QTextCursor(document());
    QTextImageFormat imageFormat;
    int offset = 0;
    for (auto it = textCursor().block().begin(); !it.atEnd(); ++it)
        if (it.fragment().charFormat().isImageFormat()) {
            const auto fragment = it.fragment();
            imageFormat = fragment.charFormat().toImageFormat();
            imageCursor.setPosition(fragment.position());
            imageCursor.setPosition(fragment.position() + 1, QTextCursor::KeepAnchor);
            offset = fragment.position() - textCursor().block().position();
            break;
        }
    if (!imageCursor.hasSelection())
        return;
    if (!imageLeftHandle_) {
        for (int side : {-1, 1}) {
            auto *handle = new ImageResizeHandle(viewport());
            handle->setObjectName(side < 0 ? "imageResizeLeft" : "imageResizeRight");
            handle->setCursor(Qt::SizeHorCursor);
            handle->setToolTip("Arrastrar para cambiar tamaño de imagen");
            handle->setStyleSheet("background:#555560; border:1px solid #aaaab4; border-radius:3px;");
            auto drag = std::make_shared<ImageDrag>();
            handle->press = [this, drag](QMouseEvent *event) {
                for (auto it = textCursor().block().begin(); !it.atEnd(); ++it)
                    if (it.fragment().charFormat().isImageFormat()) {
                        const auto fragment = it.fragment();
                        const auto format = fragment.charFormat().toImageFormat();
                        drag->cursor = QTextCursor(document());
                        drag->cursor.setPosition(fragment.position());
                        drag->cursor.setPosition(fragment.position() + 1, QTextCursor::KeepAnchor);
                        drag->width = format.width();
                        drag->height = format.height();
                        drag->start = event->globalPosition().toPoint();
                        if (drag->width <= 0)
                            drag->width = 320;
                        if (drag->height <= 0)
                            drag->height = 240;
                        drag->cursor.beginEditBlock();
                        drag->active = true;
                        break;
                    }
            };
            handle->move = [this, drag, side](QMouseEvent *event) {
                if (!drag->active)
                    return;
                const auto delta = (event->globalPosition().x() - drag->start.x()) * side;
                const auto width =
                    qBound(32.0, drag->width + delta, qreal(qMax(32, viewport()->width() - 32)));
                const auto height = width * drag->height / drag->width;
                auto format = drag->cursor.charFormat().toImageFormat();
                format.setWidth(width);
                format.setHeight(height);
                drag->cursor.setCharFormat(format);
                auto metadata = QJsonDocument::fromJson(
                                    drag->cursor.blockFormat().property(NativeBlockProperty).toByteArray())
                                    .object();
                auto canonical = metadata.value("canonical_node").toObject();
                auto attrs = canonical.value("attrs").toObject();
                attrs["width"] = width;
                attrs["height"] = height;
                canonical["attrs"] = attrs;
                metadata["canonical_node"] = canonical;
                auto blockFormat = drag->cursor.blockFormat();
                blockFormat.setProperty(NativeBlockProperty,
                                        QJsonDocument(metadata).toJson(QJsonDocument::Compact));
                drag->cursor.setBlockFormat(blockFormat);
                updateImageBar();
                viewport()->update();
            };
            handle->release = [this, drag](QMouseEvent *) {
                if (drag->active) {
                    drag->cursor.endEditBlock();
                    drag->active = false;
                    updateImageBar();
                }
            };
            if (side < 0)
                imageLeftHandle_ = handle;
            else
                imageRightHandle_ = handle;
        }
    }
    const auto blockRect = document()->documentLayout()->blockBoundingRect(textCursor().block());
    const auto line = textCursor().block().layout()->lineForTextPosition(offset);
    const auto width = imageFormat.width() > 0 ? imageFormat.width() : 320;
    const auto height = imageFormat.height() > 0 ? imageFormat.height() : 240;
    const auto imageRect = QRectF(blockRect.left() + (line.isValid() ? line.cursorToX(offset) : 0) -
                                      horizontalScrollBar()->value(),
                                  blockRect.top() + (line.isValid() ? line.y() + line.ascent() - height : 0) -
                                      verticalScrollBar()->value(),
                                  width, height);
    const auto handleHeight = qRound(qMin<qreal>(100, height * .65));
    imageLeftHandle_->setGeometry(qRound(imageRect.left() - 9),
                                  qRound(imageRect.center().y() - handleHeight / 2.), 6, handleHeight);
    imageRightHandle_->setGeometry(qRound(imageRect.right() + 3),
                                   qRound(imageRect.center().y() - handleHeight / 2.), 6, handleHeight);
    imageLeftHandle_->show();
    imageLeftHandle_->raise();
    imageRightHandle_->show();
    imageRightHandle_->raise();
    styleLayer(imageBar_);
    imageBar_->adjustSize();
    const auto rect = document()->documentLayout()->blockBoundingRect(textCursor().block());
    const int top = qRound(rect.top()) - verticalScrollBar()->value();
    if (top < -rect.height() || top > viewport()->height()) {
        imageBar_->hide();
        return;
    }
    imageBar_->move(qMax(4, viewport()->width() - imageBar_->width() - 12), qMax(4, top + 4));
    imageBar_->show();
    imageBar_->raise();
}
