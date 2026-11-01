#pragma once

#include <QtWidgets>
#include <QPdfDocument>
#include <QPdfView>

// All document bytes and reading positions are kept in process memory only.
class MediaViewer : public QDialog {
  public:
    static MediaViewer *showAsset(QWidget *parent, const QString &id, const QString &filename,
                                  const QByteArray &bytes, bool dark = false,
                                  bool reducedMotion = false);
    ~MediaViewer() override;
    int currentPage() const;
    int pageCount() const;
    void setPage(int page);
    void zoomIn();
    void zoomOut();
    void fitPage();
    void fitWidth();
    void actualSize();
    void copyImageToClipboard();
    QString errorText() const;
  protected:
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
  private:
    MediaViewer(QWidget *parent, QString id, QString filename, QByteArray bytes, bool dark,
                bool reducedMotion);
    QString assetId_;
    QByteArray bytes_;
    QPixmap image_;
    QLabel *imageLabel_ = nullptr, *zoomLabel_ = nullptr, *errorLabel_ = nullptr;
    QScrollArea *imageScroll_ = nullptr;
    QPdfDocument *pdf_ = nullptr;
    QPdfView *pdfView_ = nullptr;
    QSpinBox *pageInput_ = nullptr;
    double imageZoom_ = 1.;
    quint64 readingIntent_ = 0;
    enum class ImageMode { FitPage, FitWidth, Actual, Custom } imageMode_ = ImageMode::FitPage;
    void updateImage();
    void updatePage();
    void rememberPosition();
};
