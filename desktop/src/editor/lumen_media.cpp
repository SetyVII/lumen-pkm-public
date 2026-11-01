#include "lumen_media.hpp"
#include "lumen_icons.hpp"
#include <QBuffer>
#include <QImageReader>
#include <QPdfPageNavigator>
#include <algorithm>

namespace {
struct ReadingPosition {
    int page = 0;
    QPointF location;
    int horizontal = 0, vertical = 0;
    double zoom = 1.;
    int mode = 0;
};
QHash<QString, ReadingPosition> positions;
class LumenMediaTitle : public QLabel {
  public:
    explicit LumenMediaTitle(const QString &title, QWidget *parent) : QLabel(title, parent) {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }
  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::WindowText));
        painter.setFont(font());
        painter.drawText(rect(), Qt::AlignVCenter | Qt::AlignLeft,
                         fontMetrics().elidedText(text(), Qt::ElideMiddle, width()));
    }
};
QToolButton *viewerTool(QWidget *parent, const QString &text, const QString &name,
                        const QString &tooltip, bool textVisible = false) {
    auto *button = new QToolButton(parent);
    const QString iconPath = ":/icons/" + name + ".svg";
    if (!name.isEmpty() && QFile::exists(iconPath)) button->setIcon(QIcon(iconPath));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setMinimumSize(32, 32);
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    button->setToolButtonStyle(textVisible || button->icon().isNull() ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    return button;
}
}

MediaViewer *MediaViewer::showAsset(QWidget *parent, const QString &id, const QString &filename,
                                  const QByteArray &bytes, bool dark, bool reducedMotion) {
    auto *viewer = new MediaViewer(parent, id, filename, bytes, dark, reducedMotion);
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    viewer->show();
    viewer->raise();
    viewer->activateWindow();
    return viewer;
}
MediaViewer::MediaViewer(QWidget *parent, QString id, QString filename, QByteArray bytes, bool dark,
                         bool reducedMotion)
    : QDialog(parent), assetId_(std::move(id)), bytes_(std::move(bytes)) {
    setObjectName("mediaViewer");
    setWindowTitle(filename);
    setAccessibleName("Visor de " + filename);
    setWindowModality(Qt::WindowModal);
    setSizeGripEnabled(true);
    const auto bounds = screen()->availableGeometry();
    resize(qMin(980, int(bounds.width() * .85)), qMin(780, int(bounds.height() * .87)));
    setMinimumSize(qMin(560, bounds.width() - 40), qMin(400, bounds.height() - 40));
    const QString background = dark ? "#151518" : "#fcfcfc";
    const QString foreground = dark ? "#e4e3e2" : "#303034";
    const QString border = dark ? "#29292e" : "#e3e3e7";
    const QString hover = dark ? "#242428" : "#f4f4f5";
    setStyleSheet(QString(
        "QDialog#mediaViewer{background:%1;color:%2;border:0;}"
        "QLabel{color:%2;background:transparent;border:0;}"
        "QFrame,QScrollArea,QPdfView{border:0;}"
        "QToolButton{background:transparent;color:%2;border:1px solid transparent;border-radius:5px;padding:5px;}"
        "QToolButton:hover{background:%4;}QToolButton:pressed{background:#b499d4;}"
        "QToolButton:focus{border:1px solid #b499d4;}"
        "QSpinBox{background:%1;color:%2;border:1px solid %3;border-radius:5px;padding:5px;}"
    ).arg(background, foreground, border, hover));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(8);
    auto *header = new QHBoxLayout;
    auto *name = new LumenMediaTitle(filename, this);
    name->setTextFormat(Qt::PlainText);
    name->setToolTip(filename);
    name->setAccessibleName(filename);
    name->setStyleSheet("font-weight:600;");
    header->addWidget(name, 1);
    auto *close = viewerTool(this, "Cerrar", "x", "Cerrar visor · Escape", true);
    close->setObjectName("mediaClose");
    connect(close, &QToolButton::clicked, this, &QDialog::reject);
    header->addWidget(close);
    layout->addLayout(header);
    auto *controls = new QHBoxLayout;
    controls->setSpacing(4);
    auto *previous = viewerTool(this, "Anterior", "chevron-left", "Página anterior · ←");
    previous->setObjectName("mediaPreviousPage");
    auto *next = viewerTool(this, "Siguiente", "chevron-right", "Página siguiente · →");
    next->setObjectName("mediaNextPage");
    pageInput_ = new QSpinBox(this);
    pageInput_->setObjectName("mediaPage");
    pageInput_->setAccessibleName("Página actual");
    pageInput_->setMinimumWidth(88);
    pageInput_->setKeyboardTracking(false);
    controls->addWidget(previous);
    controls->addWidget(pageInput_);
    controls->addWidget(next);
    controls->addStretch();
    auto *fit = viewerTool(this, "Ajustar", "arrows-maximize", "Ajustar página o imagen a la ventana · Ctrl+0", true);
    fit->setObjectName("mediaFitPage");
    auto *widthButton = viewerTool(this, "Ancho", "arrows-maximize", "Ajustar al ancho", true);
    widthButton->setObjectName("mediaFitWidth");
    auto *out = viewerTool(this, "−", "zoom-out", "Alejar · Ctrl+−");
    auto *in = viewerTool(this, "+", "zoom-in", "Acercar · Ctrl++");
    auto *actual = viewerTool(this, "100 %", "", "Tamaño real · 100 %", true);
    actual->setObjectName("mediaActualSize");
    auto *copy = viewerTool(this, "Copiar", "copy", "Copiar imagen al portapapeles · Ctrl+C", true);
    copy->setObjectName("mediaCopy");
    zoomLabel_ = new QLabel(this);
    zoomLabel_->setMinimumWidth(50);
    zoomLabel_->setAlignment(Qt::AlignCenter);
    zoomLabel_->setAccessibleName("Nivel de ampliación");
    controls->addWidget(fit);
    controls->addWidget(widthButton);
    controls->addWidget(out);
    controls->addWidget(zoomLabel_);
    controls->addWidget(in);
    controls->addWidget(actual);
    controls->addWidget(copy);
    layout->addLayout(controls);
    connect(previous, &QToolButton::clicked, this, [this] { setPage(currentPage() - 1); });
    connect(next, &QToolButton::clicked, this, [this] { setPage(currentPage() + 1); });
    connect(pageInput_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int page) { setPage(page - 1); });
    connect(fit, &QToolButton::clicked, this, [this] { fitPage(); });
    connect(widthButton, &QToolButton::clicked, this, [this] { fitWidth(); });
    connect(out, &QToolButton::clicked, this, [this] { zoomOut(); });
    connect(in, &QToolButton::clicked, this, [this] { zoomIn(); });
    connect(actual, &QToolButton::clicked, this, [this] { actualSize(); });
    connect(copy, &QToolButton::clicked, this, &MediaViewer::copyImageToClipboard);
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName("mediaError");
    errorLabel_->setWordWrap(true);
    errorLabel_->setAlignment(Qt::AlignCenter);
    errorLabel_->hide();
    layout->addWidget(errorLabel_);

    // Fedora can decode a PDF through QImageReader; test the PDF signature first.
    const bool isPdf = bytes_.left(1024).contains("%PDF-");
    if (isPdf) {
        pdf_ = new QPdfDocument(this);
        auto *buffer = new QBuffer(pdf_);
        buffer->setData(bytes_);
        buffer->open(QIODevice::ReadOnly);
        pdfView_ = new QPdfView(this);
        pdfView_->setObjectName("mediaPdfView");
        pdfView_->setAccessibleName("Documento PDF");
        pdfView_->setDocument(pdf_);
        pdfView_->setPageMode(QPdfView::PageMode::MultiPage);
        pdfView_->setZoomMode(QPdfView::ZoomMode::FitInView);
        pdfView_->setDocumentMargins(QMargins(12, 12, 12, 12));
        layout->addWidget(pdfView_, 1);
        const quint64 initialReadingIntent = readingIntent_;
        connect(pdf_, &QPdfDocument::statusChanged, this,
            [this, previous, next, initialReadingIntent](QPdfDocument::Status status) {
                if (status == QPdfDocument::Status::Ready) {
                    {
                        // Initializing the counter is not a reader navigation request.
                        QSignalBlocker guard(pageInput_);
                        pageInput_->setRange(1, qMax(1, pdf_->pageCount()));
                        pageInput_->setSuffix(" / " + QString::number(pdf_->pageCount()));
                    }
                    previous->setEnabled(pdf_->pageCount() > 1);
                    next->setEnabled(pdf_->pageCount() > 1);
                    const auto saved = positions.value(assetId_);
                    const int page = qBound(0, saved.page, pdf_->pageCount() - 1);
                    QTimer::singleShot(0, this, [this, saved, page, initialReadingIntent] {
                        // Restoration is an initial default. A newer page/zoom request wins.
                        if (readingIntent_ != initialReadingIntent) return;
                        if (saved.mode == int(QPdfView::ZoomMode::Custom) && saved.zoom > 0)
                            pdfView_->setZoomFactor(saved.zoom);
                        if (positions.contains(assetId_)) pdfView_->setZoomMode(QPdfView::ZoomMode(saved.mode));
                        pdfView_->pageNavigator()->jump(page, saved.location, pdfView_->zoomFactor());
                        updatePage();
                    });
                } else if (status == QPdfDocument::Status::Error) {
                    errorLabel_->setText("No se pudo abrir este PDF. Comprueba su formato o si necesita contraseña.");
                    errorLabel_->show();
                    pdfView_->hide();
                }
            });
        connect(pdfView_->pageNavigator(), &QPdfPageNavigator::currentPageChanged, this, [this] { updatePage(); });
        connect(pdfView_, &QPdfView::zoomFactorChanged, this, [this](double zoom) {
            zoomLabel_->setText(QString::number(qRound(zoom * 100)) + " %");
        });
        pdf_->load(buffer);
    } else {
        previous->hide(); next->hide(); pageInput_->hide();
        QBuffer buffer(&bytes_);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        const QSize size = reader.size();
        const bool safeSize = size.isValid() && qint64(size.width()) * size.height() <= 80000000 &&
                              size.width() <= 60000 && size.height() <= 60000;
        if (safeSize) image_ = QPixmap::fromImage(reader.read());
        if (image_.isNull()) {
            errorLabel_->setText("No se pudo abrir la imagen. El archivo está dañado, tiene un formato no compatible o supera el tamaño admitido.");
            errorLabel_->show();
            layout->addStretch();
        } else {
            imageScroll_ = new QScrollArea(this);
            imageScroll_->setObjectName("mediaImageScroll");
            imageScroll_->setAlignment(Qt::AlignCenter);
            imageScroll_->setWidgetResizable(false);
            imageScroll_->setStyleSheet(QString("QScrollArea{background:%1;border:0;}").arg(dark ? "#0d0d10" : "#f4f4f5"));
            imageLabel_ = new QLabel(imageScroll_);
            imageLabel_->setObjectName("mediaImage");
            imageLabel_->setAlignment(Qt::AlignCenter);
            imageLabel_->setAccessibleName(filename);
            imageLabel_->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(imageLabel_, &QLabel::customContextMenuRequested, this, [this](const QPoint &pos) {
                QMenu menu(this);
                menu.addAction(LumenIcons::icon("copy"), "Copiar imagen (Ctrl+C)", this, &MediaViewer::copyImageToClipboard);
                menu.exec(imageLabel_->mapToGlobal(pos));
            });
            imageScroll_->setWidget(imageLabel_);
            layout->addWidget(imageScroll_, 1);
            if (positions.contains(assetId_)) {
                const auto saved = positions.value(assetId_);
                imageZoom_ = saved.zoom;
                imageMode_ = ImageMode(qBound(0, saved.mode, 3));
                QTimer::singleShot(0, this, [this, saved] {
                    updateImage();
                    imageScroll_->horizontalScrollBar()->setValue(saved.horizontal);
                    imageScroll_->verticalScrollBar()->setValue(saved.vertical);
                });
            }
        }
    }
    auto *footer = new QLabel("Vista privada en memoria · Escape para cerrar", this);
    footer->setStyleSheet("font-size:11px;color:#919097;");
    layout->addWidget(footer);
    QTimer::singleShot(0, this, [this, close] { updateImage(); updatePage(); close->setFocus(); });
    if (!reducedMotion) {
        // Fade only this transient viewer, never the writing surface.
        setWindowOpacity(.94);
        auto *animation = new QPropertyAnimation(this, "windowOpacity", this);
        animation->setDuration(150);
        animation->setStartValue(.94);
        animation->setEndValue(1.);
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}
MediaViewer::~MediaViewer() {
    rememberPosition();
    // Destroy the renderer before its document, and before the controls used by its signals.
    if (pdf_) QObject::disconnect(pdf_, nullptr, this, nullptr);
    if (pdfView_) {
        QObject::disconnect(pdfView_, nullptr, this, nullptr);
        QObject::disconnect(pdfView_->pageNavigator(), nullptr, this, nullptr);
        delete pdfView_;
        pdfView_ = nullptr;
    }
    delete pdf_;
    pdf_ = nullptr;
}
int MediaViewer::currentPage() const { return pdfView_ ? pdfView_->pageNavigator()->currentPage() : 0; }
int MediaViewer::pageCount() const { return pdf_ ? pdf_->pageCount() : image_.isNull() ? 0 : 1; }
void MediaViewer::setPage(int page) {
    if (!pdfView_ || !pdf_ || pdf_->pageCount() < 1) return;
    ++readingIntent_;
    pdfView_->pageNavigator()->jump(qBound(0, page, pdf_->pageCount() - 1), QPointF(), pdfView_->zoomFactor());
    updatePage();
}
void MediaViewer::updatePage() {
    if (!pdfView_ || !pageInput_) return;
    QSignalBlocker guard(pageInput_);
    pageInput_->setValue(currentPage() + 1);
    zoomLabel_->setText(QString::number(qRound(pdfView_->zoomFactor() * 100)) + " %");
}
void MediaViewer::updateImage() {
    if (!imageScroll_ || image_.isNull()) return;
    const auto available = imageScroll_->viewport()->size() - QSize(18, 18);
    if (imageMode_ == ImageMode::FitPage)
        imageZoom_ = qMin(double(available.width()) / image_.width(), double(available.height()) / image_.height());
    else if (imageMode_ == ImageMode::FitWidth)
        imageZoom_ = double(available.width()) / image_.width();
    else if (imageMode_ == ImageMode::Actual) imageZoom_ = 1.;
    imageZoom_ = qBound(.05, imageZoom_, 8.);
    const QSize target(qMax(1, qRound(image_.width() * imageZoom_)), qMax(1, qRound(image_.height() * imageZoom_)));
    imageLabel_->setPixmap(image_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    imageLabel_->resize(target);
    zoomLabel_->setText(QString::number(qRound(imageZoom_ * 100)) + " %");
}
void MediaViewer::zoomIn() {
    ++readingIntent_;
    if (pdfView_) {
        const auto zoom = qBound(.1, pdfView_->zoomFactor() * 1.25, 6.);
        pdfView_->setZoomMode(QPdfView::ZoomMode::Custom);
        pdfView_->setZoomFactor(zoom);
    } else { imageMode_ = ImageMode::Custom; imageZoom_ *= 1.25; updateImage(); }
}
void MediaViewer::zoomOut() {
    ++readingIntent_;
    if (pdfView_) {
        const auto zoom = qBound(.1, pdfView_->zoomFactor() / 1.25, 6.);
        pdfView_->setZoomMode(QPdfView::ZoomMode::Custom);
        pdfView_->setZoomFactor(zoom);
    } else { imageMode_ = ImageMode::Custom; imageZoom_ /= 1.25; updateImage(); }
}
void MediaViewer::fitPage() {
    ++readingIntent_;
    if (pdfView_) pdfView_->setZoomMode(QPdfView::ZoomMode::FitInView);
    else { imageMode_ = ImageMode::FitPage; updateImage(); }
}
void MediaViewer::fitWidth() {
    ++readingIntent_;
    if (pdfView_) pdfView_->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    else { imageMode_ = ImageMode::FitWidth; updateImage(); }
}
void MediaViewer::actualSize() {
    ++readingIntent_;
    if (pdfView_) { pdfView_->setZoomMode(QPdfView::ZoomMode::Custom); pdfView_->setZoomFactor(1.); }
    else { imageMode_ = ImageMode::Actual; updateImage(); }
}
void MediaViewer::copyImageToClipboard() {
    if (!image_.isNull()) {
        QApplication::clipboard()->setPixmap(image_);
        QToolTip::showText(QCursor::pos(), "Imagen copiada al portapapeles", this);
    }
}
QString MediaViewer::errorText() const { return errorLabel_ ? errorLabel_->text() : QString(); }
void MediaViewer::rememberPosition() {
    if (assetId_.isEmpty() || (image_.isNull() && !pdfView_)) return;
    ReadingPosition position;
    if (pdfView_) {
        position.page = currentPage();
        position.location = pdfView_->pageNavigator()->currentLocation();
        position.zoom = pdfView_->zoomFactor();
        position.mode = int(pdfView_->zoomMode());
    } else {
        position.horizontal = imageScroll_->horizontalScrollBar()->value();
        position.vertical = imageScroll_->verticalScrollBar()->value();
        position.zoom = imageZoom_;
        position.mode = int(imageMode_);
    }
    if (positions.size() >= 128 && !positions.contains(assetId_)) positions.erase(positions.begin());
    positions.insert(assetId_, position);
}
void MediaViewer::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    updateImage();
}
void MediaViewer::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { reject(); return; }
    if (event->matches(QKeySequence::Copy) ||
        ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_C)) {
        copyImageToClipboard();
        return;
    }
    if (event->modifiers() & Qt::ControlModifier) {
        if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) { zoomIn(); return; }
        if (event->key() == Qt::Key_Minus) { zoomOut(); return; }
        if (event->key() == Qt::Key_0) { fitPage(); return; }
    }
    if (pdfView_ && !pageInput_->hasFocus()) {
        if (event->key() == Qt::Key_Right || event->key() == Qt::Key_PageDown) { setPage(currentPage() + 1); return; }
        if (event->key() == Qt::Key_Left || event->key() == Qt::Key_PageUp) { setPage(currentPage() - 1); return; }
        if (event->key() == Qt::Key_Home) { setPage(0); return; }
        if (event->key() == Qt::Key_End) { setPage(pageCount() - 1); return; }
    }
    QDialog::keyPressEvent(event);
}
void MediaViewer::closeEvent(QCloseEvent *event) { rememberPosition(); QDialog::closeEvent(event); }
