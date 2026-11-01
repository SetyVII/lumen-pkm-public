#pragma once
#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include <set>
#include "lumen_icons.hpp"

// Forward declaration if needed
class ApiClient;

class LumenImageDropZone : public QFrame {
public:
    std::function<void(const QString &filePath)> onFileDropped;
    std::function<void(const QByteArray &bytes, const QString &filename, const QString &mime)> onDataDropped;
    bool isDragging = false;
    bool darkTheme = true;

    explicit LumenImageDropZone(QWidget *parent = nullptr, bool dark = true)
        : QFrame(parent), darkTheme(dark) {
        setAcceptDrops(true);
        setObjectName("lumenImageDropZone");
        setMinimumHeight(230);
        updateStyle();
    }

    void updateStyle() {
        QString borderCol = isDragging ? "#9a86bd" : (darkTheme ? "#383842" : "#d0cfe0");
        QString bgCol = isDragging ? (darkTheme ? "#272432" : "#f3effa") : (darkTheme ? "#1b1b1e" : "#fafafc");
        setStyleSheet(QString(
            "#lumenImageDropZone { border: 2px dashed %1; border-radius: 12px; background: %2; }"
        ).arg(borderCol, bgCol));
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
            isDragging = true;
            updateStyle();
            event->acceptProposedAction();
        }
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) {
            event->acceptProposedAction();
        }
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override {
        isDragging = false;
        updateStyle();
        event->accept();
    }
    void dropEvent(QDropEvent *event) override {
        isDragging = false;
        updateStyle();
        if (event->mimeData()->hasUrls()) {
            for (const auto &url : event->mimeData()->urls()) {
                if (url.isLocalFile()) {
                    QString localPath = url.toLocalFile();
                    if (onFileDropped) {
                        onFileDropped(localPath);
                        event->acceptProposedAction();
                        return;
                    }
                }
            }
        }
        if (event->mimeData()->hasImage()) {
            QImage img = qvariant_cast<QImage>(event->mimeData()->imageData());
            if (!img.isNull() && onDataDropped) {
                QByteArray bytes;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                img.save(&buffer, "PNG");
                onDataDropped(bytes, "imagen-portapapeles.png", "image/png");
                event->acceptProposedAction();
                return;
            }
        }
        event->accept();
    }
};

class LumenAddImageDialog : public QDialog {
public:
    using OnImageSelected = std::function<void(QString assetId)>;

    LumenAddImageDialog(QWidget *parent,
                        std::function<void(const QString &objectId, const QString &filename, const QByteArray &bytes, const QString &mime, std::function<void(QJsonValue, QString)>)> uploader,
                        std::function<void(const QString &path, std::function<void(QByteArray, QString)>)> byteFetcher,
                        const QJsonArray &workspaceObjects,
                        bool dark = true,
                        OnImageSelected onSelected = {})
        : QDialog(parent), uploader_(uploader), byteFetcher_(byteFetcher),
          objects_(workspaceObjects), dark_(dark), onSelected_(onSelected) {
        setWindowTitle("Añadir Imagen");
        setModal(true);
        setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
        resize(580, 480);
        setMinimumSize(520, 440);
        setupUI();
    }

private:
    std::function<void(const QString &, const QString &, const QByteArray &, const QString &, std::function<void(QJsonValue, QString)>)> uploader_;
    std::function<void(const QString &, std::function<void(QByteArray, QString)>)> byteFetcher_;
    QJsonArray objects_;
    bool dark_ = true;
    OnImageSelected onSelected_;

    QStackedWidget *stack_ = nullptr;
    QList<QPushButton*> tabButtons_;
    QLabel *statusLabel_ = nullptr;
    QLineEdit *urlInput_ = nullptr;
    QPushButton *urlSubmitBtn_ = nullptr;

    void setupUI() {
        setObjectName("addImageDialog");
        setStyleSheet(QString(
            "QDialog#addImageDialog { background: %1; color: %2; font-family: Inter, sans-serif; }"
            "QLabel { color: %2; }"
        ).arg(dark_ ? "#1a1a1d" : "#f6f6f8", dark_ ? "#e5e4ea" : "#222224"));

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(20, 16, 20, 20);
        root->setSpacing(14);

        // Header: Icon + Title + Close Button
        auto *header = new QHBoxLayout;
        header->setSpacing(8);

        auto *iconLabel = new QLabel(this);
        iconLabel->setPixmap(LumenIcons::icon("photo", dark_ ? QColor("#e5e4ea") : QColor("#222224"), 20).pixmap(20, 20));
        header->addWidget(iconLabel);

        auto *title = new QLabel("Añadir Imagen", this);
        title->setStyleSheet("font-size: 16px; font-weight: 600;");
        header->addWidget(title);
        header->addStretch();

        auto *closeBtn = new QPushButton(this);
        closeBtn->setIcon(LumenIcons::icon("x", dark_ ? QColor("#a5a4ac") : QColor("#686772"), 18));
        closeBtn->setCursor(Qt::PointingHandCursor);
        closeBtn->setStyleSheet(
            "QPushButton { background: transparent; border: 0; border-radius: 6px; padding: 4px; }"
            "QPushButton:hover { background: rgba(160, 160, 175, 0.15); }"
        );
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
        header->addWidget(closeBtn);
        root->addLayout(header);

        // Tab Bar
        auto *tabsRow = new QHBoxLayout;
        tabsRow->setSpacing(6);
        const QStringList tabTitles = {"Subir", "Añadir un enlace", "Unsplash", "Contenido existente"};
        for (int i = 0; i < tabTitles.size(); ++i) {
            auto *btn = new QPushButton(tabTitles[i], this);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setProperty("tabIndex", i);
            connect(btn, &QPushButton::clicked, this, [this, i] { switchTab(i); });
            tabsRow->addWidget(btn);
            tabButtons_.append(btn);
        }
        tabsRow->addStretch();
        root->addLayout(tabsRow);

        // Stacked Pages
        stack_ = new QStackedWidget(this);
        stack_->addWidget(createUploadTab());
        stack_->addWidget(createLinkTab());
        stack_->addWidget(createUnsplashTab());
        stack_->addWidget(createExistingTab());
        root->addWidget(stack_, 1);

        switchTab(0);
    }

    void switchTab(int index) {
        if (!stack_ || index < 0 || index >= stack_->count()) return;
        stack_->setCurrentIndex(index);
        for (int i = 0; i < tabButtons_.size(); ++i) {
            bool active = (i == index);
            if (active) {
                tabButtons_[i]->setStyleSheet(QString(
                    "QPushButton { background: %1; color: %2; font-weight: 600; font-size: 13px; border: 0; border-radius: 6px; padding: 6px 14px; }"
                ).arg(dark_ ? "#2d2d34" : "#e6e5ec", dark_ ? "#ffffff" : "#1a1a1c"));
            } else {
                tabButtons_[i]->setStyleSheet(QString(
                    "QPushButton { background: transparent; color: %1; font-weight: 500; font-size: 13px; border: 0; border-radius: 6px; padding: 6px 14px; }"
                    "QPushButton:hover { background: %2; }"
                ).arg(dark_ ? "#9c9ba4" : "#6c6b75", dark_ ? "#24242a" : "#eeeef2"));
            }
        }
    }

    QWidget *createUploadTab() {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 4, 0, 0);
        layout->setSpacing(12);

        // Notification / Info Callout
        auto *infoBox = new QFrame(page);
        infoBox->setStyleSheet(QString(
            "QFrame { background: %1; border: 1px solid %2; border-radius: 8px; padding: 10px 14px; }"
        ).arg(dark_ ? "#232328" : "#f0f0f4", dark_ ? "#32323a" : "#dcdae2"));
        auto *infoLayout = new QHBoxLayout(infoBox);
        infoLayout->setContentsMargins(4, 2, 4, 2);
        infoLayout->setSpacing(10);

        auto *infoIcon = new QLabel("ⓘ", infoBox);
        infoIcon->setStyleSheet("font-size: 15px; font-weight: bold; color: #a5a4ac;");
        infoLayout->addWidget(infoIcon);

        auto *infoText = new QLabel(
            "Selecciona un solo archivo (.png, .jpeg, .jpg, .jpe, .gif, .svg, .svgz, .webp). "
            "El límite máximo de tamaño total de archivos es 100 MB.", infoBox);
        infoText->setWordWrap(true);
        infoText->setStyleSheet(QString("font-size: 12px; color: %1;").arg(dark_ ? "#bab9c2" : "#4a4952"));
        infoLayout->addWidget(infoText, 1);
        layout->addWidget(infoBox);

        // Drop Area
        auto *dropArea = new LumenImageDropZone(page, dark_);
        auto *dropLayout = new QVBoxLayout(dropArea);
        dropLayout->setContentsMargins(20, 20, 20, 20);
        dropLayout->addStretch(2);

        auto *centerIcon = new QLabel(dropArea);
        centerIcon->setAlignment(Qt::AlignCenter);
        centerIcon->setPixmap(LumenIcons::icon("file-upload", dark_ ? QColor("#85848e") : QColor("#92919d"), 36).pixmap(36, 36));
        dropLayout->addWidget(centerIcon);

        auto *centerLabel = new QLabel("Arrastrar y soltar", dropArea);
        centerLabel->setAlignment(Qt::AlignCenter);
        centerLabel->setStyleSheet(QString("font-size: 14px; font-weight: 500; color: %1; margin-top: 6px;").arg(dark_ ? "#a5a4ac" : "#6c6b75"));
        dropLayout->addWidget(centerLabel);

        statusLabel_ = new QLabel(dropArea);
        statusLabel_->setAlignment(Qt::AlignCenter);
        statusLabel_->setStyleSheet("font-size: 12px; color: #b19160; margin-top: 4px;");
        statusLabel_->hide();
        dropLayout->addWidget(statusLabel_);

        dropLayout->addStretch(1);

        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch();
        auto *selectBtn = new QPushButton("  Seleccionar archivo", dropArea);
        selectBtn->setIcon(LumenIcons::icon("file-text", QColor("#18181b"), 16));
        selectBtn->setCursor(Qt::PointingHandCursor);
        selectBtn->setStyleSheet(
            "QPushButton { background: #f5f4f6; color: #18181b; font-weight: 600; font-size: 13px; border: 0; border-radius: 6px; padding: 7px 16px; }"
            "QPushButton:hover { background: #ffffff; }"
        );
        connect(selectBtn, &QPushButton::clicked, this, [this] {
            QString path = QFileDialog::getOpenFileName(this, "Seleccionar archivo de imagen", QString(),
                "Imágenes (*.png *.jpg *.jpeg *.jpe *.gif *.svg *.svgz *.webp);;Todos los archivos (*)");
            if (!path.isEmpty()) uploadLocalPath(path);
        });
        btnRow->addWidget(selectBtn);
        dropLayout->addLayout(btnRow);

        dropArea->onFileDropped = [this](const QString &path) {
            uploadLocalPath(path);
        };
        dropArea->onDataDropped = [this](const QByteArray &bytes, const QString &filename, const QString &mime) {
            uploadRawBytes(bytes, filename, mime);
        };

        layout->addWidget(dropArea, 1);
        return page;
    }

    QWidget *createLinkTab() {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 10, 0, 0);
        layout->setSpacing(14);

        urlInput_ = new QLineEdit(page);
        urlInput_->setPlaceholderText("Pegar URL");
        urlInput_->setClearButtonEnabled(true);
        urlInput_->setStyleSheet(QString(
            "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px; padding: 9px 12px; font-size: 13px; color: %3; }"
            "QLineEdit:focus { border: 1px solid #a48baf; }"
        ).arg(dark_ ? "#222227" : "#ffffff", dark_ ? "#383842" : "#d0cfe0", dark_ ? "#ffffff" : "#1a1a1c"));

        layout->addWidget(urlInput_);

        auto *statusText = new QLabel(page);
        statusText->setStyleSheet("font-size: 12px; color: #b19160;");
        statusText->hide();
        layout->addWidget(statusText);

        auto *btnRow = new QHBoxLayout;
        btnRow->addStretch();
        urlSubmitBtn_ = new QPushButton("Añadir imagen", page);
        urlSubmitBtn_->setCursor(Qt::PointingHandCursor);
        urlSubmitBtn_->setStyleSheet(
            "QPushButton { background: #8e7aa9; color: #ffffff; font-weight: 600; font-size: 13px; border: 0; border-radius: 6px; padding: 7px 18px; }"
            "QPushButton:hover { background: #9d89b9; }"
            "QPushButton:disabled { background: #444450; color: #888892; }"
        );
        btnRow->addWidget(urlSubmitBtn_);
        layout->addLayout(btnRow);

        auto triggerFetch = [this, statusText] {
            QString urlStr = urlInput_->text().trimmed();
            if (urlStr.isEmpty()) return;
            QUrl url(urlStr);
            if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) {
                statusText->setText("Por favor, introduce una URL válida (http o https).");
                statusText->show();
                return;
            }
            statusText->setText("Descargando imagen desde URL…");
            statusText->show();
            urlSubmitBtn_->setEnabled(false);

            auto *nam = new QNetworkAccessManager(this);
            QNetworkRequest req(url);
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            req.setTransferTimeout(20000);
            auto *reply = nam->get(req);
            connect(reply, &QNetworkReply::finished, this, [this, reply, nam, statusText, url] {
                reply->deleteLater();
                nam->deleteLater();
                urlSubmitBtn_->setEnabled(true);
                if (reply->error() != QNetworkReply::NoError) {
                    statusText->setText("Error al descargar la imagen: " + reply->errorString());
                    return;
                }
                QByteArray bytes = reply->readAll();
                QImage img;
                if (!img.loadFromData(bytes)) {
                    statusText->setText("La URL no devolvió un formato de imagen compatible.");
                    return;
                }
                QString filename = QFileInfo(url.path()).fileName();
                if (filename.isEmpty()) filename = "imagen-web.jpg";
                QString mime = reply->header(QNetworkRequest::ContentTypeHeader).toString();
                if (mime.isEmpty() || !mime.startsWith("image/")) mime = "image/jpeg";
                statusText->setText("Subiendo imagen al espacio…");
                uploadRawBytes(bytes, filename, mime);
            });
        };

        connect(urlSubmitBtn_, &QPushButton::clicked, this, triggerFetch);
        connect(urlInput_, &QLineEdit::returnPressed, this, triggerFetch);

        layout->addStretch();
        return page;
    }

    QWidget *createUnsplashTab() {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 10, 0, 0);
        layout->setSpacing(12);

        auto *search = new QLineEdit(page);
        search->setPlaceholderText("Buscar en Unsplash…");
        search->setClearButtonEnabled(true);
        search->setStyleSheet(QString(
            "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px; padding: 8px 12px; font-size: 13px; color: %3; }"
        ).arg(dark_ ? "#222227" : "#ffffff", dark_ ? "#383842" : "#d0cfe0", dark_ ? "#ffffff" : "#1a1a1c"));
        layout->addWidget(search);

        auto *scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto *gridHost = new QWidget;
        auto *grid = new QGridLayout(gridHost);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(10);

        struct UnsplashSample { QString label; QString url; QString thumbColor; };
        const std::vector<UnsplashSample> samples = {
            {"Lectura & Libros", "https://images.unsplash.com/photo-1544716278-ca5e3f4abd8c?w=1200&q=80", "#8b6f57"},
            {"Biblioteca clásica", "https://images.unsplash.com/photo-1507842229451-7f01be7f7443?w=1200&q=80", "#4a5358"},
            {"Páginas abiertas", "https://images.unsplash.com/photo-1457369804613-52c61a468e7d?w=1200&q=80", "#786c5f"},
            {"Arquitectura minimal", "https://images.unsplash.com/photo-1486406146926-c627a92ad1ab?w=1200&q=80", "#5c6d70"},
            {"Naturaleza & Calma", "https://images.unsplash.com/photo-1518495973542-4542c06a5843?w=1200&q=80", "#5b7553"},
            {"Degradado etéreo", "https://images.unsplash.com/photo-1579546929518-9e396f3cc809?w=1200&q=80", "#7b618b"}
        };

        for (size_t i = 0; i < samples.size(); ++i) {
            auto *card = new QFrame(gridHost);
            card->setCursor(Qt::PointingHandCursor);
            card->setStyleSheet(QString(
                "QFrame { background: %1; border-radius: 8px; border: 1px solid %2; min-height: 85px; }"
                "QFrame:hover { border: 2px solid #a48baf; }"
            ).arg(samples[i].thumbColor, dark_ ? "#383842" : "#d0cfe0"));
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->addStretch();
            auto *lbl = new QLabel(samples[i].label, card);
            lbl->setStyleSheet("color: #ffffff; font-weight: 600; font-size: 13px; text-shadow: 0 1px 3px rgba(0,0,0,0.8); padding: 4px;");
            cardLayout->addWidget(lbl);

            QString targetUrl = samples[i].url;
            card->installEventFilter(this);
            // Clicking card downloads the Unsplash photo
            auto *btn = new QPushButton(card);
            btn->setStyleSheet("background: transparent; border: 0;");
            btn->resize(card->size());
            connect(btn, &QPushButton::clicked, this, [this, targetUrl] {
                if (urlInput_) urlInput_->setText(targetUrl);
                switchTab(1);
                if (urlSubmitBtn_) urlSubmitBtn_->click();
            });
            grid->addWidget(card, int(i / 2), int(i % 2));
        }

        scroll->setWidget(gridHost);
        layout->addWidget(scroll, 1);
        return page;
    }

    QWidget *createExistingTab() {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 6, 0, 0);
        layout->setSpacing(10);

        // Filter / Search Bar
        auto *filterRow = new QHBoxLayout;
        filterRow->setSpacing(8);

        auto *search = new QLineEdit(page);
        search->setPlaceholderText("Buscar imagen…");
        search->setClearButtonEnabled(true);
        search->setStyleSheet(QString(
            "QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px; padding: 7px 10px; font-size: 13px; color: %3; }"
        ).arg(dark_ ? "#222227" : "#ffffff", dark_ ? "#383842" : "#d0cfe0", dark_ ? "#ffffff" : "#1a1a1c"));
        filterRow->addWidget(search, 1);

        auto *tagBtn = new QPushButton("Etiquetas", page);
        tagBtn->setIcon(LumenIcons::icon("tag", dark_ ? QColor("#a5a4ac") : QColor("#686772"), 16));
        tagBtn->setStyleSheet(QString(
            "QPushButton { background: %1; color: %2; font-size: 12px; border: 1px solid %3; border-radius: 6px; padding: 6px 12px; }"
            "QPushButton:hover { background: %4; }"
        ).arg(dark_ ? "#222227" : "#ffffff", dark_ ? "#a5a4ac" : "#4a4952", dark_ ? "#383842" : "#d0cfe0", dark_ ? "#2c2c34" : "#f1eff4"));
        filterRow->addWidget(tagBtn);
        layout->addLayout(filterRow);

        // Scan existing assets from objects
        struct ExistingAsset {
            QString assetId;
            QString title;
        };
        std::vector<ExistingAsset> existing;
        std::set<QString> seen;

        for (const auto &val : objects_) {
            auto obj = val.toObject();
            const QString typeId = obj.value("type_id").toString();
            const auto props = obj.value("properties").toObject();
            const QString objTitle = obj.value("title").toString();

            if (typeId == "image") {
                QString aId = props.value("image_asset_id").toString();
                if (aId.isEmpty()) aId = props.value("asset_id").toString();
                if (!aId.isEmpty() && seen.insert(aId).second) {
                    existing.push_back({aId, objTitle.isEmpty() ? "Imagen" : objTitle});
                }
            }
            QString coverId = props.value("image_asset_id").toString();
            if (!coverId.isEmpty() && seen.insert(coverId).second) {
                existing.push_back({coverId, objTitle.isEmpty() ? "Portada" : objTitle});
            }
            for (auto av : props.value("assets").toArray()) {
                auto a = av.toObject();
                if (a.value("mime").toString().startsWith("image/")) {
                    QString aId = a.value("id").toString();
                    if (!aId.isEmpty() && seen.insert(aId).second) {
                        existing.push_back({aId, a.value("name").toString("Imagen adjunta")});
                    }
                }
            }
        }

        auto *scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);

        auto *gridHost = new QWidget;
        auto *grid = new QGridLayout(gridHost);
        grid->setContentsMargins(0, 4, 0, 0);
        grid->setSpacing(10);

        if (existing.empty()) {
            auto *empty = new QLabel("No hay imágenes existentes en este espacio.", gridHost);
            empty->setAlignment(Qt::AlignCenter);
            empty->setStyleSheet(QString("color: %1; padding: 40px; font-size: 13px;").arg(dark_ ? "#888892" : "#76757f"));
            grid->addWidget(empty, 0, 0);
        } else {
            int col = 0, row = 0;
            const int maxCols = 3;
            for (const auto &item : existing) {
                auto *card = new QFrame(gridHost);
                card->setCursor(Qt::PointingHandCursor);
                card->setFixedSize(160, 130);
                card->setStyleSheet(QString(
                    "QFrame { background: %1; border-radius: 8px; border: 1px solid %2; }"
                    "QFrame:hover { border: 2px solid #a48baf; background: %3; }"
                ).arg(dark_ ? "#222227" : "#ffffff", dark_ ? "#383842" : "#d0cfe0", dark_ ? "#292930" : "#f5f4f8"));

                auto *cl = new QVBoxLayout(card);
                cl->setContentsMargins(6, 6, 6, 6);
                cl->setSpacing(4);

                auto *thumb = new QLabel(card);
                thumb->setAlignment(Qt::AlignCenter);
                thumb->setFixedHeight(90);
                thumb->setStyleSheet(QString("background: %1; border-radius: 6px;").arg(dark_ ? "#161618" : "#eae9ef"));
                cl->addWidget(thumb);

                auto *lbl = new QLabel(item.title, card);
                lbl->setAlignment(Qt::AlignCenter);
                lbl->setStyleSheet("font-size: 11px; color: #a5a4ac;");
                cl->addWidget(lbl);

                QPointer<QLabel> guard = thumb;
                const QString assetId = item.assetId;
                if (byteFetcher_) {
                    byteFetcher_("/v1/assets/" + assetId + "/content", [guard](QByteArray data, QString err) {
                        if (!guard || !err.isEmpty() || data.isEmpty()) return;
                        QPixmap px;
                        if (px.loadFromData(data)) {
                            guard->setPixmap(px.scaled(guard->size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                        }
                    });
                }

                auto *btn = new QPushButton(card);
                btn->setStyleSheet("background: transparent; border: 0;");
                btn->resize(card->size());
                connect(btn, &QPushButton::clicked, this, [this, assetId] {
                    if (onSelected_) onSelected_(assetId);
                    accept();
                });

                grid->addWidget(card, row, col);
                ++col;
                if (col >= maxCols) { col = 0; ++row; }
            }
        }

        scroll->setWidget(gridHost);
        layout->addWidget(scroll, 1);
        return page;
    }

    void uploadLocalPath(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            if (statusLabel_) {
                statusLabel_->setText("No se pudo abrir el archivo seleccionado.");
                statusLabel_->show();
            }
            return;
        }
        if (file.size() > 100 * 1024 * 1024) {
            if (statusLabel_) {
                statusLabel_->setText("El archivo supera el límite de 100 MB.");
                statusLabel_->show();
            }
            return;
        }
        QByteArray bytes = file.readAll();
        QString filename = QFileInfo(path).fileName();
        QString mime = "image/png";
        if (filename.endsWith(".jpg", Qt::CaseInsensitive) || filename.endsWith(".jpeg", Qt::CaseInsensitive))
            mime = "image/jpeg";
        else if (filename.endsWith(".webp", Qt::CaseInsensitive))
            mime = "image/webp";
        else if (filename.endsWith(".gif", Qt::CaseInsensitive))
            mime = "image/gif";
        else if (filename.endsWith(".svg", Qt::CaseInsensitive))
            mime = "image/svg+xml";

        uploadRawBytes(bytes, filename, mime);
    }

    void uploadRawBytes(const QByteArray &bytes, const QString &filename, const QString &mime) {
        if (statusLabel_) {
            statusLabel_->setText("Subiendo imagen…");
            statusLabel_->show();
        }
        if (!uploader_) return;
        uploader_("", filename, bytes, mime, [this](QJsonValue val, QString err) {
            if (!err.isEmpty()) {
                if (statusLabel_) {
                    statusLabel_->setText("Error al subir: " + err);
                    statusLabel_->show();
                }
                return;
            }
            const auto res = val.toObject();
            const QString assetId = res.value("id").toString();
            if (onSelected_) onSelected_(assetId);
            accept();
        });
    }
};
