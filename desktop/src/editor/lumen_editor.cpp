#include "lumen_editor.hpp"
#include "lumen_document.hpp"
#include "lumen_clipboard.hpp"
#include "lumen_table_controls.hpp"
#include "lumen_aliases.hpp"
#include "lumen_icons.hpp"
#include <QClipboard>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QUuid>
#include <QTimeZone>
#include <algorithm>

namespace {
constexpr int BlockIdProperty = QTextFormat::UserProperty + 701;
constexpr int ReferenceTypeProperty = QTextFormat::UserProperty + 702;
constexpr int EmbedBlockProperty = QTextFormat::UserProperty + 703;
constexpr int ReferenceIconProperty = QTextFormat::UserProperty + 704;
constexpr int BlockStyleProperty = LumenSyntax::BlockStyleProperty;
constexpr int CodeLanguageProperty = LumenSyntax::CodeLanguageProperty;
constexpr int MathLatexProperty = QTextFormat::UserProperty + 707;
constexpr int MathDisplayProperty = QTextFormat::UserProperty + 708;
constexpr int GroupBlockProperty = QTextFormat::UserProperty + 709;
constexpr int NativeFrameProperty = QTextFormat::UserProperty + 712;
constexpr int LayoutBoundaryProperty = QTextFormat::UserProperty + 713;
constexpr int NativeBlockProperty = QTextFormat::UserProperty + 714;
constexpr int NativeRunProperty = QTextFormat::UserProperty + 715;
constexpr int EmptyDocumentProperty = QTextFormat::UserProperty + 717;
constexpr int CollapsedFrameStyleProperty = QTextFormat::UserProperty + 716;
// Per-block style colors chosen from the handle menu "Style" submenu. Stored
// as "#rrggbb" strings on the block format and serialized as additive
// "text_color" / "background_color" block keys (contract adoption: follow-up).
constexpr int BlockTextColorProperty = QTextFormat::UserProperty + 710;
constexpr int BlockBackgroundProperty = QTextFormat::UserProperty + 711;
QString value(const QJsonObject &o, const char *key) { return o.value(key).toString(); }
QColor groupBoxColor(bool dark) { return QColor(dark ? "#222228" : "#f4f4f7"); }
// Small fixed palette derived from the app theme presets: muted gray, the
// accent purple, the standard type colors (blue/green/gold) and their dark
// counterparts. Backgrounds are the existing chip/surface tints.
struct StyleSwatch { const char *name; const char *light; const char *dark; };
const QList<StyleSwatch> &textColorPalette() {
    static const QList<StyleSwatch> palette{
        {"Gray", "#75747b", "#919097"},   {"Red", "#c05050", "#f2a0a0"},
        {"Gold", "#b99854", "#e6c98f"},    {"Green", "#6d9b7e", "#9ccdb0"},
        {"Blue", "#7294ca", "#9dbdf2"},    {"Purple", "#73519b", "#d2b7f5"},
        {"Pink", "#c05f8e", "#eab7d0"},
    };
    return palette;
}
const QList<StyleSwatch> &backgroundPalette() {
    static const QList<StyleSwatch> palette{
        {"Gray", "#ececf0", "#232327"},   {"Red", "#f9e9e9", "#3f2a2a"},
        {"Gold", "#f7f0df", "#3a3226"},    {"Green", "#e7f0e9", "#23322a"},
        {"Blue", "#e8eef7", "#26303f"},    {"Purple", "#f2edf8", "#2d2638"},
        {"Pink", "#f7e9f2", "#3a2a35"},
    };
    return palette;
}
QColor swatchColor(const StyleSwatch &swatch, bool dark) { return QColor(dark ? swatch.dark : swatch.light); }
QIcon swatchIcon(const QColor &color, bool checked) {
    QPixmap pixmap(14, 14);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setPen(QColor(checked ? "#303035" : "#b9b9c2"));
    painter.drawRect(0, 0, 13, 13);
    painter.end();
    return QIcon(pixmap);
}
// Recomputes the background a block should show once explicit style colors
// are gone: the user palette wins, then the group box, then the type default.
QColor recomputedBlockBackground(const QTextBlockFormat &format, bool dark) {
    const QString user = format.property(BlockBackgroundProperty).toString();
    if (!user.isEmpty()) return QColor(user);
    const QString style = format.property(BlockStyleProperty).toString();
    if (!format.property(GroupBlockProperty).toString().isEmpty()) return groupBoxColor(dark);
    if (style == "group") return groupBoxColor(dark);
    if (style == "code") return QColor(dark ? "#1b1b20" : "#f1f1f4");
    if (format.property(EmbedBlockProperty).toBool()) return QColor(dark ? "#1c1c20" : "#f4f4f5");
    return {};
}
// Restores the intrinsic margins of a block that just left a group box, so a
// quote stays a quote and a divider keeps its full-width line.
void restoreUngroupedMargins(const QTextBlock &block, bool dark) {
    const QString style = block.blockFormat().property(BlockStyleProperty).toString();
    auto format = block.blockFormat();
    format.clearProperty(GroupBlockProperty);
    if (style == "divider") {
        format.setLeftMargin(0); format.setRightMargin(0);
        format.setTopMargin(12); format.setBottomMargin(12);
    } else if (style == "quote") {
        format.setLeftMargin(22); format.setRightMargin(10); format.setTopMargin(0); format.setBottomMargin(10);
    } else if (style == "group") {
        format.setLeftMargin(16); format.setRightMargin(16); format.setTopMargin(8); format.setBottomMargin(8);
    } else if (style == "toggle") {
        format.setLeftMargin(0); format.setTopMargin(0); format.setBottomMargin(6);
    } else if (style == "todo") {
        format.setLeftMargin(0); format.setTopMargin(3); format.setBottomMargin(4);
    } else {
        format.setLeftMargin(0); format.setRightMargin(0); format.setTopMargin(0); format.setBottomMargin(10);
    }
    const QColor restored = recomputedBlockBackground(format, dark);
    if (restored.isValid()) format.setBackground(restored);
    else format.clearBackground();
    QTextCursor cursor(block);
    cursor.setBlockFormat(format);
}
// Object and block references render as read-only chips: find counts their
// title but replace must not rewrite the link text.
bool isReferenceChipFormat(const QTextCharFormat &format) {
    if (!format.isAnchor()) return false;
    const QUrl url(format.anchorHref());
    return url.scheme() == "lumen" && (url.host() == "object" || url.host() == "block");
}
QIcon actionIcon(const QString &name) {
    const auto path = ":/icons/" + name + ".svg";
    return !name.isEmpty() && QFile::exists(path) ? LumenIcons::icon(name) : QIcon();
}
QIcon stableIcon(const QIcon &source, int size) {
    QIcon result;
    const auto pixmap = source.pixmap(size, size);
    for (auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected, QIcon::Disabled})
        result.addPixmap(pixmap, mode);
    return result;
}
// Los prefijos de tarea/toggle son parte del texto del bloque. Al cambiar de
// tipo hay que retirarlos; si no, la casilla o el triángulo sobreviven al
// encabezado o a la viñeta y el bloque muestra dos marcas.
QString blockMarker(const QString &text) {
    static const QStringList markers{
        QString::fromUtf8("○ "), QString::fromUtf8("✔ "),
        QString::fromUtf8("▶ "), QString::fromUtf8("▼ "),
        QStringLiteral("[ ] "), QStringLiteral("[x] "), QStringLiteral("[X] ")};
    for (const auto &marker : markers)
        if (text.startsWith(marker)) return marker;
    return {};
}
bool isTodoMarker(const QString &marker) {
    return marker == QString::fromUtf8("○ ") || marker == QString::fromUtf8("✔ ") ||
           marker == QStringLiteral("[ ] ") || marker == QStringLiteral("[x] ") ||
           marker == QStringLiteral("[X] ");
}
bool isToggleMarker(const QString &marker) {
    return marker == QString::fromUtf8("▶ ") || marker == QString::fromUtf8("▼ ");
}
struct EditorReferenceSpan {
    QString id, type, text;
    int start = 0, end = 0;
};
QList<EditorReferenceSpan> referenceSpans(const QTextDocument *document) {
    QList<EditorReferenceSpan> spans;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto format = fragment.charFormat();
            const QUrl url(format.anchorHref());
            if (!format.isAnchor() || url.scheme() != "lumen" || url.host() != "object") continue;
            const auto id = url.path().mid(1);
            if (id.isEmpty() || id.contains('/')) continue;
            const auto type = format.property(ReferenceTypeProperty).toString();
            if (!spans.isEmpty() && spans.last().id == id && spans.last().end == fragment.position()) {
                spans.last().text += fragment.text();
                spans.last().end += fragment.length();
                if (spans.last().type.isEmpty()) spans.last().type = type;
            } else spans.append({id, type, fragment.text(), fragment.position(), fragment.position() + fragment.length()});
        }
    }
    return spans;
}
QToolButton *tool(const QString &title, const QString &iconName, QWidget *parent) {
    auto *button = new QToolButton(parent);
    button->setText(title);
    button->setToolTip(title);
    button->setAccessibleName(title);
    button->setIcon(actionIcon(iconName));
    button->setCursor(Qt::PointingHandCursor);
    button->setAutoRaise(true);
    button->setMinimumSize(30, 30);
    if (button->icon().isNull()) button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}
}

PrivateDocument::PrivateDocument(RichEditor *editor)
    : QTextDocument(editor), editor_(editor) {}

QVariant PrivateDocument::loadResource(int type, const QUrl &name) {
    if (editor_ && editor_->resourceProvider) {
        auto res = editor_->resourceProvider(type, name);
        if (res.isValid() && !res.isNull()) return res;
    }
    if (editor_) {
        auto res = editor_->resolveDefaultResource(type, name);
        if (res.isValid() && !res.isNull()) return res;
    }
    if (type == QTextDocument::ImageResource && name.scheme() == "qrc") {
        return QTextDocument::loadResource(type, name);
    }
    return {};
}

QVariant RichEditor::resolveDefaultResource(int type, const QUrl &name) const {
    if (type == QTextDocument::ImageResource) {
        if (name.scheme() == "lumen-rule" && name.host() == "divider") {
            const bool isDark = name.path().contains("dark") || (name.path().isEmpty() && dark_);
            QPixmap linePix(680, 2);
            linePix.fill(QColor(isDark ? "#383840" : "#dcdcde"));
            return linePix;
        }
        if (name.scheme() == "lumen-icon" && name.host() == "type") {
            const auto parts = name.path().split('/', Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                const QString tId = parts.value(0);
                return typeIcon(tId, 18).pixmap(18, 18).toImage();
            }
        }
    }
    return {};
}

RichEditor::RichEditor(QWidget *parent) : QTextEdit(parent) {
    setDocument(new PrivateDocument(this));
    setAcceptRichText(false);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    document()->setDocumentMargin(32);
    // Las listas de Qt sangran 40 px por nivel por defecto; con 16 px el texto
    // de viñetas y tareas queda al ras del párrafo, como en la referencia.
    document()->setIndentWidth(16);
    {
        QTextOption opt = document()->defaultTextOption();
        opt.setTabStopDistance(24);
        document()->setDefaultTextOption(opt);
    }
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    blockHandle_ = new QToolButton(viewport());
    blockHandle_->setObjectName("blockHandle");
    blockHandle_->setIcon(LumenIcons::icon("grip-vertical", QColor(dark_ ? "#80808a" : "#9999a3"), 16));
    blockHandle_->setFixedSize(18, 22);
    blockHandle_->setCursor(Qt::OpenHandCursor);
    blockHandle_->setToolTip("Opciones de bloque · Arrastrar para mover o crear columnas");
    blockHandle_->setStyleSheet("QToolButton#blockHandle{background:transparent;border:0;border-radius:4px;padding:1px;}"
                                "QToolButton#blockHandle:hover{background:rgba(128,128,128,0.25);}");
    blockHandle_->installEventFilter(this);
    blockHandle_->hide();
    setAccessibleName("Editor de notas por bloques");
    setPlaceholderText("Escribe lo que tienes en mente…\n\nUsa @ para enlazar o crear un objeto. / abre los comandos.");
    idTimer_.setSingleShot(true);
    toolbarTimer_.setSingleShot(true);
    connect(&idTimer_, &QTimer::timeout, this, [this] {
        ensureBlockIds();
        if (toggleLayoutPending_) {
            toggleLayoutPending_ = false;
            // Undo finishes invalidating text layouts after contentsChanged.
            // Reflow on the next event-loop turn, once the transaction is complete.
            const auto size = document()->pageSize();
            document()->setPageSize(QSizeF(size.width() + 1, size.height()));
            document()->setPageSize(size);
            document()->markContentsDirty(0, document()->characterCount());
            viewport()->update();
        }
    });
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (!normalizing_) { idTimer_.start(0); refreshToggleVisibility(); }
    });
    connect(this, &QTextEdit::selectionChanged, this, [this] { toolbarTimer_.start(90); });
    connect(&toolbarTimer_, &QTimer::timeout, this, [this] { updateSelectionToolbar(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (selectionToolbar_ && selectionToolbar_->isVisible())
            positionPopup(selectionToolbar_, selectionCursor_, true);
        if (referencePopup_) positionPopup(referencePopup_, queryCursor_);
        if (blockPopup_) positionPopup(blockPopup_, queryCursor_, true);
        if (blockHandle_ && activeHandleBlock_.isValid()) {
            positionBlockHandle();
        }
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
        updateInlineQuery();
        updateCodeBar(); updateImageBar();
        if (!isReadOnly() && textCursor().block().isValid()) {
            activeHandleBlock_ = textCursor().block();
            if (blockHandle_) {
                positionBlockHandle();
                blockHandle_->show();
            }
        }
    });
    connect(document(), &QTextDocument::contentsChanged, this, [this] { updateCodeBar(); updateImageBar(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { updateCodeBar(); updateImageBar(); });
    highlighter_ = new LumenSyntaxHighlighter(document());
    // Live counter while the find bar is open: document edits (typing,
    // replace, reference reconciliation) rescan after the current event loop
    // turn.
    findRefreshTimer_.setSingleShot(true);
    connect(&findRefreshTimer_, &QTimer::timeout, this, [this] {
        if (!findBarVisible() || findUpdating_) return;
        refreshFindMatches(false);
    });
    // focusBlock() flashes a block through the same setExtraSelections() API
    // the find highlights use; keep the flash in its own list and rebuild the
    // union so neither feature clobbers the other.
    flashTimer_.setSingleShot(true);
    connect(&flashTimer_, &QTimer::timeout, this, [this] {
        flashSelections_.clear();
        applyExtraSelections();
    });
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (!findBarVisible() || findUpdating_) return;
        findRefreshTimer_.start(0);
    });
    ensureBlockIds();
    LumenTableControls::install(this);
}
RichEditor::~RichEditor() {
    if (findQueryEdit_) findQueryEdit_->removeEventFilter(this);
    if (findReplaceEdit_) findReplaceEdit_->removeEventFilter(this);
    if (findBarHost_) findBarHost_->removeEventFilter(this);
    if (findBar_) {
        findBar_->hide();
        delete findBar_.data();
    }
}

void RichEditor::setAppearance(bool dark, bool reducedMotion) {
    dark_ = dark;
    reducedMotion_ = reducedMotion;
    if (referencePopup_) styleLayer(referencePopup_);
    if (blockPopup_) styleLayer(blockPopup_);
    if (selectionToolbar_) styleLayer(selectionToolbar_);
    if (linkPopup_) styleLayer(linkPopup_);
    for (auto block = document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto format = it.fragment().charFormat();
            if (!format.property(ReferenceIconProperty).toBool() || !format.isImageFormat()) continue;
            document()->addResource(QTextDocument::ImageResource, QUrl(format.toImageFormat().name()),
                typeIcon(format.property(ReferenceTypeProperty).toString(), 18).pixmap(18, 18).toImage());
        }
    if (mathPopup_) styleLayer(mathPopup_);
    if (codeBar_) styleLayer(codeBar_);
    if (imageBar_) styleLayer(imageBar_);
    if (findBar_) {
        styleLayer(findBar_);
        updateFindBarIcons();
        positionFindBar();
        rebuildFindSelections();
    }
    if (blockHandle_) {
        blockHandle_->setIcon(LumenIcons::icon("grip-vertical", QColor(dark_ ? "#80808a" : "#9999a3"), 16));
    }
    const bool themeChanged = highlighter_ && highlighter_->property("dark").toBool() != dark;
    if (highlighter_) { highlighter_->setProperty("dark", dark); highlighter_->setDark(dark); }
    if (themeChanged) {
        // Code and group backgrounds are part of the block format; refresh them with the theme.
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
            const QString bStyle = block.blockFormat().property(BlockStyleProperty).toString();
            if (bStyle == "code") {
                QTextCursor cursor(block);
                auto format = block.blockFormat();
                format.setBackground(QColor(dark_ ? "#1b1b20" : "#f1f1f4"));
                cursor.setBlockFormat(format);
            } else if (bStyle == "group") {
                QTextCursor cursor(block);
                auto format = block.blockFormat();
                format.setBackground(QColor(dark_ ? "#222228" : "#f4f4f7"));
                cursor.setBlockFormat(format);
            } else if (!block.blockFormat().property(GroupBlockProperty).toString().isEmpty()) {
                // Multi-block group members keep the shared box across themes.
                QTextCursor cursor(block);
                auto format = block.blockFormat();
                const QColor background = recomputedBlockBackground(format, dark_);
                if (background.isValid()) format.setBackground(background);
                else format.clearBackground();
                cursor.setBlockFormat(format);
            }
        }
    }
    refreshMath();
    document()->markContentsDirty(0, document()->characterCount());
}
void RichEditor::styleLayer(QWidget *layer) {
    const QString background = dark_ ? "#1c1c20" : "#fcfcfc";
    const QString foreground = dark_ ? "#e4e3e2" : "#303034";
    const QString border = dark_ ? "#29292e" : "#e3e3e7";
    const QString hover = dark_ ? "#2b2b2e" : "#f4f4f5";
    auto palette = layer->palette();
    for (auto role : {QPalette::Window, QPalette::Base, QPalette::AlternateBase, QPalette::Button}) palette.setColor(role, QColor(background));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) palette.setColor(role, QColor(foreground));
    palette.setColor(QPalette::Highlight, QColor("#7060c0")); palette.setColor(QPalette::HighlightedText, Qt::white);
    layer->setPalette(palette); layer->setAutoFillBackground(true);
    layer->setStyleSheet(QString(
        "QFrame#editorLayer,QFrame#imageBlockToolbar,QDialog#temporalPicker,QDialog#blockReferencePicker{background:%1;color:%2;border:1px solid %3;border-radius:9px;}"
        "QFrame#findBar{background:%1;color:%2;border:1px solid %3;border-radius:9px;}"
        "QLabel,QCheckBox{color:%2;background:transparent;border:0;}"
        "QLineEdit,QComboBox,QDateEdit,QTimeEdit{background:%1;color:%2;border:1px solid %3;border-radius:5px;padding:7px;}"
        "QLineEdit:focus,QComboBox:focus{border:1px solid #b499d4;}"
        "QListWidget{background:%1;color:%2;border:0;outline:none;}"
        "QListWidget::item{padding:4px 8px;border-radius:10px;}"
        "QListWidget#referenceResults,QListWidget#blockResults{font-size:16px;}"
        "QLabel#inlineMenuHint{color:#969397;font-size:13px;border-top:1px solid %3;}"
        "QLabel#inlineMenuHeading{color:#969397;font-size:14px;}"
        "QLabel#findCounter{color:#969397;font-size:12px;}"
        "QLabel#findNotice{color:#969397;font-size:12px;}"
        "QScrollBar:vertical{background:transparent;width:7px;margin:3px 0;}"
        "QScrollBar::handle:vertical{background:#3b3b40;border-radius:3px;min-height:24px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QListWidget::item:selected,QListWidget::item:hover{background:%4;color:%2;}"
        "QCalendarWidget QWidget#qt_calendar_navigationbar{background:%1;}"
        "QCalendarWidget QAbstractItemView{background:%1;color:%2;selection-background-color:%4;selection-color:%2;border:0;outline:none;}"
        "QCalendarWidget QSpinBox{background:%1;color:%2;border:0;}"
        "QToolButton,QPushButton{background:transparent;color:%2;border:0;border-radius:5px;padding:5px;}"
        "QToolButton:hover,QPushButton:hover{background:%4;}"
        "QToolButton:focus,QPushButton:focus{border:1px solid #b499d4;}"
    ).arg(background, foreground, border, hover));
    for (auto *calendar : layer->findChildren<QCalendarWidget *>()) {
        auto colors = calendar->palette();
        colors.setColor(QPalette::Window, QColor(background)); colors.setColor(QPalette::Base, QColor(background));
        colors.setColor(QPalette::AlternateBase, QColor(background)); colors.setColor(QPalette::Button, QColor(background));
        colors.setColor(QPalette::Text, QColor(foreground)); colors.setColor(QPalette::WindowText, QColor(foreground));
        colors.setColor(QPalette::ButtonText, QColor(foreground)); colors.setColor(QPalette::Highlight, QColor(hover));
        colors.setColor(QPalette::HighlightedText, QColor(foreground));
        calendar->setPalette(colors);
        calendar->setLocale(QLocale(QLocale::Spanish, QLocale::Spain));
        QTextCharFormat header;
        header.setBackground(QColor(background)); header.setForeground(QColor(dark_ ? "#aaa7af" : "#67646c"));
        calendar->setHeaderTextFormat(header);
        QTextCharFormat weekend; weekend.setForeground(QColor(dark_ ? "#aaa7af" : "#67646c"));
        calendar->setWeekdayTextFormat(Qt::Saturday, weekend); calendar->setWeekdayTextFormat(Qt::Sunday, weekend);
        if (auto *previous = calendar->findChild<QToolButton *>("qt_calendar_prevmonth")) previous->setIcon(actionIcon("chevron-left"));
        if (auto *next = calendar->findChild<QToolButton *>("qt_calendar_nextmonth")) next->setIcon(actionIcon("chevron-right"));
    }
}
QIcon RichEditor::typeIcon(const QString &id, int size) const {
    QJsonObject type;
    for (const auto &entry : types_) if (value(entry.toObject(), "id") == id) { type = entry.toObject(); break; }
    if (value(type, "icon").isEmpty()) type.insert("icon", id == "book" ? "book" : id == "person" ? "user"
        : id == "journal" ? "calendar" : id == "web" ? "world" : id == "image" ? "photo"
        : id == "pdf" ? "file-type-pdf" : id == "project" ? "box" : "bulb");
    if (value(type, "color").isEmpty()) type.insert("color", id == "book" ? "#9e78bc"
        : id == "journal" || id == "web" ? "#7294ca" : id == "project" ? "#6d9b7e" : "#b99854");
    return LumenIcons::typeIcon(type, size, dark_);
}
QString RichEditor::typeLabel(const QString &id) const {
    if (id == "journal") return "Fecha";
    for (const auto &entry : types_) {
        const auto type = entry.toObject();
        if (value(type, "id") == id) {
            auto title = value(type, "name");
            if (title.isEmpty()) title = value(type, "title");
            return title.isEmpty() ? id : title;
        }
    }
    return id;
}
void RichEditor::setPlainText(const QString &text) {
    ++contentGeneration_; closeInlineMenu(); QTextEdit::setPlainText(text);
}
void RichEditor::setHtml(const QString &text) {
    ++contentGeneration_; closeInlineMenu(); QTextEdit::setHtml(text);
}
void RichEditor::clear() {
    ++contentGeneration_; closeInlineMenu(); QTextEdit::clear();
}
void RichEditor::setRecentIds(const QStringList &ids) {
    recentIds_ = ids;
    if (referencePopup_) rebuildReferences();
}
void RichEditor::setReferenceData(const QJsonArray &objects, const QJsonArray &types) {
    objects_ = objects;
    types_ = types;
    referenceDataSet_ = true;
    if (referencePopup_ && referencePopup_->isVisible()) rebuildReferences();
}
void RichEditor::positionPopup(QWidget *popup, const QTextCursor &cursor, bool above) {
    if (!popup) return;
    QRect caret = cursorRect(cursor);
    QPoint point = viewport()->mapToGlobal(above ? caret.topLeft() : caret.bottomLeft());
    if (above) point.ry() -= popup->height() + 8;
    else point.ry() += 6;
    auto *screen = this->screen();
    const QRect bounds = popup->isWindow()
        ? (screen ? screen->availableGeometry() : QRect(point, popup->size()))
        : QRect(viewport()->mapToGlobal(QPoint(0, 0)), viewport()->size());
    if (above && point.y() < bounds.top() + 8)
        point.setY(viewport()->mapToGlobal(caret.bottomLeft()).y() + 8);
    if (point.y() + popup->height() > bounds.bottom())
        point.setY(viewport()->mapToGlobal(caret.topLeft()).y() - popup->height() - 8);
    point.setX(qBound(bounds.left() + 8, point.x(), qMax(bounds.left() + 8, bounds.right() - popup->width() - 8)));
    point.setY(qBound(bounds.top() + 8, point.y(), qMax(bounds.top() + 8, bounds.bottom() - popup->height() - 8)));
    if (popup->isWindow()) popup->move(point);
    else popup->move(mapFromGlobal(point));
}
void RichEditor::beginInlineQuery(const QString &trigger) {
    closeInlineMenu();
    updatingPicker_ = true;
    auto cursor = textCursor();
    // Keep the trigger beside the caret; the editor continues receiving every key.
    if (cursor.hasSelection()) cursor.clearSelection();
    queryCursor_ = cursor;
    queryCursor_.setKeepPositionOnInsert(true);
    const int start = cursor.position();
    cursor.insertText(trigger, QTextCharFormat());
    queryCursor_.setPosition(start); // QTextCursor anchors also track insertion at their position.
    setTextCursor(cursor);
    referenceCursor_ = cursor;
    inlineQuery_.clear();
    updatingPicker_ = false;
}
QFrame *RichEditor::makeInlinePanel(const QString &accessibleName, const QString &kind, bool heading) {
    auto *popup = new QFrame(this, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    popup->setObjectName("editorLayer");
    popup->setProperty("editorMenuKind", kind);
    popup->setAttribute(Qt::WA_ShowWithoutActivating);
    popup->setFocusPolicy(Qt::NoFocus);
    popup->setAccessibleName(accessibleName);
    popup->setFixedSize(430, 386);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(7, heading ? 9 : 7, 7, 0);
    layout->setSpacing(0);
    if (heading) {
        auto *title = new QLabel("Crear un bloque", popup);
        title->setObjectName("inlineMenuHeading");
        title->setContentsMargins(5, 1, 5, 8);
        layout->addWidget(title);
    } else {
        auto *title = new QLabel(popup);
        referenceHeading_ = title;
        title->setObjectName("inlineMenuHeading");
        title->setContentsMargins(5, 1, 5, 8);
        title->hide();
        auto *back = tool("Volver a todos los tipos · ←", "chevron-left", popup);
        referenceBack_ = back;
        back->setFocusPolicy(Qt::NoFocus); back->setFixedSize(26, 28); back->hide();
        connect(back, &QToolButton::clicked, this, [this] { referenceTypeId_.clear(); rebuildReferences(); });
        auto *headingRow = new QHBoxLayout;
        headingRow->setContentsMargins(0, 0, 0, 0); headingRow->setSpacing(2);
        headingRow->addWidget(back); headingRow->addWidget(title, 1);
        layout->addLayout(headingRow);
    }
    auto *list = new QListWidget(popup);
    list->setFocusPolicy(Qt::NoFocus);
    list->setIconSize(QSize(30, 30));
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setTextElideMode(Qt::ElideRight);
    list->setSpacing(0);
    if (kind == "reference") {
        referenceResults_ = list;
        auto *calendar = new QCalendarWidget(popup);
        referenceCalendar_ = calendar;
        calendar->setObjectName("referenceCalendar");
        calendar->setAccessibleName("Fecha para enlazar al diario");
        calendar->setFocusPolicy(Qt::NoFocus);
        calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        calendar->hide();
        layout->addWidget(calendar, 1);
        connect(calendar, &QCalendarWidget::clicked, this, [this] { selectReference(); });
        list->setObjectName("referenceResults");
        list->setAccessibleName("Objetos recientes y tipos de objeto");
        connect(list, &QListWidget::itemClicked, this, [this] { selectReference(); });
    } else {
        blockResults_ = list;
        list->setObjectName("blockResults");
        list->setAccessibleName("Tipos de bloque");
        connect(list, &QListWidget::itemClicked, this, [this] { selectBlock(); });
    }
    layout->addWidget(list, 1);
    auto *hint = new QLabel("↑↓ para navegar   Esc para cancelar   ↵ para seleccionar", popup);
    if (kind == "reference") referenceHint_ = hint;
    hint->setObjectName("inlineMenuHint");
    hint->setFixedHeight(35);
    hint->setContentsMargins(5, 0, 5, 0);
    layout->addWidget(hint);
    styleLayer(popup);
    return popup;
}
void RichEditor::showReferencePicker(bool asBlock) {
    if (isReadOnly()) return;
    if (!referenceDataSet_) { if (linkRequested) linkRequested(); return; }
    if (referencePopup_) return;
    beginInlineQuery("@");
    referenceBlock_ = asBlock;
    referenceTypeId_.clear();
    if (selectionToolbar_) selectionToolbar_->hide();
    referencePopup_ = makeInlinePanel("Enlazar o crear objeto", "reference", false);
    rebuildReferences();
    positionPopup(referencePopup_, queryCursor_);
    referencePopup_->show();
    setFocus(Qt::OtherFocusReason);
}
void RichEditor::rebuildReferences() {
    if (!referenceResults_) return;
    const int slashIdx = inlineQuery_.indexOf('/');
    const bool hasSlash = slashIdx >= 0;
    QString scopedTypeId = referenceTypeId_;
    QString searchFilter;

    if (hasSlash) {
        QString typeQuery = inlineQuery_.left(slashIdx).trimmed();
        searchFilter = inlineQuery_.mid(slashIdx + 1).trimmed();
        if (!typeQuery.isEmpty()) {
            for (const auto &entry : types_) {
                const auto t = entry.toObject();
                const QString tid = value(t, "id");
                const QString tname = value(t, "name");
                if (tid.compare(typeQuery, Qt::CaseInsensitive) == 0 ||
                    tname.compare(typeQuery, Qt::CaseInsensitive) == 0 ||
                    typeLabel(tid).compare(typeQuery, Qt::CaseInsensitive) == 0 ||
                    tname.contains(typeQuery, Qt::CaseInsensitive) ||
                    typeLabel(tid).contains(typeQuery, Qt::CaseInsensitive)) {
                    scopedTypeId = tid;
                    break;
                }
            }
        }
        if (scopedTypeId.isEmpty()) scopedTypeId = "atomic";
    } else {
        searchFilter = inlineQuery_.trimmed();
    }

    const bool inType = !scopedTypeId.isEmpty();
    const bool datePicker = scopedTypeId == "journal";
    referenceResults_->clear();
    referenceResults_->setVisible(!datePicker);
    if (referenceCalendar_) referenceCalendar_->setVisible(datePicker);
    if (referenceBack_) referenceBack_->setVisible(inType && !hasSlash);
    if (referenceHint_) referenceHint_->setText(datePicker ? "←↑↓→ fecha   Alt+← volver   ↵ enlazar" : "↑↓ para navegar   Esc para cancelar   ↵ para seleccionar");
    if (referenceHeading_) {
        if (hasSlash) {
            referenceHeading_->setText("Buscar o crear");
            referenceHeading_->setVisible(true);
        } else {
            referenceHeading_->setText(typeLabel(scopedTypeId));
            referenceHeading_->setVisible(inType);
        }
        referenceHeading_->setToolTip("← para volver a todos los tipos");
    }
    if (datePicker) return;

    auto add = [this](const QString &title, const QString &type, const QString &iconOverride = {}) {
        auto *item = new QListWidgetItem(stableIcon(iconOverride.isEmpty() ? typeIcon(type) : actionIcon(iconOverride), 30), title, referenceResults_);
        item->setSizeHint(QSize(390, 41));
        item->setToolTip(title);
        return item;
    };

    auto attachTypeBadge = [this](QListWidgetItem *item, const QString &typeId) {
        auto *row = new QWidget(referenceResults_);
        row->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *layout = new QHBoxLayout(row); layout->setContentsMargins(38, 0, 8, 0); layout->setSpacing(8);
        auto *title = new QLabel(item->text(), row); title->setObjectName("creationActionLabel");
        title->setStyleSheet("background:transparent;"); title->setMinimumWidth(0);
        title->setToolTip(item->text()); layout->addWidget(title, 1);
        auto *badgeLabel = new QLabel(typeLabel(typeId), row);
        const QString bg = dark_ ? "#382f24" : "#fef3c7", fg = dark_ ? "#f59e0b" : "#b45309";
        badgeLabel->setStyleSheet(QString("background:%1;color:%2;border-radius:4px;padding:2px 7px;font-size:11px;font-weight:600;").arg(bg,fg));
        layout->addWidget(badgeLabel, 0); referenceResults_->setItemWidget(item, row);
    };

    // D6 (aliases): a result that matched through an alias shows the alias
    // as a muted right-aligned hint so the user knows why it matched; the
    // stored spelling is also carried on the item (UserRole + 4) so picking
    // it inserts the alias as the link text.
    auto attachAliasHint = [this](QListWidgetItem *item, const QString &alias) {
        auto *hintLabel = new QLabel(alias, referenceResults_);
        hintLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-style: italic; margin-right: 8px;")
                                     .arg(dark_ ? "#919097" : "#75747b"));
        hintLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hintLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        hintLabel->setAccessibleName("Matched alias");
        referenceResults_->setItemWidget(item, hintLabel);
        item->setToolTip("Matched alias: " + alias + " — Enter inserts it as the link text");
    };
    auto setObjectRow = [this, &add, &attachAliasHint](const QJsonObject &object, const QString &typeId,
                                                      const QString &matchedAlias) {
        auto *item = add(value(object, "title"), typeId);
        item->setData(Qt::UserRole, object);
        if (!matchedAlias.isEmpty()) {
            item->setData(Qt::UserRole + 4, matchedAlias);
            attachAliasHint(item, matchedAlias);
        }
    };
    // The client-side alias rule (the server's matcher for GET /v1/search):
    // a title hit needs no alias — the title is the canonical link text; an
    // alias hit only counts when the title did not match.
    auto aliasMatch = [](const QJsonObject &object, const QString &filter) -> QString {
        if (filter.isEmpty() || value(object, "title").contains(filter, Qt::CaseInsensitive))
            return {};
        return LumenAliases::matched(object, filter);
    };

    if (hasSlash && !scopedTypeId.isEmpty()) {
        const QString createTitle = searchFilter.isEmpty() ? ("Crear " + typeLabel(scopedTypeId)) : ("Crear «" + searchFilter + "» · " + typeLabel(scopedTypeId));
        auto *createItem = add(createTitle, scopedTypeId, "plus");
        createItem->setData(Qt::UserRole + 1, scopedTypeId);
        createItem->setData(Qt::UserRole + 2, searchFilter.isEmpty() ? typeLabel(scopedTypeId) : searchFilter);
        attachTypeBadge(createItem, scopedTypeId);

        auto *exploreItem = add("Explorar todos los " + typeLabel(scopedTypeId), scopedTypeId, "search");
        exploreItem->setData(Qt::UserRole + 3, scopedTypeId);

        int count = 0;
        QList<QJsonObject> orderedObjects;
        for (const auto &entry : objects_) orderedObjects.append(entry.toObject());
        std::stable_sort(orderedObjects.begin(), orderedObjects.end(), [this](const QJsonObject &a, const QJsonObject &b) {
            const int aRecent = recentIds_.indexOf(value(a, "id")), bRecent = recentIds_.indexOf(value(b, "id"));
            if (aRecent >= 0 || bRecent >= 0) return aRecent >= 0 && (bRecent < 0 || aRecent < bRecent);
            const auto aTime = value(a, "updated_at").isEmpty() ? value(a, "created_at") : value(a, "updated_at");
            const auto bTime = value(b, "updated_at").isEmpty() ? value(b, "created_at") : value(b, "updated_at");
            return aTime > bTime;
        });
        for (const auto &object : orderedObjects) {
            if (value(object, "type_id") != scopedTypeId) continue;
            const QString matchedAlias = aliasMatch(object, searchFilter);
            if (!searchFilter.isEmpty() && matchedAlias.isEmpty() &&
                !value(object, "title").contains(searchFilter, Qt::CaseInsensitive))
                continue;
            setObjectRow(object, scopedTypeId, matchedAlias);
            if (++count >= 30) break;
        }
    } else if (inType) {
        int count = 0;
        QList<QJsonObject> orderedObjects;
        for (const auto &entry : objects_) orderedObjects.append(entry.toObject());
        std::stable_sort(orderedObjects.begin(), orderedObjects.end(), [this](const QJsonObject &a, const QJsonObject &b) {
            const int aRecent = recentIds_.indexOf(value(a, "id")), bRecent = recentIds_.indexOf(value(b, "id"));
            if (aRecent >= 0 || bRecent >= 0) return aRecent >= 0 && (bRecent < 0 || aRecent < bRecent);
            const auto aTime = value(a, "updated_at").isEmpty() ? value(a, "created_at") : value(a, "updated_at");
            const auto bTime = value(b, "updated_at").isEmpty() ? value(b, "created_at") : value(b, "updated_at");
            return aTime > bTime;
        });
        for (const auto &object : orderedObjects) {
            if (value(object, "type_id") != scopedTypeId)
                continue;
            const QString matchedAlias = aliasMatch(object, searchFilter);
            if (!searchFilter.isEmpty() && matchedAlias.isEmpty() &&
                !value(object, "title").contains(searchFilter, Qt::CaseInsensitive))
                continue;
            setObjectRow(object, value(object, "type_id"), matchedAlias);
            if (++count >= 30) break;
        }
        if (!searchFilter.isEmpty() && referenceCreateRequested) {
            auto *item = add("Crear «" + searchFilter + "» · " + typeLabel(scopedTypeId), scopedTypeId, "plus");
            item->setData(Qt::UserRole + 1, scopedTypeId);
            item->setData(Qt::UserRole + 2, searchFilter);
            attachTypeBadge(item, scopedTypeId);
        }
    } else {
        if (!searchFilter.isEmpty() && referenceCreateRequested) {
            QString defaultType = "note";
            bool hasNote = false;
            for (const auto &entry : types_) if (value(entry.toObject(), "id") == defaultType) hasNote = true;
            if (!hasNote && !types_.isEmpty()) defaultType = value(types_.first().toObject(), "id");
            auto *createItem = add("Nuevo '" + searchFilter + "'", defaultType, "plus");
            createItem->setData(Qt::UserRole + 1, defaultType);
            createItem->setData(Qt::UserRole + 2, searchFilter);
            attachTypeBadge(createItem, defaultType);
        }

        QJsonArray availableTypes = types_;
        bool hasJournal = false;
        for (const auto &entry : availableTypes) if (value(entry.toObject(), "id") == "journal") hasJournal = true;
        if (!hasJournal) availableTypes.append(QJsonObject{{"id", "journal"}, {"name", "Fecha"}});

        for (const auto &entry : availableTypes) {
            const auto type = entry.toObject();
            const auto id = value(type, "id");
            if (id == "query") continue;
            if (!searchFilter.isEmpty() && !typeLabel(id).contains(searchFilter, Qt::CaseInsensitive)) continue;
            auto *item = add(typeLabel(id), id);
            item->setData(Qt::UserRole + 3, id);
            auto *arrow = new QLabel(referenceResults_);
            arrow->setPixmap(actionIcon("chevron-right").pixmap(17, 17));
            arrow->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            arrow->setContentsMargins(0, 0, 10, 0);
            arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
            referenceResults_->setItemWidget(item, arrow);
        }

        int count = 0;
        QList<QJsonObject> orderedObjects;
        for (const auto &entry : objects_) orderedObjects.append(entry.toObject());
        std::stable_sort(orderedObjects.begin(), orderedObjects.end(), [this](const QJsonObject &a, const QJsonObject &b) {
            const int aRecent = recentIds_.indexOf(value(a, "id")), bRecent = recentIds_.indexOf(value(b, "id"));
            if (aRecent >= 0 || bRecent >= 0) return aRecent >= 0 && (bRecent < 0 || aRecent < bRecent);
            const auto aTime = value(a, "updated_at").isEmpty() ? value(a, "created_at") : value(a, "updated_at");
            const auto bTime = value(b, "updated_at").isEmpty() ? value(b, "created_at") : value(b, "updated_at");
            return aTime > bTime;
        });
        for (const auto &object : orderedObjects) {
            const QString matchedAlias = aliasMatch(object, searchFilter);
            if (!searchFilter.isEmpty() && matchedAlias.isEmpty() &&
                !value(object, "title").contains(searchFilter, Qt::CaseInsensitive))
                continue;
            setObjectRow(object, value(object, "type_id"), matchedAlias);
            if (++count >= (searchFilter.isEmpty() ? 5 : 30)) break;
        }
    }

    if (referenceResults_->count() == 0) {
        auto *item = add(searchFilter.isEmpty() ? "Escribe para buscar o crear un objeto" : "No hay coincidencias", "note");
        item->setFlags(Qt::NoItemFlags);
    } else referenceResults_->setCurrentRow(0);
}
void RichEditor::updateInlineQuery() {
    if (updatingPicker_ || (!referencePopup_ && !blockPopup_)) return;
    const auto cursor = textCursor();
    const int start = queryCursor_.position();
    if (cursor.hasSelection() || cursor.block() != queryCursor_.block() || cursor.position() < start + 1) {
        closeInlineMenu(true); return;
    }
    auto query = queryCursor_;
    query.setPosition(cursor.position(), QTextCursor::KeepAnchor);
    const auto text = query.selectedText();
    const auto marker = referencePopup_ ? "@" : "/";
    if (!text.startsWith(marker)) { closeInlineMenu(); return; }
    inlineQuery_ = text.mid(1);
    referenceCursor_ = cursor;
    if (referencePopup_) rebuildReferences(); else rebuildBlockMenu();
    positionPopup(referencePopup_ ? referencePopup_.data() : blockPopup_.data(), queryCursor_, bool(blockPopup_));
}
QTextCursor RichEditor::consumeInlineQuery(bool keepQuery) {
    auto cursor = queryCursor_;
    const int end = referenceCursor_.position();
    cursor.setPosition(keepQuery ? qMin(cursor.position() + 1, end) : end, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.setKeepPositionOnInsert(false);
    return cursor;
}
void RichEditor::closeInlineMenu(bool cancel) {
    if (!referencePopup_ && !blockPopup_) return;
    updatingPicker_ = true;
    if (cancel) {
        const auto active = textCursor();
        auto marker = queryCursor_;
        marker.setPosition(qMin(document()->characterCount() - 1, marker.position() + 1), QTextCursor::KeepAnchor);
        if (marker.selectedText() == (referencePopup_ ? "@" : "/")) consumeInlineQuery(true);
        setTextCursor(active);
    }
    if (referencePopup_) {
        for (auto *child : referencePopup_->findChildren<QWidget *>()) child->setObjectName({});
        referencePopup_->hide(); referencePopup_->deleteLater();
    }
    if (blockPopup_) {
        for (auto *child : blockPopup_->findChildren<QWidget *>()) child->setObjectName({});
        blockPopup_->hide(); blockPopup_->deleteLater();
    }
    referencePopup_ = nullptr; blockPopup_ = nullptr;
    referenceResults_ = nullptr; blockResults_ = nullptr; referenceHeading_ = nullptr; referenceCalendar_ = nullptr; referenceHint_ = nullptr; referenceBack_ = nullptr;
    inlineQuery_.clear(); referenceTypeId_.clear();
    updatingPicker_ = false;
}
void RichEditor::selectReference() {
    if (!referenceResults_) return;
    QJsonObject object;
    QString type, name;
    QString matchedAlias;  // D6: an alias-matched row inserts the alias as the chip text
    if (referenceTypeId_ == "journal" && referenceCalendar_) {
        type = "journal";
        name = referenceCalendar_->selectedDate().toString(Qt::ISODate);
    } else {
        auto *item = referenceResults_->currentItem();
        if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
        const auto nextType = item->data(Qt::UserRole + 3).toString();
        if (!nextType.isEmpty()) { referenceTypeId_ = nextType; rebuildReferences(); return; }
        object = item->data(Qt::UserRole).toJsonObject();
        type = item->data(Qt::UserRole + 1).toString();
        name = item->data(Qt::UserRole + 2).toString();
        matchedAlias = item->data(Qt::UserRole + 4).toString();
    }
    updatingPicker_ = true;
    auto cursor = queryCursor_;
    cursor.beginEditBlock();
    cursor = consumeInlineQuery();
    const bool asBlock = referenceBlock_;
    closeInlineMenu();
    if (!object.isEmpty()) {
        if (asBlock) insertReferenceBlockAt(cursor, object, matchedAlias);
        else insertReferenceAt(cursor, object, matchedAlias);
        cursor.endEditBlock();
        setFocus();
        return;
    }
    cursor.endEditBlock();
    if (!referenceCreateRequested || name.isEmpty()) return;
    setTextCursor(cursor);
    QPointer<RichEditor> guard(this);
    const auto generation = contentGeneration_;
    referenceCreateRequested(type, name, [guard, cursor, asBlock, generation](QJsonObject created) {
        if (!guard || created.isEmpty() || guard->contentGeneration_ != generation || cursor.document() != guard->document()) return;
        auto active = guard->textCursor();
        const bool stillAtOrigin = active.position() == cursor.position() && !active.hasSelection();
        const auto scroll = guard->verticalScrollBar()->value();
        if (asBlock) guard->insertReferenceBlockAt(cursor, created); else guard->insertReferenceAt(cursor, created);
        if (!stillAtOrigin) { guard->setTextCursor(active); guard->verticalScrollBar()->setValue(scroll); }
    });
}
void RichEditor::insertReferenceIcon(QTextCursor &cursor, const QTextCharFormat &format, const QString &type) {
    const QUrl resource("lumen-icon://type/" + type + (dark_ ? "/dark" : "/light"));
    const auto image = typeIcon(type, 18).pixmap(18, 18).toImage();
    document()->addResource(QTextDocument::ImageResource, resource, image);
    QTextImageFormat icon;
    icon.merge(format);
    icon.setName(resource.toString());
    icon.setWidth(18); icon.setHeight(18);
    icon.setVerticalAlignment(QTextCharFormat::AlignMiddle);
    icon.setProperty(ReferenceIconProperty, true);
    cursor.insertImage(icon);
}
void RichEditor::insertReferenceAt(QTextCursor cursor, const QJsonObject &object, const QString &linkText) {
    const QString id = value(object, "id"), title = value(object, "title"), type = value(object, "type_id");
    if (id.isEmpty() || title.isEmpty() || cursor.document() != document()) return;
    const QString displayed = linkText.isEmpty() ? title : linkText;
    cursor.setKeepPositionOnInsert(false);
    cursor.beginEditBlock();
    QTextCharFormat format;
    format.setAnchor(true);
    format.setAnchorHref("lumen://object/" + id);
    format.setForeground(QColor(dark_ ? "#d2b7f5" : "#7b589d"));
    format.setBackground(QColor(dark_ ? "#2d2638" : "#f2edf8"));
    format.setFontUnderline(true);
    format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    format.setProperty(ReferenceTypeProperty, type);
    format.setToolTip(typeLabel(type) + " · " + title + "\nClic para previsualizar · Ctrl+clic para abrir en pestaña");
    insertReferenceIcon(cursor, format, type);
    cursor.insertText(" " + displayed, format);
    QTextCharFormat normal;
    cursor.insertText(" ", normal);
    cursor.endEditBlock();
    setTextCursor(cursor);
    // La creación inline es asíncrona: al volver del modal, el foco debe
    // regresar al editor para seguir escribiendo en la misma frase.
    setFocus(Qt::OtherFocusReason);
    ensureBlockIds();
    if (referenceInserted) referenceInserted(id);
}
void RichEditor::insertObjectReference(const QJsonObject &object) { insertReferenceAt(textCursor(), object); }
void RichEditor::insertReferenceBlockAt(QTextCursor cursor, const QJsonObject &object, const QString &linkText) {
    cursor.beginEditBlock();
    if (cursor.positionInBlock() > 0) cursor.insertBlock();
    auto format = cursor.blockFormat();
    format.setProperty(EmbedBlockProperty, true);
    format.setLeftMargin(18);
    format.setRightMargin(12);
    format.setTopMargin(10);
    format.setBottomMargin(12);
    format.setBackground(QColor(dark_ ? "#1c1c20" : "#f4f4f5"));
    cursor.setBlockFormat(format);
    insertReferenceAt(cursor, object, linkText);
    auto after = textCursor();
    after.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.endEditBlock();
    setTextCursor(after);
    ensureBlockIds();
}
void RichEditor::insertImageBlock(const QString &objectId, const QPixmap &pixmap, const QString &title) {
    if (objectId.isEmpty() || pixmap.isNull() || !document()) return;
    const QUrl resource("lumen://image/" + objectId);
    document()->addResource(QTextDocument::ImageResource, resource, pixmap);

    auto cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.positionInBlock() > 0) cursor.insertBlock();
    auto format = cursor.blockFormat();
    format.setProperty(EmbedBlockProperty, true);
    format.setProperty(BlockStyleProperty, "image");
    format.setAlignment(Qt::AlignCenter);
    format.setTopMargin(8);
    format.setBottomMargin(8);
    cursor.setBlockFormat(format);

    const double maxW = 640.0;
    double w = pixmap.width();
    double h = pixmap.height();
    if (w > maxW) {
        h = h * (maxW / w);
        w = maxW;
    }
    w = qMax(1.0, w);
    h = qMax(1.0, h);

    QTextImageFormat imgFormat;
    imgFormat.setName(resource.toString());
    imgFormat.setWidth(w);
    imgFormat.setHeight(h);
    imgFormat.setAnchor(true);
    imgFormat.setAnchorHref("lumen://object/" + objectId);
    imgFormat.setProperty(ReferenceTypeProperty, "image");
    imgFormat.setToolTip((title.isEmpty() ? "Imagen" : title) + "\nClic para previsualizar · Ctrl+clic para abrir en pestaña");

    cursor.insertImage(imgFormat);

    auto after = textCursor();
    after.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.endEditBlock();
    setTextCursor(after);
    ensureBlockIds();
    if (referenceInserted) referenceInserted(objectId);
}
void RichEditor::insertObjectReferenceBlock(const QJsonObject &object) { insertReferenceBlockAt(textCursor(), object); }
bool RichEditor::focusBlock(const QString &id) {
    if (id.isEmpty()) return false;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        if (blockId(block) != id) continue;
        QTextCursor cursor(block);
        setTextCursor(cursor);
        setFocus(Qt::OtherFocusReason);
        ensureCursorVisible();
        QTextEdit::ExtraSelection selected;
        selected.cursor = cursor;
        selected.format.setBackground(QColor(dark_ ? "#342b40" : "#eee5f6"));
        selected.format.setProperty(QTextFormat::FullWidthSelection, true);
        flashSelections_ = {selected};
        applyExtraSelections();
        flashTimer_.start(reducedMotion_ ? 1100 : 700);
        return true;
    }
    return false;
}
bool RichEditor::referenceHasTitle(const QString &text, const QString &type, const QString &) const {
    if (type == "image") return true;
    auto payload = text;
    payload.remove(QChar::ObjectReplacementCharacter);
    // Older notes used text glyphs; accept them without creating more glyph references.
    const QStringList legacy{QString::fromUtf8("▣"), QString::fromUtf8("♙"), QString::fromUtf8("▦"),
        QString::fromUtf8("↗"), QString::fromUtf8("▧"), QString::fromUtf8("▤"), QString::fromUtf8("□"), QString::fromUtf8("◇")};
    for (const auto &glyph : legacy) if (payload.startsWith(glyph)) { payload.remove(0, glyph.size()); break; }
    return !payload.trimmed().isEmpty();
}
QJsonArray RichEditor::documentReferenceIds() const {
    QJsonArray ids;
    for (const auto &span : referenceSpans(document()))
        if (referenceHasTitle(span.text, span.type, span.id) && !ids.contains(span.id)) ids.append(span.id);
    return ids;
}
QList<RichEditor::TagMention> RichEditor::tagMentions() const {
    QList<TagMention> mentions;
    QSet<QString> seen;
    for (const auto &span : referenceSpans(document())) {
        if (!referenceHasTitle(span.text, span.type, span.id) || seen.contains(span.id)) continue;
        // El tipo viaja en la referencia; si falta (contenido antiguo), se
        // resuelve contra los objetos conocidos para no perder menciones.
        QString type = span.type;
        if (type.isEmpty())
            for (const auto &entry : objects_)
                if (value(entry.toObject(), "id") == span.id) {
                    type = value(entry.toObject(), "type_id");
                    break;
                }
        if (type != "tag") continue;
        const auto block = document()->findBlock(span.start);
        const QString id = block.isValid() ? blockId(block) : QString();
        if (id.isEmpty()) continue;
        seen.insert(span.id);
        mentions.append({span.id, id});
    }
    return mentions;
}
QString RichEditor::blockId(const QTextBlock &block) { return block.blockFormat().property(BlockIdProperty).toString(); }
void RichEditor::ensureBlockIds() {
    if (normalizing_) return;
    normalizing_ = true;
    QSet<QString> used;
    QTextCursor transaction(document());
    bool began = false;
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        QString id = blockId(block);
        if (id.isEmpty() || used.contains(id)) {
            if (!began) { transaction.joinPreviousEditBlock(); began = true; }
            id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QTextCursor cursor(block);
            auto format = block.blockFormat();
            format.setProperty(BlockIdProperty, id);
            cursor.setBlockFormat(format);
        }
        used.insert(id);
    }
    const auto spans = referenceSpans(document());
    for (auto it = spans.crbegin(); it != spans.crend(); ++it) {
        if (referenceHasTitle(it->text, it->type, it->id)) continue;
        if (!began) { transaction.joinPreviousEditBlock(); began = true; }
        QTextCursor cursor(document());
        cursor.setPosition(it->start);
        cursor.setPosition(it->end, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    if (began) transaction.endEditBlock();
    normalizing_ = false;
}
namespace {
QJsonObject canonicalLocation(const QJsonArray &nodes, const QString &identity, const QString &parent = {}) {
    for (int i = 0; i < nodes.size(); ++i) {
        const auto node = nodes[i].toObject();
        if (node.value("id").toString() == identity) return {{"parent_id", parent}, {"position", i}, {"node", node}};
        const auto nested = canonicalLocation(node.value("children").toArray(), identity, node.value("id").toString());
        if (!nested.isEmpty()) return nested;
    }
    return {};
}
}
bool RichEditor::selectCanonicalBlock(const QString &identity) {
    QTextFrame *matched = nullptr;
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        const auto data = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object();
        if (data.value("canonical_node").toObject().value("id").toString() == identity) matched = frame;
        for (auto *child : frame->childFrames()) visit(child);
    };
    visit(document()->rootFrame());
    QTextCursor selection(document());
    if (matched) {
        selection.setPosition(matched->firstPosition() - 1);
        selection.setPosition(matched->lastPosition() + 1, QTextCursor::KeepAnchor);
    } else {
        const auto block = findBlockById(identity); if (!block.isValid()) return false;
        selection.setPosition(block.position()); selection.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    }
    setTextCursor(selection); return true;
}
bool RichEditor::applyCanonicalCommands(const QJsonArray &commands, QString *error) {
    if (isReadOnly()) return false;
    const auto previous = canonicalDocument(); QJsonObject next;
    if (!LumenDocument::applyCommands(previous, commands, &next, error)) return false;
    if (next == previous) return true;
    RichEditor rendered;
    rendered.resourceProvider = resourceProvider;
    rendered.setAppearance(dark_, reducedMotion_);
    rendered.setFont(font()); rendered.setCanonicalDocument(next);
    for (auto block = rendered.document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) if (it.fragment().charFormat().isImageFormat()) {
            const QUrl name(it.fragment().charFormat().toImageFormat().name());
            const auto resource = rendered.document()->resource(QTextDocument::ImageResource, name);
            if (resource.isValid()) document()->addResource(QTextDocument::ImageResource, name, resource);
        }
    QTextCursor source(rendered.document()); source.select(QTextCursor::Document);
    const QTextDocumentFragment fragment(source);
    const auto selectedID = blockId(textCursor().block());
    QTextCursor edit(document()); edit.beginEditBlock(); edit.select(QTextCursor::Document);
    edit.removeSelectedText();
    edit.setBlockFormat(rendered.document()->begin().blockFormat()); edit.setCharFormat(QTextCharFormat());
    edit.insertFragment(fragment); edit.endEditBlock();
    loadedCanonicalEmpty_ = false;
    if (!focusBlock(selectedID)) setTextCursor(edit);
    refreshToggleVisibility(); refreshDividerRule();
    return true;
}
QJsonObject RichEditor::canonicalDocument() const {
    if ((loadedCanonicalEmpty_ || document()->begin().blockFormat().property(EmptyDocumentProperty).toBool()) && document()->characterCount() == 1 && !document()->begin().textList()
        && !document()->begin().blockFormat().headingLevel()
        && document()->begin().blockFormat().property(BlockStyleProperty).toString().isEmpty()) return LumenDocument::empty();
    auto canonical = LumenDocument::fromPresentation(structuredBlocks());
    std::function<QJsonArray(const QJsonArray &)> resolve = [&](const QJsonArray &nodes) {
        QJsonArray out;
        for (const auto &value : nodes) {
            auto node = value.toObject(); auto attrs = node.value("attrs").toObject();
            if (node.value("type") == "image" && attrs.value("asset_id").toString().isEmpty()) {
                for (const auto &entry : objects_) if (entry.toObject().value("id") == attrs.value("object_id")) {
                    const auto properties = entry.toObject().value("properties").toObject();
                    auto id = properties.value("image_asset_id").toString();
                    if (id.isEmpty()) id = properties.value("asset_id").toString();
                    if (!id.isEmpty()) { attrs["asset_id"] = id; node["attrs"] = attrs; }
                }
            }
            if (node.contains("children")) node["children"] = resolve(node.value("children").toArray());
            out.append(node);
        }
        return out;
    };
    canonical["blocks"] = resolve(canonical.value("blocks").toArray());
    return canonical;
}
void RichEditor::setCanonicalDocument(const QJsonObject &value) {
    const auto blocks = LumenDocument::toPresentation(value);
    loadedCanonicalEmpty_ = blocks.isEmpty();
    if (blocks.isEmpty()) { clear(); QTextCursor cursor(document()); auto format = QTextBlockFormat();
        format.setProperty(EmptyDocumentProperty, true); cursor.setBlockFormat(format);
        document()->clearUndoRedoStacks(); return; }
    setStructuredBlocks(blocks);
}
QJsonArray RichEditor::structuredBlocks() const {
    const_cast<RichEditor *>(this)->ensureBlockIds();
    QJsonArray blocks;
    // Serializes one block with its intrinsic type/runs (used for top-level
    // blocks and for the members stored inside a group's "children").
    auto serializeBlock = [](const QTextBlock &block) {
        const auto format = block.blockFormat();
        QString type = "paragraph";
        if (format.property(EmbedBlockProperty).toBool()) type = "embed";
        else if (format.property(BlockStyleProperty).toString() == "code") type = "code";
        else if (format.property(BlockStyleProperty).toString() == "math") type = "math";
        else if (format.headingLevel()) type = "heading";
        else if (block.textList()) type = block.textList()->format().style() == QTextListFormat::ListDecimal ? "numbered_list"
            : (block.textList()->format().style() == QTextListFormat::ListLowerAlpha || block.textList()->format().style() == QTextListFormat::ListUpperAlpha) ? "alphabetic_list" : (block.textList()->format().style() == QTextListFormat::ListLowerRoman || block.textList()->format().style() == QTextListFormat::ListUpperRoman) ? "roman_list" : "bullet_list";
        else if (format.leftMargin() >= 18) type = "quote";
        else if (format.property(BlockStyleProperty).toString() == "small") type = "small";
        else if (format.property(BlockStyleProperty).toString() == "todo" &&
                 (block.text().startsWith(QString::fromUtf8("○ ")) || block.text().startsWith(QString::fromUtf8("✔ "))))
            type = "todo";
        else if (format.property(BlockStyleProperty).toString() == "toggle" &&
                 (block.text().startsWith(QString::fromUtf8("▶ ")) || block.text().startsWith(QString::fromUtf8("▼ "))))
            type = "toggle";
        else if (format.property(BlockStyleProperty).toString() == "group") type = "group";
        else if (format.property(BlockStyleProperty).toString() == "divider") type = "divider";
        auto item = QJsonDocument::fromJson(format.property(NativeBlockProperty).toByteArray()).object();
        item.insert("id", blockId(block)); item.insert("type", type); item.insert("text", block.text());
        item.insert("level", format.headingLevel()); item.insert("alignment", int(format.alignment()));
        if (type == "code") item.insert("language", format.property(CodeLanguageProperty).toString());
        else item.remove("language");
        if (block.textList()) item.insert("indent", block.textList()->format().indent());
        else if (format.indent() > 0) item.insert("indent", format.indent());
        else item.remove("indent");
        const QString textColor = format.property(BlockTextColorProperty).toString();
        if (!textColor.isEmpty()) item.insert("text_color", textColor);
        else item.remove("text_color");
        const QString backgroundColor = format.property(BlockBackgroundProperty).toString();
        if (!backgroundColor.isEmpty()) item.insert("background_color", backgroundColor);
        else item.remove("background_color");
        QJsonArray runs;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto f = fragment.charFormat();
            auto run = QJsonDocument::fromJson(f.property(NativeRunProperty).toByteArray()).object();
            run.insert("text", fragment.text()); run.insert("bold", f.fontWeight() >= QFont::Bold);
            run.insert("italic", f.fontItalic()); run.insert("underline", f.fontUnderline());
            run.insert("strike", f.fontStrikeOut()); run.insert("code", f.fontFixedPitch());
            if (!f.isAnchor() && !f.isImageFormat()) { run.remove("href"); run.remove("object_id"); run.remove("type_id"); }
            if (!f.isImageFormat()) {
                if (run.value("type").toString() == "image" || run.value("type").toString() == "math") run.remove("type");
                for (const auto *key : {"image_name", "width", "height", "latex", "display", "reference_icon"}) run.remove(key);
            }
            if (f.isAnchor()) {
                run.insert("href", f.anchorHref());
                run.insert("type_id", f.property(ReferenceTypeProperty).toString());
                const QUrl url(f.anchorHref());
                if (url.scheme() == "lumen" && url.host() == "object") run.insert("object_id", url.path().mid(1));
            }
            if (f.isImageFormat() && f.hasProperty(MathLatexProperty)) {
                run.insert("type", "math");
                run.insert("latex", f.property(MathLatexProperty).toString());
                run.insert("display", f.property(MathDisplayProperty).toBool());
            } else if (f.isImageFormat() && !f.property(ReferenceIconProperty).toBool()) {
                const auto imgFmt = f.toImageFormat();
                run.insert("type", "image");
                run.insert("image_name", imgFmt.name());
                run.insert("width", imgFmt.width());
                run.insert("height", imgFmt.height());
            }
            if (f.property(ReferenceIconProperty).toBool()) run.insert("reference_icon", true);
            if (f.fontPointSize() > 0) run.insert("point_size", f.fontPointSize());
            runs.append(run);
        }
        item.insert("runs", runs);
        return item;
    };
    // Traverse frames rather than document()->begin()/next(), which flattens
    // every table cell into an unrelated paragraph.
    std::function<QJsonArray(QTextFrame::iterator, QTextFrame::iterator)> serializeRange;
    serializeRange = [&](QTextFrame::iterator begin, QTextFrame::iterator end) {
        QJsonArray result, groupChildren;
        QString groupId;
        auto flushGroup = [&] {
            if (groupId.isEmpty()) return;
            result.append(QJsonObject{{"id", groupId}, {"type", "group"}, {"text", QString()},
                {"level", 0}, {"alignment", 0}, {"runs", QJsonArray()}, {"children", groupChildren}});
            groupId.clear(); groupChildren = {};
        };
        for (auto it = begin; it != end; ++it) {
            if (auto *table = qobject_cast<QTextTable *>(it.currentFrame())) {
                flushGroup();
                const auto format = table->format();
                auto item = QJsonDocument::fromJson(format.property(NativeFrameProperty).toByteArray()).object();
                const QString kind = format.property(BlockStyleProperty).toString() == "columns" ? "columns" : "table";
                QString id = item.value("id").toString();
                if (id.isEmpty()) id = format.property(BlockIdProperty).toString();
                if (id.isEmpty()) {
                    id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    auto stored = format; stored.setProperty(BlockIdProperty, id); table->setFormat(stored);
                }
                item.insert("id", id); item.insert("type", kind); item.insert("text", QString()); item.insert("runs", QJsonArray());
                QJsonArray rows, columns, align;
                auto at = [](const QJsonArray &array, int index) { return index < array.size() ? array.at(index) : QJsonValue(); };
                for (int r = 0; r < table->rows(); ++r) {
                    QJsonArray row;
                    for (int c = 0; c < table->columns(); ++c) {
                        const auto cell = table->cellAt(r, c);
                        const auto children = serializeRange(cell.begin(), cell.end());
                        if (kind == "columns") { columns.append(children); continue; }
                        auto cellJson = at(at(item.value("rows").toArray(), r).toArray(), c).toObject();
                        QJsonArray runs;
                        bool first = true;
                        for (const auto &child : children) {
                            if (!first) runs.append(QJsonObject{{"text", QString(QChar::LineSeparator)}});
                            first = false;
                            for (const auto &run : child.toObject().value("runs").toArray()) runs.append(run);
                        }
                        cellJson.insert("runs", runs); cellJson.insert("blocks", children); row.append(cellJson);
                        if (r == 0) {
                            const auto alignment = cell.firstCursorPosition().blockFormat().alignment();
                            align.append(alignment.testFlag(Qt::AlignHCenter) ? "center" : alignment.testFlag(Qt::AlignRight) ? "right" :
                                at(item.value("align").toArray(), c).toString());
                        }
                    }
                    if (kind != "columns") rows.append(row);
                }
                if (kind == "columns") item.insert("columns", columns);
                else { item.insert("rows", rows); item.insert("align", align); }
                result.append(item);
            } else if (auto *frame = it.currentFrame()) {
                flushGroup();
                auto item = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object();
                const auto kind = item.value("type").toString();
                if (kind == "group") {
                    item.insert("children", serializeRange(frame->begin(), frame->end())); result.append(item);
                } else if (kind == "toggle") {
                    const auto entries = serializeRange(frame->begin(), frame->end());
                    if (!entries.isEmpty()) {
                        auto header = entries.first().toObject();
                        QJsonArray children;
                        for (int i = 1; i < entries.size(); ++i) children.append(entries.at(i));
                        if (header.value("type").toString() == "toggle") {
                            header.insert("children", children); result.append(header);
                        } else {
                            header.remove("children"); result.append(header);
                            for (const auto &child : children) result.append(child);
                        }
                    }
                } else if (kind == "toggle-body") {
                    for (const auto &child : serializeRange(frame->begin(), frame->end())) result.append(child);
                }
            } else if (const auto block = it.currentBlock(); block.isValid()) {
                if (block.text().isEmpty() && block.blockFormat().property(LayoutBoundaryProperty).toBool()) continue;
                if (block.text().isEmpty() && !block.textList() && !block.blockFormat().headingLevel()
                    && block.blockFormat().property(BlockStyleProperty).toString().isEmpty()
                    && QJsonDocument::fromJson(block.blockFormat().property(NativeBlockProperty).toByteArray()).object().value("canonical_placeholder").toBool()) continue;
                const auto nextGroup = block.blockFormat().property(GroupBlockProperty).toString();
                if (nextGroup != groupId) flushGroup();
                if (!nextGroup.isEmpty()) { groupId = nextGroup; groupChildren.append(serializeBlock(block)); }
                else result.append(serializeBlock(block));
            }
        }
        flushGroup(); return result;
    };
    blocks = serializeRange(document()->rootFrame()->begin(), document()->rootFrame()->end());
    return blocks;
}
void RichEditor::setStructuredBlocks(const QJsonArray &blocks) {
    if (blocks.isEmpty()) return;
    normalizing_ = true;
    QSignalBlocker guard(this);
    clear();
    QTextCursor cursor(document());
    bool first = true;
    QTextList *activeList = nullptr;
    QString activeListType;
    QTextBlock lastInserted;
    // Inserts one serialized block (or group child) at the cursor. Shared by
    // top-level entries and the members of a group's "children".
    auto insertObject = [&](const QJsonObject &object) {
        if (!first) cursor.insertBlock();
        first = false;
        auto format = QTextBlockFormat();
        QString id = value(object, "id");
        if (id.isEmpty()) id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        format.setProperty(BlockIdProperty, id);
        format.setProperty(NativeBlockProperty, QJsonDocument(object).toJson(QJsonDocument::Compact));
        format.setHeadingLevel(qBound(0, object.value("level").toInt(), 6));
        if (object.contains("alignment")) format.setAlignment(Qt::Alignment(object.value("alignment").toInt()));
        const auto rawType = value(object, "type");
        const QString blockText = value(object, "text");
        const auto type = (rawType == "todo" && !blockText.startsWith(QString::fromUtf8("○ ")) &&
                           !blockText.startsWith(QString::fromUtf8("✔ ")))
                              ? QString("paragraph")
                              : rawType;
        if (type == "small" || type == "code" || type == "todo" || type == "toggle" || type == "math" || type == "group" || type == "divider") format.setProperty(BlockStyleProperty, type);
        if (type == "math") format.setAlignment(Qt::AlignCenter);
        if (type == "divider") {
            format.setAlignment(Qt::AlignLeft);
            format.setLeftMargin(0); format.setRightMargin(0);
            format.setTopMargin(12); format.setBottomMargin(12);
        }
        if (type == "group") {
            format.setLeftMargin(16); format.setRightMargin(16); format.setTopMargin(8); format.setBottomMargin(8);
            format.setBackground(groupBoxColor(dark_));
        }
        if (type == "quote") { format.setLeftMargin(22); format.setRightMargin(10); }
        if (type == "todo") { format.setLeftMargin(0); }
        if (type == "toggle") { format.setLeftMargin(0); format.setBottomMargin(6); }
        if (type == "embed") {
            format.setProperty(EmbedBlockProperty, true);
            format.setLeftMargin(18); format.setRightMargin(12); format.setTopMargin(10);
            format.setBackground(QColor(dark_ ? "#1c1c20" : "#f4f4f5"));
        }
        if (type != "bullet_list" && type != "numbered_list" && type != "alphabetic_list" && type != "divider") {
            const int blkIndent = qBound(0, object.value("indent").toInt(0), 8);
            if (blkIndent > 0) format.setIndent(blkIndent);
        }
        if (type != "divider") format.setBottomMargin(10);
        const QString storedTextColor = value(object, "text_color");
        if (!storedTextColor.isEmpty()) format.setProperty(BlockTextColorProperty, storedTextColor);
        const QString storedBackground = value(object, "background_color");
        if (!storedBackground.isEmpty()) {
            format.setProperty(BlockBackgroundProperty, storedBackground);
            format.setBackground(QColor(storedBackground));
        }
        cursor.setBlockFormat(format);
        if (type == "divider") {
            QTextCharFormat divCharFmt;
            divCharFmt.setFontPointSize(1);
            cursor.setBlockCharFormat(divCharFmt);
        }
        const bool codeBlock = type == "code";
        const bool headingBlock = type == "heading";
        const QString headingStyle = headingBlock
            ? "heading" + QString::number(qBound(1, object.value("level").toInt(), 4)) : QString();
        const qreal fallbackSize = headingBlock ? blockStylePointSize(headingStyle)
            : type == "small" ? blockStylePointSize("small")
            : codeBlock ? blockStylePointSize("code") : 0;
        if (codeBlock) {
            const auto rawLanguage = value(object, "language");
            const auto language = LumenSyntax::normalize(rawLanguage);
            styleCodeBlock(cursor, language.isEmpty() ? rawLanguage : language);
        }
        auto runs = object.value("runs").toArray();
        if (runs.isEmpty()) runs.append(QJsonObject{{"text", value(object, "text")}});
        for (const auto &part : runs) {
            const auto run = part.toObject();
            const bool runBold = run.value("bold").toBool();
            QTextCharFormat character;
            character.setProperty(NativeRunProperty, QJsonDocument(run).toJson(QJsonDocument::Compact));
            // Un encabezado conserva su seminegrita al guardar y cargar aunque
            // el JSON solo distinga negrita sí/no: el peso base lo pone el tipo.
            character.setFontWeight(headingBlock
                ? (runBold ? QFont::Bold : QFont::DemiBold)
                : (runBold ? QFont::Bold : QFont::Normal));
            character.setFontItalic(run.value("italic").toBool());
            character.setFontUnderline(run.value("underline").toBool());
            character.setFontStrikeOut(run.value("strike").toBool());
            character.setFontFixedPitch(run.value("code").toBool());
            if (run.value("point_size").toDouble() > 0) character.setFontPointSize(run.value("point_size").toDouble());
            else if (fallbackSize > 0) character.setFontPointSize(fallbackSize);
            if (codeBlock) character.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
            if (run.value("color").isString()) character.setForeground(QColor(run.value("color").toString()));
            if (run.value("highlight").isString()) character.setBackground(QColor(run.value("highlight").toString()));
            auto href = value(run, "href");
            if (href.isEmpty() && run.value("type").toString() == "image") {
                QString id = value(run, "object_id");
                if (id.isEmpty() && value(run, "image_name").startsWith("lumen://image/")) id = value(run, "image_name").mid(14);
                if (!id.isEmpty()) href = "lumen://object/" + id;
            }
            if (run.value("type").toString() == "math") {
                auto image = mathFormat(value(run, "latex"), run.value("display").toBool());
                image.setProperty(NativeRunProperty, QJsonDocument(run).toJson(QJsonDocument::Compact));
                cursor.insertImage(image);
                continue;
            }
            if (!href.isEmpty()) {
                character.setAnchor(true);
                character.setAnchorHref(href);
                character.setProperty(ReferenceTypeProperty, value(run, "type_id"));
                character.setForeground(QColor(dark_ ? "#d2b7f5" : "#7b589d"));
                character.setBackground(QColor(dark_ ? "#2d2638" : "#f2edf8"));
                character.setToolTip("Clic para previsualizar · Ctrl+clic para abrir en pestaña");
            } else if (!storedTextColor.isEmpty()) {
                character.setForeground(QColor(storedTextColor));
            }
            if (run.value("canonical_inline").toObject().value("type") == "temporal") {
                character.setBackground(QColor(dark_ ? "#29292e" : "#eeeeef"));
                character.setToolTip("Fecha y hora");
            }
            if (run.value("reference_icon").toBool() && !href.isEmpty()
                && QUrl(href).scheme() == "lumen" && QUrl(href).host() == "object") {
                insertReferenceIcon(cursor, character, value(run, "type_id"));
            } else if (run.value("type").toString() == "image" || run.contains("image_name")) {
                QTextImageFormat imgFmt;
                imgFmt.merge(character);
                const QString imgName = run.value("image_name").toString();
                imgFmt.setName(imgName);
                // Las líneas de divisor toman el ancho de la columna del recurso;
                // el ancho guardado (680 en notas anteriores) no debe fijarlas.
                if (imgName.startsWith("lumen-rule://divider/")) {
                    refreshDividerRule();
                    imgFmt.setHeight(2);
                    imgFmt.setProperty(BlockStyleProperty, "divider");
                } else {
                    if (run.value("width").toDouble() > 0) imgFmt.setWidth(run.value("width").toDouble());
                    if (run.value("height").toDouble() > 0) imgFmt.setHeight(run.value("height").toDouble());
                }
                imgFmt.setProperty(ReferenceTypeProperty, "image");
                if (!href.isEmpty()) {
                    imgFmt.setAnchor(true);
                    imgFmt.setAnchorHref(href);
                    imgFmt.setToolTip("Imagen · Clic para previsualizar · Ctrl+clic para abrir en pestaña");
                }
                const QUrl imgUrl(imgName);
                // Las líneas de divisor las gestiona refreshDividerRule: el ancho
                // del recurso depende de la columna, no del contenido guardado.
                if (!imgName.startsWith("lumen-rule://divider/")) {
                    if (resourceProvider) {
                        const auto res = resourceProvider(QTextDocument::ImageResource, imgUrl);
                        if (res.isValid() && !res.isNull()) {
                            document()->addResource(QTextDocument::ImageResource, imgUrl, res);
                        }
                    }
                    const auto defRes = resolveDefaultResource(QTextDocument::ImageResource, imgUrl);
                    if (defRes.isValid() && !defRes.isNull()) {
                        document()->addResource(QTextDocument::ImageResource, imgUrl, defRes);
                    }
                }
                cursor.insertImage(imgFmt);
            } else {
                cursor.insertText(value(run, "text"), character);
            }
        }
        if (type == "bullet_list" || type == "numbered_list" || type == "alphabetic_list" || type == "roman_list") {
            QTextListFormat list;
            list.setStyle(type == "numbered_list" ? QTextListFormat::ListDecimal
                : type == "alphabetic_list" ? QTextListFormat::ListLowerAlpha : type == "roman_list" ? QTextListFormat::ListLowerRoman : QTextListFormat::ListDisc);
            if (value(object, "list_case") == "upper") {
                if (type == "alphabetic_list") list.setStyle(QTextListFormat::ListUpperAlpha);
                if (type == "roman_list") list.setStyle(QTextListFormat::ListUpperRoman);
            }
            list.setIndent(qBound(1, object.value("indent").toInt(1), 8));
            if (activeList && activeListType == type && activeList->format().indent() == list.indent())
                activeList->add(cursor.block());
            else
                activeList = cursor.createList(list);
            activeListType = type;
        } else {
            activeList = nullptr;
            activeListType.clear();
        }
        lastInserted = cursor.block();
    };
    // Draws the group box (shared id + background + margins) over members.
    auto applyGroupBox = [&](const QList<QTextBlock> &members, const QString &groupId) {
        for (int i = 0; i < members.size(); ++i) {
            auto format = members.at(i).blockFormat();
            format.setProperty(GroupBlockProperty, groupId);
            format.setLeftMargin(16);
            format.setRightMargin(16);
            format.setTopMargin(i == 0 ? 8 : 0);
            format.setBottomMargin(i == members.size() - 1 ? 8 : 0);
            const QColor background = recomputedBlockBackground(format, dark_);
            if (background.isValid()) format.setBackground(background);
            else format.clearBackground();
            QTextCursor member(members.at(i));
            member.setBlockFormat(format);
        }
    };
    std::function<void(const QJsonObject &)> insertEntry;
    insertEntry = [&](const QJsonObject &object) {
        const auto type = value(object, "type");
        if (type == "table" || type == "columns") {
            activeList = nullptr; activeListType.clear();
            if (!first) cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            auto boundary = QTextBlockFormat(); boundary.setProperty(LayoutBoundaryProperty, true);
            cursor.setBlockFormat(boundary);
            const auto rows = object.value("rows").toArray();
            const auto columns = object.value("columns").toArray();
            int width = type == "columns" ? columns.size() : (rows.isEmpty() ? 1 : rows.first().toArray().size());
            if (width == 0) width = 1;
            QTextTableFormat format;
            format.setProperty(BlockStyleProperty, type);
            format.setProperty(NativeFrameProperty, QJsonDocument(object).toJson(QJsonDocument::Compact));
            format.setBorder(type == "columns" ? 0 : 1); format.setCellSpacing(type == "columns" ? 12 : 0);
            format.setCellPadding(8); format.setTopMargin(8); format.setBottomMargin(12);
            const auto canonical = object.value("canonical_node").toObject();
            QList<QTextLength> widths;
            if (type == "table") {
                qreal total = 0;
                for (const auto &value : canonical.value("attrs").toObject().value("columns").toArray()) {
                    const auto width = qMax(40.0, value.toObject().value("width").toDouble(160));
                    widths.append(QTextLength(QTextLength::FixedLength, width)); total += width;
                }
                format.setWidth(QTextLength(QTextLength::FixedLength, total > 0 ? total : width * 160));
            } else {
                format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
                qreal total = 0; for (const auto &c : canonical.value("children").toArray()) total += c.toObject().value("attrs").toObject().value("width").toDouble(1);
                for (const auto &c : canonical.value("children").toArray()) widths.append(QTextLength(QTextLength::PercentageLength,
                    100 * c.toObject().value("attrs").toObject().value("width").toDouble(1) / qMax(1.0, total)));
            }
            if (!widths.isEmpty()) format.setColumnWidthConstraints(widths);
            auto *table = cursor.insertTable(type == "columns" ? 1 : qMax(1, rows.size()), width, format);
            for (int r = 0; r < table->rows(); ++r) for (int c = 0; c < table->columns(); ++c) {
                cursor = table->cellAt(r, c).firstCursorPosition(); first = true;
                if (type == "columns") {
                    const auto children = c < columns.size() ? columns.at(c).toArray() : QJsonArray{};
                    for (const auto &child : children) insertEntry(child.toObject());
                    if (children.isEmpty()) insertObject(QJsonObject{{"text", ""}, {"canonical_placeholder", true}});
                } else {
                    const auto row = r < rows.size() ? rows.at(r).toArray() : QJsonArray{};
                    const auto cell = c < row.size() ? row.at(c).toObject() : QJsonObject{};
                    auto paragraph = cell; paragraph.insert("type", "paragraph");
                    const auto align = object.value("align").toArray();
                    const QString alignment = c < align.size() ? align.at(c).toString() : QString();
                    paragraph.insert("alignment", int(alignment == "center" ? Qt::AlignHCenter : alignment == "right" ? Qt::AlignRight : Qt::AlignLeft));
                    const auto cellBlocks = cell.value("blocks").toArray();
                    if (cellBlocks.isEmpty()) {
                        if (cell.contains("blocks")) insertObject(QJsonObject{{"text", ""}, {"canonical_placeholder", true}});
                        else insertObject(paragraph);
                    }
                    else for (const auto &entry : cellBlocks) insertEntry(entry.toObject());
                }
                activeList = nullptr; activeListType.clear();
            }
            cursor = QTextCursor(document()); cursor.setPosition(table->lastPosition() + 1);
            cursor.setBlockFormat(boundary); cursor.setCharFormat(QTextCharFormat());
            first = true; lastInserted = {};
            return;
        }
        const auto children = object.value("children").toArray();
        if (type == "toggle") {
            if (!first) cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            auto boundary = QTextBlockFormat(); boundary.setProperty(LayoutBoundaryProperty, true);
            boundary.setLineHeight(1, QTextBlockFormat::FixedHeight);
            cursor.setBlockFormat(boundary);
            QTextFrameFormat format;
            format.setProperty(NativeFrameProperty, QJsonDocument(object).toJson(QJsonDocument::Compact));
            auto *frame = cursor.insertFrame(format);
            cursor = frame->firstCursorPosition(); first = true;
            auto header = object; header.remove("children");
            insertObject(header);
            cursor.insertBlock(boundary, QTextCharFormat());
            QTextFrameFormat bodyFormat; bodyFormat.setLeftMargin(24);
            bodyFormat.setProperty(NativeFrameProperty, QByteArray("{\"type\":\"toggle-body\"}"));
            auto *body = cursor.insertFrame(bodyFormat);
            cursor = body->firstCursorPosition(); first = true;
            for (const auto &child : children) insertEntry(child.toObject());
            if (children.isEmpty()) insertObject(QJsonObject{{"type", "paragraph"}, {"text", ""}, {"canonical_placeholder", true}});
            cursor = QTextCursor(document()); cursor.setPosition(body->lastPosition() + 1); cursor.setBlockFormat(boundary);
            cursor.setPosition(frame->lastPosition() + 1); cursor.setBlockFormat(boundary); cursor.setCharFormat(QTextCharFormat());
            first = true; lastInserted = {}; activeList = nullptr; activeListType.clear(); return;
        }
        const bool complexGroup = type == "group" && (object.contains("canonical_node") || std::any_of(children.begin(), children.end(), [](const QJsonValue &child) {
            const auto kind = child.toObject().value("type").toString(); return kind == "table" || kind == "columns" || kind == "group" || kind == "toggle";
        }));
        if (complexGroup) {
            if (!first) cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            auto boundary = QTextBlockFormat(); boundary.setProperty(LayoutBoundaryProperty, true); cursor.setBlockFormat(boundary);
            QTextFrameFormat format; format.setProperty(NativeFrameProperty, QJsonDocument(object).toJson(QJsonDocument::Compact));
            format.setPadding(12); format.setBackground(groupBoxColor(dark_));
            auto *frame = cursor.insertFrame(format); cursor = frame->firstCursorPosition(); first = true;
            for (const auto &child : children) insertEntry(child.toObject());
            if (children.isEmpty()) insertObject(QJsonObject{{"type", "paragraph"}, {"text", ""}, {"canonical_placeholder", true}});
            cursor = QTextCursor(document()); cursor.setPosition(frame->lastPosition() + 1); cursor.setBlockFormat(boundary);
            first = true; lastInserted = {}; activeList = nullptr; activeListType.clear(); return;
        }
        if (value(object, "type") == "group" && !children.isEmpty()) {
            QString groupId = value(object, "id");
            if (groupId.isEmpty()) groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QList<QTextBlock> members;
            for (const auto &child : children) {
                insertObject(child.toObject());
                if (lastInserted.isValid()) members.append(lastInserted);
            }
            if (!members.isEmpty()) applyGroupBox(members, groupId);
        } else {
            insertObject(object);
        }
    };
    for (const auto &entry : blocks) insertEntry(entry.toObject());
    setTextCursor(QTextCursor(document()));
    normalizing_ = false;
    refreshToggleVisibility();
    document()->clearUndoRedoStacks();
    refreshDividerRule();   // si el editor ya tiene tamaño, los divisores cargan a ancho completo
    ensureBlockIds();
    document()->setModified(false);
}
namespace {
struct BlockAction {
    const char *title;
    const char *style;
    const char *icon;
    const char *alias;
    bool hasVariant = false;
};
const QList<BlockAction> blockActions{
    {"Estándar", "paragraph", "typography", "p texto normal", false},
    {"Pequeño", "small", "text-size", "aa small", false},
    {"Encabezado 1", "heading1", "h-1", "h1 header1 titulo1", true},
    {"Encabezado 2", "heading2", "h-2", "h2 header2 titulo2", true},
    {"Encabezado 3", "heading3", "h-3", "h3 header3 titulo3", true},
    {"Encabezado 4", "heading4", "h-4", "h4 header4 titulo4", true},
    {"Lista con viñetas", "bullet", "list", "ul bullet vineta puntos", false},
    {"Lista alfabética", "alphabetic", "list-letters", "alfa abc letras", false},
    {"Lista numerada", "numbered", "list-numbers", "ol 1. numeros lista", false},
    {"Toggle", "toggle", "chevron-right", "toggle desplegable plegable", false},
    {"Tarea", "todo", "circle-check", "task check checkbox todo", false},
    {"Cita", "quote", "quote", "quote blockquote cita", false},
    {"Ícono", "icon", "mood-smile", "emoji icon icono simbolo", false},
    {"Grupo", "group", "square", "grupo callout box contenedor", false},
    {"Matemáticas", "math", "sigma", "math latex formula ecuacion", true},
    {"Código", "code", "code", "code codigo snippet programacion", false},
    {"Línea horizontal", "divider", "minus", "divider hr separador linea ---", false},
    {"Dos columnas", "columns", "layout-columns", "columnas cols 2col dos", false},
    {"Tabla", "table", "table", "tabla grid table matriz", false},
    {"Fecha", "date", "calendar", "fecha date calendario", false},
    {"Fecha y hora", "datetime", "clock", "hora time datetime rango", false},
    {"Ahora", "now", "clock", "ahora now tiempo actual", false},
    {"Enlace web", "web", "external-link", "web link url enlace", false}
};
}

// Registra las líneas de los divisores al ancho real de la columna de texto,
// para que "---" ocupe todo el renglón a nivel 1 (también en diseño amplio) y
// se reajuste al redimensionar. No edita el documento: sólo recursos de
// imagen y una invalidación de layout, sin tocar undo ni disparar guardados.
void RichEditor::refreshDividerRule() {
    if (!document()) return;
    const qreal text = qMax(document()->pageSize().width(), qreal(viewport() ? viewport()->width() : 0))
                       - 2.0 * document()->documentMargin();
    if (text <= 0.0) return;
    if (qFuzzyCompare(text, dividerRuleWidth_)) {
        // clear()/setHtml()/setPlainText() vacían la caché de recursos del
        // documento: si la línea registrada ya no está, hay que registrarla
        // aunque el ancho no haya cambiado.
        const auto pix = document()->resource(QTextDocument::ImageResource,
                                               QUrl("lumen-rule://divider/dark")).value<QPixmap>();
        if (!pix.isNull() && qAbs(pix.width() - text) <= 1.0) return;
    }
    dividerRuleWidth_ = text;
    const int w = qRound(text);
    QPixmap darkPix(w, 2), lightPix(w, 2);
    darkPix.fill(QColor("#383840"));
    lightPix.fill(QColor("#dcdcde"));
    document()->addResource(QTextDocument::ImageResource, QUrl("lumen-rule://divider/dark"), darkPix);
    document()->addResource(QTextDocument::ImageResource, QUrl("lumen-rule://divider/light"), lightPix);
    document()->markContentsDirty(0, document()->characterCount());
}

void RichEditor::insertDivider() {
    auto cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.positionInBlock() > 0) cursor.insertBlock();
    // El separador vive a nivel 1: sale de cualquier lista y no hereda las
    // sangrías ni fondos del bloque donde se dispara ("---", "/" o el menú).
    if (auto *list = cursor.currentList()) list->remove(cursor.block());
    auto format = cursor.blockFormat();
    format.setProperty(BlockStyleProperty, "divider");
    format.clearProperty(EmbedBlockProperty);
    format.clearBackground();
    format.setIndent(0);
    format.setHeadingLevel(0);
    format.setLeftMargin(0);
    format.setRightMargin(0);
    format.setTopMargin(12);
    format.setBottomMargin(12);
    format.setAlignment(Qt::AlignLeft);
    cursor.setBlockFormat(format);
    QTextCharFormat divCharFmt;
    divCharFmt.setFontPointSize(1);
    cursor.setBlockCharFormat(divCharFmt);

    const QUrl resource("lumen-rule://divider/" + QString(dark_ ? "dark" : "light"));
    refreshDividerRule();
    QTextImageFormat imgFmt;
    imgFmt.setName(resource.toString());
    // Sin ancho explícito: la línea toma el ancho de columna registrado en el
    // recurso y se adapta sola al redimensionar la ventana.
    imgFmt.setHeight(2);
    imgFmt.setProperty(BlockStyleProperty, "divider");
    cursor.insertImage(imgFmt);

    auto after = textCursor();
    after.insertBlock(QTextBlockFormat(), QTextCharFormat());
    after.setBlockCharFormat(QTextCharFormat());
    after.setCharFormat(QTextCharFormat());
    cursor.endEditBlock();
    setTextCursor(after);
    ensureBlockIds();
}

void RichEditor::insertTableBlock(int rows, int cols) {
    rows = qBound(1, rows, 1000); cols = qBound(1, cols, 64);
    auto fresh = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    QJsonArray definitions, entries;
    for (int c = 0; c < cols; ++c) definitions.append(QJsonObject{{"id", fresh()}, {"width", 160}});
    for (int r = 0; r < rows; ++r) {
        QJsonArray cells;
        for (int c = 0; c < cols; ++c) cells.append(QJsonObject{{"id", fresh()}, {"type", "table_cell"},
            {"children", QJsonArray{QJsonObject{{"id", fresh()}, {"type", "paragraph"}, {"content", QJsonArray()}}}}});
        entries.append(QJsonObject{{"id", fresh()}, {"type", "table_row"}, {"children", cells}});
    }
    const auto currentId = blockId(textCursor().block());
    auto node = QJsonObject{{"id", fresh()}, {"type", "table"}, {"attrs", QJsonObject{{"columns", definitions}}}, {"children", entries}};
    auto command = QJsonObject{{"type", "insert"}, {"node", node}, {"position", canonicalDocument().value("blocks").toArray().size()}};
    if (textCursor().block().text().isEmpty() && !currentId.isEmpty()) { command["type"] = "replace"; command["node_id"] = currentId; }
    applyCanonicalCommands(QJsonArray{command});
}

void RichEditor::insertColumnsBlock() {
    auto cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.positionInBlock() > 0) cursor.insertBlock();
    QTextTableFormat tableFormat;
    tableFormat.setAlignment(Qt::AlignLeft);
    tableFormat.setBorder(0);
    tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_None);
    tableFormat.setCellSpacing(12);
    tableFormat.setCellPadding(6);
    tableFormat.setLeftMargin(0);
    tableFormat.setRightMargin(0);
    tableFormat.setTopMargin(6);
    tableFormat.setBottomMargin(10);
    tableFormat.setProperty(BlockStyleProperty, "columns");
    QList<QTextLength> columnWidths;
    columnWidths << QTextLength(QTextLength::PercentageLength, 50);
    columnWidths << QTextLength(QTextLength::PercentageLength, 50);
    tableFormat.setColumnWidthConstraints(columnWidths);
    cursor.insertTable(1, 2, tableFormat);
    cursor.endEditBlock();
    ensureBlockIds();
}

void RichEditor::showIconPicker() {
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    popup->setObjectName("editorLayer");
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAccessibleName("Selector de ícono");
    auto *layout = new QGridLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    const QStringList emojis = {
        "💡", "⭐", "📌", "🚀", "📝", "📅", "🎯", "🔍",
        "💬", "⚠️", "✅", "❌", "🧠", "📚", "🎨", "🎧",
        "⚡", "🏷️", "📎", "🔗", "✨", "🔥", "💎", "🌐"
    };

    int row = 0, col = 0;
    for (const auto &emoji : emojis) {
        auto *btn = new QToolButton(popup);
        btn->setText(emoji);
        btn->setFont(QFont("Inter", 16));
        btn->setFixedSize(36, 36);
        btn->setAutoRaise(true);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QToolButton::clicked, this, [this, popup, emoji] {
            popup->close();
            textCursor().insertText(emoji + " ");
            setFocus();
        });
        layout->addWidget(btn, row, col);
        if (++col >= 8) { col = 0; ++row; }
    }
    styleLayer(popup);
    popup->adjustSize();
    positionPopup(popup, textCursor());
    popup->show();
}

qreal RichEditor::baseFontPointSize() const {
    return font().pointSizeF() > 0 ? font().pointSizeF()
        : qMax(1, font().pixelSize()) * 72. / qMax(72, logicalDpiY());
}
qreal RichEditor::blockStylePointSize(const QString &style) const {
    return baseFontPointSize() * (style == "heading1" ? 1.75 : style == "heading2" ? 1.45
        : style == "heading3" ? 1.2 : style == "heading4" ? 1.08 : style == "small" ? .84 : 1.);
}
QTextCharFormat RichEditor::characterFormatForBlockStyle(const QString &style) const {
    QTextCharFormat character;
    character.setFontWeight(style.startsWith("heading") ? QFont::DemiBold : QFont::Normal);
    character.setFontPointSize(blockStylePointSize(style));
    character.setFontFixedPitch(style == "code");
    character.setFontFamilies(style == "code" ? QStringList{QFontDatabase::systemFont(QFontDatabase::FixedFont).family()}
        : font().families());
    return character;
}
void RichEditor::applyBlockCharacterStyle(QTextCursor cursor, const QString &oldStyle, const QString &style) {
    const QTextCharFormat target = characterFormatForBlockStyle(style);
    cursor.mergeBlockCharFormat(target);

    const QTextBlock block = cursor.block();
    const bool oldHeading = oldStyle.startsWith("heading");
    const bool oldSmall = oldStyle == "small";
    const bool oldCode = oldStyle == "code";
    const bool newHeading = style.startsWith("heading");
    const bool newSmall = style == "small";
    const bool newCode = style == "code";
    const bool resetOld = oldHeading || oldSmall || oldCode;
    if (!newHeading && !newSmall && !newCode && !resetOld) {
        if (block.text().isEmpty()) cursor.mergeCharFormat(target);
        return;
    }
    if (block.text().isEmpty()) {
        cursor.mergeCharFormat(target);
        return;
    }

    const qreal baseSize = blockStylePointSize("paragraph");
    const qreal targetSize = target.fontPointSize();
    const QStringList baseFamilies = font().families();
    const QString fixedFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    struct Fragment { int position; int length; QTextCharFormat format; };
    QList<Fragment> fragments;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (fragment.isValid() && fragment.length() > 0)
            fragments.append({fragment.position(), fragment.length(), fragment.charFormat()});
    }
    for (const auto &fragment : fragments) {
        const auto current = fragment.format;
        QTextCharFormat modifier;
        if (newHeading) {
            if (current.fontWeight() < QFont::Bold) modifier.setFontWeight(QFont::DemiBold);
            modifier.setFontPointSize(targetSize);
            if (oldCode) {
                modifier.setFontFixedPitch(false);
                modifier.setFontFamilies(baseFamilies);
            }
        } else if (newSmall) {
            if (oldHeading && current.fontWeight() == QFont::DemiBold) modifier.setFontWeight(QFont::Normal);
            modifier.setFontPointSize(targetSize);
            if (oldCode) {
                modifier.setFontFixedPitch(false);
                modifier.setFontFamilies(baseFamilies);
            }
        } else if (newCode) {
            if (oldHeading && current.fontWeight() == QFont::DemiBold) modifier.setFontWeight(QFont::Normal);
            modifier.setFontPointSize(targetSize);
            modifier.setFontFixedPitch(true);
            modifier.setFontFamilies(QStringList{fixedFamily});
        } else {
            if (oldHeading && current.fontWeight() == QFont::DemiBold) modifier.setFontWeight(QFont::Normal);
            if (resetOld) modifier.setFontPointSize(baseSize);
            if (oldCode) {
                modifier.setFontFixedPitch(false);
                modifier.setFontFamilies(baseFamilies);
            }
        }
        if (modifier.isEmpty()) continue;
        QTextCursor fragmentCursor(document());
        fragmentCursor.setPosition(fragment.position);
        fragmentCursor.setPosition(fragment.position + fragment.length, QTextCursor::KeepAnchor);
        fragmentCursor.mergeCharFormat(modifier);
    }
}
void RichEditor::applyBlockStyle(const QString &requestedStyle) {
    if (requestedStyle == "group") { groupSelection(); return; }
    auto *currentFrame = textCursor().currentFrame();
    const bool existingToggleHeader = currentFrame->firstCursorPosition().block() == textCursor().block()
        && QJsonDocument::fromJson(currentFrame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString() == "toggle";
    if (requestedStyle == "toggle" && existingToggleHeader && textCursor().blockFormat().property(BlockStyleProperty).toString() == "toggle") return;

    if (requestedStyle == "math") { insertMathBlock(); return; }
    if (requestedStyle == "divider") { insertDivider(); return; }
    if (requestedStyle == "columns") { insertColumnsBlock(); return; }
    if (requestedStyle == "table") { insertTableBlock(); return; }
    if (requestedStyle == "toggle" && !existingToggleHeader) {
        auto cursor = textCursor();
        cursor.beginEditBlock();
        cursor.select(QTextCursor::BlockUnderCursor);
        // Select only this paragraph, excluding the preceding separator.
        cursor.setPosition(textCursor().block().position());
        cursor.setPosition(textCursor().block().position() + textCursor().block().length() - 1, QTextCursor::KeepAnchor);
        QTextFrameFormat frameFormat;
        frameFormat.setProperty(NativeFrameProperty, QByteArray("{\"type\":\"toggle\"}"));
        auto *frame = cursor.insertFrame(frameFormat);
        auto header = frame->firstCursorPosition();
        const auto oldMarker = blockMarker(header.block().text());
        if (!oldMarker.isEmpty() && !isToggleMarker(oldMarker)) {
            header.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, oldMarker.size()); header.removeSelectedText();
        }
        auto format = header.blockFormat(); format.setProperty(BlockStyleProperty, "toggle");
        format.setHeadingLevel(0); format.setLeftMargin(0); format.setIndent(0);
        if (header.currentList()) header.currentList()->remove(header.block());
        header.setBlockFormat(format);
        if (!header.block().text().startsWith(QString::fromUtf8("▶ ")) && !header.block().text().startsWith(QString::fromUtf8("▼ ")))
            header.insertText(QString::fromUtf8("▼ "), characterFormatForBlockStyle("toggle"));
        auto bodyCursor = frame->lastCursorPosition();
        auto boundary = QTextBlockFormat(); boundary.setProperty(LayoutBoundaryProperty, true); boundary.setLineHeight(1, QTextBlockFormat::FixedHeight);
        bodyCursor.insertBlock(boundary, QTextCharFormat());
        QTextFrameFormat bodyFormat; bodyFormat.setLeftMargin(24);
        bodyFormat.setProperty(NativeFrameProperty, QByteArray("{\"type\":\"toggle-body\"}"));
        auto *body = bodyCursor.insertFrame(bodyFormat);
        bodyCursor = body->firstCursorPosition(); bodyCursor.setBlockFormat(QTextBlockFormat());
        for (const int position : {frame->firstPosition() - 1, body->lastPosition() + 1, frame->lastPosition() + 1}) {
            QTextCursor edge(document()); edge.setPosition(position);
            if (edge.block().text().isEmpty()) edge.setBlockFormat(boundary);
        }
        cursor.endEditBlock();
        header = frame->firstCursorPosition(); header.movePosition(QTextCursor::EndOfBlock); setTextCursor(header);
        ensureBlockIds(); refreshToggleVisibility(); return;
    }
    if (requestedStyle == "icon") { showIconPicker(); return; }
    QString style = requestedStyle, language;
    if (style.startsWith("code:")) { language = LumenSyntax::normalize(style.mid(5)); style = "code"; }
    auto cursor = textCursor();
    cursor.beginEditBlock();

    const QTextBlock initialBlock = cursor.block();
    const QTextBlockFormat initialFormat = initialBlock.blockFormat();
    QString oldStyle = initialFormat.property(BlockStyleProperty).toString();
    const int oldHeadingLevel = initialFormat.headingLevel();
    if (oldHeadingLevel > 0)
        oldStyle = "heading" + QString::number(qBound(1, oldHeadingLevel, 4));
    // La sangría de una lista vive en QTextListFormat, pero Qt también deja el
    // valor en el blockFormat al salir de ella. Si no se limpia, el texto
    // conserva el desplazamiento de la lista y se suma al nuevo estilo.
    const bool hadList = cursor.currentList() != nullptr;
    const int previousListIndent = hadList ? cursor.currentList()->format().indent() : 1;
    const QString marker = blockMarker(initialBlock.text());
    bool keepTodoMarker = style == "todo" && isTodoMarker(marker);
    const bool keepToggleMarker = style == "toggle" && isToggleMarker(marker);
    if (style == "todo" && (marker == QStringLiteral("[ ] ") || marker == QStringLiteral("[x] ") ||
                            marker == QStringLiteral("[X] "))) {
        QTextCursor canonical(initialBlock);
        canonical.movePosition(QTextCursor::StartOfBlock);
        canonical.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, marker.size());
        canonical.insertText(marker == QStringLiteral("[ ] ") ? QString::fromUtf8("○ ") : QString::fromUtf8("✔ "),
                             characterFormatForBlockStyle("todo"));
        keepTodoMarker = true;
    } else if (!marker.isEmpty() && !keepTodoMarker && !keepToggleMarker) {
        QTextCursor remove(initialBlock);
        remove.movePosition(QTextCursor::StartOfBlock);
        remove.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, marker.size());
        remove.removeSelectedText();
    }

    if (style == "bullet" || style == "numbered" || style == "alphabetic") {
        auto block = cursor.blockFormat();
        block.clearProperty(BlockStyleProperty); block.clearProperty(EmbedBlockProperty);
        block.setHeadingLevel(0); block.setLeftMargin(0); block.setIndent(0); block.clearBackground();
        cursor.setBlockFormat(block);
        QTextListFormat format;
        format.setStyle(style == "bullet" ? QTextListFormat::ListDisc
            : style == "numbered" ? QTextListFormat::ListDecimal : QTextListFormat::ListLowerAlpha);
        format.setIndent(qBound(1, previousListIndent, 6));
        cursor.createList(format);
    } else if (style == "todo") {
        if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
        auto format = cursor.blockFormat();
        format.clearProperty(EmbedBlockProperty);
        format.clearBackground();
        format.setProperty(BlockStyleProperty, "todo");
        format.setHeadingLevel(0);
        format.setLeftMargin(0);
        if (hadList) format.setIndent(0);
        format.setBottomMargin(8);
        cursor.setBlockFormat(format);
    } else if (style == "toggle") {
        if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
        auto format = cursor.blockFormat();
        format.clearProperty(EmbedBlockProperty);
        format.clearBackground();
        format.setProperty(BlockStyleProperty, "toggle");
        format.setHeadingLevel(0);
        format.setLeftMargin(0);
        if (hadList) format.setIndent(0);
        format.setBottomMargin(6);
        cursor.setBlockFormat(format);
    } else if (style == "group") {
        if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
        auto format = cursor.blockFormat();
        format.clearProperty(EmbedBlockProperty);
        format.setProperty(BlockStyleProperty, "group");
        format.setHeadingLevel(0);
        format.setLeftMargin(16);
        format.setRightMargin(16);
        if (hadList) format.setIndent(0);
        format.setTopMargin(8);
        format.setBottomMargin(8);
        format.setBackground(QColor(dark_ ? "#222228" : "#f4f4f7"));
        cursor.setBlockFormat(format);
    } else {
        if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
        auto format = cursor.blockFormat();
        format.clearProperty(EmbedBlockProperty);
        format.clearBackground();
        format.setProperty(BlockStyleProperty, style);
        format.clearProperty(CodeLanguageProperty);
        format.setHeadingLevel(style.startsWith("heading") ? qBound(1, style.right(1).toInt(), 4) : 0);
        format.setLeftMargin(style == "quote" ? 22 : 0);
        if (hadList) format.setIndent(0);
        format.setBottomMargin(style.startsWith("heading") ? 14 : 10);
        cursor.setBlockFormat(format);
    }
    {
        auto format = cursor.blockFormat();
        const QString groupId = format.property(GroupBlockProperty).toString();
        if (!groupId.isEmpty()) format.setBackground(groupBoxColor(dark_));
        const QString storedBackground = format.property(BlockBackgroundProperty).toString();
        if (!storedBackground.isEmpty()) format.setBackground(QColor(storedBackground));
        cursor.setBlockFormat(format);
    }
    applyBlockCharacterStyle(cursor, oldStyle, style);
    if (style == "todo" && !keepTodoMarker) {
        QTextCursor insertCur(cursor.block());
        insertCur.insertText(QString::fromUtf8("○ "), characterFormatForBlockStyle("todo"));
    } else if (style == "toggle" && !keepToggleMarker) {
        QTextCursor insertCur(cursor.block());
        insertCur.insertText(QString::fromUtf8("▶ "), characterFormatForBlockStyle("toggle"));
    }
    if ((style == "todo" || style == "toggle") && !cursor.hasSelection()) {
        const int afterMarker = cursor.block().position() + 2;
        if (cursor.position() < afterMarker) cursor.setPosition(afterMarker);
    }
    if (style == "code") styleCodeBlock(cursor, language);
    cursor.endEditBlock();
    setTextCursor(cursor);
    setFocus();
}
void RichEditor::showBlockMenu() {
    if (isReadOnly()) return;
    if (blockPopup_) return;
    beginInlineQuery("/");
    if (selectionToolbar_) selectionToolbar_->hide();
    blockPopup_ = makeInlinePanel("Crear un bloque", "block", true);
    rebuildBlockMenu();
    positionPopup(blockPopup_, queryCursor_, true);
    blockPopup_->show();
    setFocus(Qt::OtherFocusReason);
}
void RichEditor::rebuildBlockMenu() {
    if (!blockResults_) return;
    blockResults_->clear();
    const QString query = inlineQuery_.trimmed();
    const int slashIdx = inlineQuery_.indexOf('/');
    const bool hasSubSlash = slashIdx >= 0;

    QString typeFilter;
    QString subQuery;
    if (hasSubSlash) {
        typeFilter = inlineQuery_.left(slashIdx).trimmed();
        subQuery = inlineQuery_.mid(slashIdx + 1).trimmed();
    }

    auto addBlockItem = [this](const QString &title, const QString &iconName, const QString &style, bool hasVariant) {
        auto *item = new QListWidgetItem(stableIcon(QIcon(LumenIcons::badge(iconName, QColor("#99989e"), 28, dark_)), 28),
            title, blockResults_);
        item->setSizeHint(QSize(390, 41));
        item->setData(Qt::UserRole, style);
        if (hasVariant) {
            auto *dots = new QLabel(blockResults_);
            dots->setPixmap(LumenIcons::icon("dots", QColor(dark_ ? "#6b6b75" : "#a1a1aa"), 16).pixmap(16, 16));
            dots->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            dots->setContentsMargins(0, 0, 10, 0);
            dots->setAttribute(Qt::WA_TransparentForMouseEvents);
            blockResults_->setItemWidget(item, dots);
        }
        return item;
    };

    if (!hasSubSlash) {
        for (const auto &action : blockActions) {
            const QString title = QString::fromUtf8(action.title);
            const QString alias = QString::fromUtf8(action.alias);
            if (!query.isEmpty() && !title.contains(query, Qt::CaseInsensitive) && !alias.contains(query, Qt::CaseInsensitive))
                continue;
            addBlockItem(title, action.icon, action.style, action.hasVariant);
        }
    }

    // Section 2: Crear un objeto
    QJsonArray availableTypes = types_;
    QSet<QString> existingTypeIds;
    for (const auto &entry : availableTypes)
        existingTypeIds.insert(value(entry.toObject(), "id"));

    const QList<QPair<QString, QString>> defaultBaseTypes{
        {"page", "Página"}, {"note", "Nota"}, {"atomic", "Nota atómica"},
        {"task", "Tarea"}, {"book", "Libro"}, {"person", "Persona"},
        {"podcast", "Podcast"}, {"tweet", "Tweet"}, {"audio", "Audio"},
        {"file", "Archivo"}, {"pdf", "PDF"}, {"quote", "Cita"},
        {"tag", "Etiqueta"}, {"web", "Enlace web"}
    };
    for (const auto &pair : defaultBaseTypes) {
        if (!existingTypeIds.contains(pair.first)) {
            availableTypes.append(QJsonObject{{"id", pair.first}, {"name", pair.second}});
            existingTypeIds.insert(pair.first);
        }
    }

    bool addedSectionHeader = false;
    for (const auto &entry : availableTypes) {
        const auto typeObj = entry.toObject();
        const QString tid = value(typeObj, "id");
        if (tid == "query" || tid == "journal") continue;
        const QString tname = typeLabel(tid);

        if (hasSubSlash) {
            if (tid.compare(typeFilter, Qt::CaseInsensitive) != 0 &&
                tname.compare(typeFilter, Qt::CaseInsensitive) != 0 &&
                !tname.contains(typeFilter, Qt::CaseInsensitive))
                continue;
        } else if (!query.isEmpty()) {
            const bool matches = tname.contains(query, Qt::CaseInsensitive) ||
                                 tid.contains(query, Qt::CaseInsensitive) ||
                                 QString("Nuevo " + tname).contains(query, Qt::CaseInsensitive) ||
                                 QString("Crear " + tname).contains(query, Qt::CaseInsensitive);
            if (!matches) continue;
        }

        if (!addedSectionHeader && blockResults_->count() > 0) {
            auto *headerItem = new QListWidgetItem("Crear un objeto", blockResults_);
            headerItem->setFlags(Qt::NoItemFlags);
            headerItem->setSizeHint(QSize(390, 28));
            auto *headerLabel = new QLabel("Crear un objeto", blockResults_);
            headerLabel->setObjectName("inlineMenuHeading");
            headerLabel->setStyleSheet("color: #969397; font-size: 13px; font-weight: 600; padding: 6px 8px 2px 8px;");
            blockResults_->setItemWidget(headerItem, headerLabel);
            addedSectionHeader = true;
        }

        const QString labelText = (hasSubSlash && !subQuery.isEmpty())
            ? ("Crear «" + subQuery + "» · " + tname)
            : ("Nuevo " + tname);

        auto *item = new QListWidgetItem(typeIcon(tid, 28), labelText, blockResults_);
        item->setSizeHint(QSize(390, 41));
        item->setData(Qt::UserRole, "create_object");
        item->setData(Qt::UserRole + 1, tid);
        item->setData(Qt::UserRole + 2, hasSubSlash && !subQuery.isEmpty() ? subQuery : QString());

        if (tid == "task") {
            auto *rocket = new QLabel(blockResults_);
            rocket->setPixmap(LumenIcons::icon("rocket", QColor("#38bdf8"), 16).pixmap(16, 16));
            rocket->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            rocket->setContentsMargins(0, 0, 10, 0);
            rocket->setAttribute(Qt::WA_TransparentForMouseEvents);
            blockResults_->setItemWidget(item, rocket);
        }
    }

    if (blockResults_->count() == 0) {
        auto *item = new QListWidgetItem("No hay coincidencias", blockResults_);
        item->setFlags(Qt::NoItemFlags);
    } else {
        for (int i = 0; i < blockResults_->count(); ++i) {
            if (blockResults_->item(i)->flags() & Qt::ItemIsEnabled) {
                blockResults_->setCurrentRow(i);
                break;
            }
        }
    }
}
void RichEditor::selectBlock() {
    if (!blockResults_) return;
    auto *item = blockResults_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsEnabled)) return;
    const auto style = item->data(Qt::UserRole).toString();
    updatingPicker_ = true;
    auto transaction = queryCursor_;
    transaction.beginEditBlock();
    auto cursor = consumeInlineQuery();
    closeInlineMenu();
    setTextCursor(cursor);
    if (style == "create_object") {
        const auto typeId = item->data(Qt::UserRole + 1).toString();
        const auto objTitle = item->data(Qt::UserRole + 2).toString();
        transaction.endEditBlock();
        ensureBlockIds();
        if (referenceCreateRequested) {
            QPointer<RichEditor> guard(this);
            const auto generation = contentGeneration_;
            referenceCreateRequested(typeId, objTitle, [guard, cursor, generation](QJsonObject created) {
                if (!guard || created.isEmpty() || guard->contentGeneration_ != generation || cursor.document() != guard->document()) return;
                guard->insertReferenceAt(cursor, created);
            });
        }
        return;
    }
    if (style == "reference" || style == "embed") showReferencePicker(style == "embed");
    else if (style == "web") showLinkEditor(bool(webEmbedRequested));
    else if (style == "date" || style == "datetime") {
        transaction.endEditBlock(); showTemporalPicker(style == "datetime"); return;
    }
    else if (style == "now") {
        const auto now = QDateTime::currentDateTime();
        insertTemporalReference(QJsonObject{{"kind", "datetime"}, {"start", now.toString(Qt::ISODate)},
            {"timezone", QString::fromUtf8(QTimeZone::systemTimeZoneId())}, {"display_hint", "time"}});
    }
    else if (style == "icon") showIconPicker();
    else if (style == "divider") insertDivider();
    else if (style == "columns") insertColumnsBlock();
    else if (style == "table") insertTableBlock();
    else applyBlockStyle(style);
    transaction.endEditBlock();
    ensureBlockIds();
}
bool RichEditor::handleInlineKey(QKeyEvent *event) {
    if (!referencePopup_ && !blockPopup_) return false;
    if (event->key() == Qt::Key_Escape) { closeInlineMenu(true); return true; }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (referencePopup_) selectReference(); else selectBlock();
        return true;
    }
    if (referencePopup_ && referenceTypeId_ == "journal" && referenceCalendar_) {
        if ((event->key() == Qt::Key_Left && event->modifiers() & Qt::AltModifier)
            || (event->key() == Qt::Key_Backspace && inlineQuery_.isEmpty())) {
            referenceTypeId_.clear(); rebuildReferences(); return true;
        }
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down || event->key() == Qt::Key_Right || event->key() == Qt::Key_Left) {
            const int days = event->key() == Qt::Key_Up ? -7 : event->key() == Qt::Key_Down ? 7 : event->key() == Qt::Key_Left ? -1 : 1;
            referenceCalendar_->setSelectedDate(referenceCalendar_->selectedDate().addDays(days));
            return true;
        }
    }
    auto *list = referencePopup_ ? referenceResults_.data() : blockResults_.data();
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        const int row = list->currentRow() + (event->key() == Qt::Key_Down ? 1 : -1);
        list->setCurrentRow(qBound(0, row, list->count() - 1));
        list->scrollToItem(list->currentItem());
        return true;
    }
    if (referencePopup_ && event->key() == Qt::Key_Right && list->currentItem()
        && !list->currentItem()->data(Qt::UserRole + 3).toString().isEmpty()) {
        selectReference(); return true;
    }
    if (referencePopup_ && event->key() == Qt::Key_Left && !referenceTypeId_.isEmpty()) {
        referenceTypeId_.clear(); rebuildReferences(); return true;
    }
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo)) closeInlineMenu();
    return false;
}
void RichEditor::showLinkEditor(bool embed) {
    if (linkPopup_) { linkPopup_->raise(); return; }
    selectionCursor_ = textCursor();
    if (selectionToolbar_) selectionToolbar_->hide();
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    linkPopup_ = popup;
    popup->setObjectName("editorLayer");
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setFixedWidth(350);
    popup->setAccessibleName("Editar enlace de la selección");
    auto *layout = new QVBoxLayout(popup);
    auto *title = new QLineEdit(selectionCursor_.selectedText(), popup);
    title->setPlaceholderText("Texto del enlace");
    title->setAccessibleName("Texto visible del enlace");
    auto *url = new QLineEdit(popup);
    url->setObjectName("linkUrl");
    url->setPlaceholderText("https://…");
    url->setAccessibleName("Dirección HTTPS del enlace");
    url->setText(selectionCursor_.charFormat().anchorHref());
    auto *hint = new QLabel("Las direcciones se abren en el navegador al hacer Ctrl+clic.", popup);
    hint->setWordWrap(true);
    auto *apply = new QPushButton(embed ? "Guardar como objeto web" : "Guardar enlace", popup);
    layout->addWidget(title);
    layout->addWidget(url);
    layout->addWidget(hint);
    layout->addWidget(apply);
    auto commit = [this, popup, title, url, hint, embed] {
        const QUrl target(url->text().trimmed());
        if ((target.scheme() != "https" && target.scheme() != "http") || target.host().isEmpty() || !target.userInfo().isEmpty()) {
            hint->setText("Introduce una dirección web completa, sin credenciales.");
            return;
        }
        auto cursor = selectionCursor_;
        if (embed && webEmbedRequested) {
            QPointer<RichEditor> guard(this);
            popup->close();
            setFocus();
            const auto generation = contentGeneration_;
            webEmbedRequested(target.toString(), [guard, cursor, generation](QJsonObject object) mutable {
                if (!guard || object.isEmpty() || guard->contentGeneration_ != generation || cursor.document() != guard->document()) return;
                auto active = guard->textCursor();
                const bool stillAtOrigin = active.position() == cursor.position();
                const int scroll = guard->verticalScrollBar()->value();
                guard->insertReferenceBlockAt(cursor, object);
                const auto after = guard->textCursor();
                guard->setTextCursor(stillAtOrigin ? after : active);
                if (!stillAtOrigin) guard->verticalScrollBar()->setValue(scroll);
            });
            return;
        }
        QTextCharFormat format;
        format.setAnchor(true);
        format.setAnchorHref(target.toString());
        format.setForeground(QColor(dark_ ? "#d2b7f5" : "#7b589d"));
        format.setFontUnderline(true);
        cursor.insertText(title->text().isEmpty() ? target.toDisplayString() : title->text(), format);
        setTextCursor(cursor);
        popup->close();
        setFocus();
    };
    connect(apply, &QPushButton::clicked, this, commit);
    connect(url, &QLineEdit::returnPressed, this, commit);
    styleLayer(popup);
    popup->adjustSize();
    positionPopup(popup, selectionCursor_);
    popup->show();
    url->setFocus();
}
void RichEditor::updateSelectionToolbar() {
    if (property("lumenTableSelectionActive").toBool()) { if (selectionToolbar_) selectionToolbar_->hide(); return; }
    if (!textCursor().hasSelection() || !hasFocus() || isReadOnly() || referencePopup_ || blockPopup_ || linkPopup_) {
        if (selectionToolbar_) selectionToolbar_->hide();
        return;
    }
    selectionCursor_ = textCursor();
    if (!selectionToolbar_) {
        auto *toolbar = new QFrame(this);
        selectionToolbar_ = toolbar;
        toolbar->setObjectName("editorLayer");
        toolbar->setAccessibleName("Formato de la selección");
        auto *layout = new QHBoxLayout(toolbar);
        layout->setContentsMargins(4, 3, 4, 3);
        layout->setSpacing(1);
        const QList<QPair<QString, QString>> actions = {{"Negrita", "bold"}, {"Cursiva", "italic"}, {"Subrayado", "underline"}, {"Tachado", "strike"}};
        for (const auto &entry : actions) {
            auto *button = tool(entry.first, entry.second == "strike" ? "strikethrough" : entry.second, toolbar);
            const QString title = entry.second == "bold" ? "Negrita · Ctrl+B" : entry.second == "italic" ? "Cursiva · Ctrl+I" : entry.second == "underline" ? "Subrayado" : "Tachado";
            button->setAccessibleName(title);
            button->setToolTip(title);
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QToolButton::clicked, this, [this, kind = entry.second] {
                setTextCursor(selectionCursor_);
                auto current = currentCharFormat();
                QTextCharFormat format;
                if (kind == "bold") format.setFontWeight(current.fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
                if (kind == "italic") format.setFontItalic(!current.fontItalic());
                if (kind == "underline") format.setFontUnderline(!current.fontUnderline());
                if (kind == "strike") format.setFontStrikeOut(!current.fontStrikeOut());
                mergeCurrentCharFormat(format);
                setFocus();
            });
            layout->addWidget(button);
        }
        auto *link = tool("Enlace · Ctrl+K", "link", toolbar);
        link->setFocusPolicy(Qt::NoFocus);
        connect(link, &QToolButton::clicked, this, [this] { setTextCursor(selectionCursor_); showLinkEditor(); });
        layout->addWidget(link);
        auto *reference = tool("Enlazar objeto · @", "plus", toolbar);
        reference->setFocusPolicy(Qt::NoFocus);
        connect(reference, &QToolButton::clicked, this, [this] { setTextCursor(selectionCursor_); showReferencePicker(); });
        layout->addWidget(reference);
        auto *block = tool("Acciones de bloque · Ctrl+Shift+/", "dots", toolbar);
        block->setFocusPolicy(Qt::NoFocus);
        connect(block, &QToolButton::clicked, this, [this] { showBlockMenu(); });
        layout->addWidget(block);
        styleLayer(toolbar);
        toolbar->adjustSize();
    }
    positionPopup(selectionToolbar_, selectionCursor_, true);
    selectionToolbar_->show();
    selectionToolbar_->raise();
}

QWidget *RichEditor::findBarHost() {
    // The editor is embedded in a QScrollArea whose viewport is what the user
    // actually sees. The bar must stay pinned to the visible pane, not scroll
    // away with the (document-height) editor. Standalone editors fall back to
    // their own viewport.
    for (QWidget *widget = parentWidget(); widget; widget = widget->parentWidget()) {
        if (auto *scroll = qobject_cast<QScrollArea *>(widget))
            return scroll->viewport();
    }
    return viewport();
}

void RichEditor::ensureFindBar() {
    if (findBar_) return;
    auto *bar = new QFrame;
    findBar_ = bar;
    bar->setObjectName("findBar");
    bar->setProperty("editorMenuKind", "find");
    bar->setAccessibleName("Find in object");
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(8, 6, 8, 6);
    outer->setSpacing(4);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    findQueryEdit_ = new QLineEdit(bar);
    findQueryEdit_->setObjectName("findQuery");
    findQueryEdit_->setAccessibleName("Find in this object");
    findQueryEdit_->setPlaceholderText("Find in this object");
    findQueryEdit_->setClearButtonEnabled(true);
    findQueryEdit_->setFixedWidth(230);
    findQueryEdit_->installEventFilter(this);
    connect(findQueryEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (findUpdating_) return;
        findQuery_ = text;
        findNoticeText_.clear();
        refreshFindMatches(true);
        if (findStateChanged) findStateChanged();
    });
    row->addWidget(findQueryEdit_);

    findCounter_ = new QLabel(bar);
    findCounter_->setObjectName("findCounter");
    findCounter_->setAccessibleName("Match count");
    findCounter_->setAlignment(Qt::AlignCenter);
    findCounter_->setMinimumWidth(46);
    row->addWidget(findCounter_);

    findPrevButton_ = tool("Previous match · Shift+Enter", "chevron-up", bar);
    findPrevButton_->setObjectName("findPrev");
    findPrevButton_->setFixedSize(28, 28);
    findPrevButton_->setFocusPolicy(Qt::NoFocus);
    connect(findPrevButton_, &QToolButton::clicked, this, [this] { findPreviousMatch(); });
    row->addWidget(findPrevButton_);

    findNextButton_ = tool("Next match · Enter", "chevron-down", bar);
    findNextButton_->setObjectName("findNext");
    findNextButton_->setFixedSize(28, 28);
    findNextButton_->setFocusPolicy(Qt::NoFocus);
    connect(findNextButton_, &QToolButton::clicked, this, [this] { findNextMatch(); });
    row->addWidget(findNextButton_);

    findOptionsButton_ = new QToolButton(bar);
    findOptionsButton_->setObjectName("findOptions");
    findOptionsButton_->setText("Aa");
    findOptionsButton_->setToolTip("Search options");
    findOptionsButton_->setAccessibleName("Search options");
    findOptionsButton_->setCursor(Qt::PointingHandCursor);
    findOptionsButton_->setAutoRaise(true);
    findOptionsButton_->setFixedSize(30, 28);
    findOptionsButton_->setFocusPolicy(Qt::NoFocus);
    auto *optionsMenu = new QMenu(findOptionsButton_);
    findMatchCaseAction_ = optionsMenu->addAction("Match case");
    findMatchCaseAction_->setObjectName("findMatchCase");
    findMatchCaseAction_->setCheckable(true);
    connect(findMatchCaseAction_, &QAction::toggled, this, [this](bool enabled) { setFindMatchCase(enabled); });
    findWholeWordAction_ = optionsMenu->addAction("Whole word");
    findWholeWordAction_->setObjectName("findWholeWord");
    findWholeWordAction_->setCheckable(true);
    connect(findWholeWordAction_, &QAction::toggled, this, [this](bool enabled) { setFindWholeWord(enabled); });
    findOptionsButton_->setMenu(optionsMenu);
    findOptionsButton_->setPopupMode(QToolButton::InstantPopup);
    row->addWidget(findOptionsButton_);

    findReplaceToggle_ = new QToolButton(bar);
    findReplaceToggle_->setObjectName("findReplaceToggle");
    findReplaceToggle_->setIcon(actionIcon("arrow-forward"));
    findReplaceToggle_->setToolTip("Replace · Ctrl+R");
    findReplaceToggle_->setAccessibleName("Replace");
    findReplaceToggle_->setCursor(Qt::PointingHandCursor);
    findReplaceToggle_->setAutoRaise(true);
    findReplaceToggle_->setCheckable(true);
    findReplaceToggle_->setFixedSize(30, 28);
    findReplaceToggle_->setFocusPolicy(Qt::NoFocus);
    connect(findReplaceToggle_, &QToolButton::toggled, this, [this](bool visible) { setFindReplaceVisible(visible); });
    row->addWidget(findReplaceToggle_);

    findCloseButton_ = tool("Close · Esc", "x", bar);
    findCloseButton_->setObjectName("findClose");
    findCloseButton_->setFixedSize(28, 28);
    findCloseButton_->setFocusPolicy(Qt::NoFocus);
    connect(findCloseButton_, &QToolButton::clicked, this, [this] { closeFindBar(); });
    row->addWidget(findCloseButton_);
    outer->addLayout(row);

    findReplaceRow_ = new QWidget(bar);
    auto *replaceRow = new QHBoxLayout(findReplaceRow_);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(4);
    findReplaceEdit_ = new QLineEdit(findReplaceRow_);
    findReplaceEdit_->setObjectName("findReplace");
    findReplaceEdit_->setAccessibleName("Replace with");
    findReplaceEdit_->setPlaceholderText("Replace with");
    findReplaceEdit_->setFixedWidth(230);
    findReplaceEdit_->installEventFilter(this);
    replaceRow->addWidget(findReplaceEdit_);
    auto *replaceOne = new QPushButton("Replace", findReplaceRow_);
    replaceOne->setObjectName("findReplaceOne");
    replaceOne->setFocusPolicy(Qt::NoFocus);
    connect(replaceOne, &QPushButton::clicked, this, [this] {
        if (findReplaceEdit_) replaceFindCurrent(findReplaceEdit_->text());
    });
    replaceRow->addWidget(replaceOne);
    auto *replaceAll = new QPushButton("Replace all", findReplaceRow_);
    replaceAll->setObjectName("findReplaceAll");
    replaceAll->setFocusPolicy(Qt::NoFocus);
    connect(replaceAll, &QPushButton::clicked, this, [this] {
        if (findReplaceEdit_) replaceFindAll(findReplaceEdit_->text());
    });
    replaceRow->addWidget(replaceAll);
    outer->addWidget(findReplaceRow_);

    findNoticeLabel_ = new QLabel(bar);
    findNoticeLabel_->setObjectName("findNotice");
    findNoticeLabel_->setWordWrap(true);
    findNoticeLabel_->setMaximumWidth(420);
    findNoticeLabel_->hide();
    outer->addWidget(findNoticeLabel_);

    styleLayer(bar);
    updateFindBarIcons();
    {
        QSignalBlocker queryBlock(findQueryEdit_);
        findQueryEdit_->setText(findQuery_);
    }
    {
        QSignalBlocker caseBlock(findMatchCaseAction_);
        findMatchCaseAction_->setChecked(findMatchCase_);
    }
    {
        QSignalBlocker wordBlock(findWholeWordAction_);
        findWholeWordAction_->setChecked(findWholeWord_);
    }
    {
        QSignalBlocker replaceBlock(findReplaceToggle_);
        findReplaceToggle_->setChecked(findReplaceVisible_);
    }
    findReplaceRow_->setVisible(findReplaceVisible_);
    bar->hide();
    updateFindUi();
}

void RichEditor::positionFindBar() {
    if (!findBar_) return;
    QWidget *host = findBarHost();
    const bool wasVisible = findBar_->isVisible();
    if (findBar_->parentWidget() != host) {
        if (findBarHost_) findBarHost_->removeEventFilter(this);
        findBar_->setParent(host);
        findBarHost_ = host;
        host->installEventFilter(this);
    }
    findBar_->adjustSize();
    // Pin the bar to the top-right of the text column: map the editor's right
    // edge into the scroll viewport so the bar sits over the page, not over
    // the empty gutter of a wide window.
    const QPoint rightEdge = host == viewport()
                                 ? QPoint(viewport()->width(), 0)
                                 : viewport()->mapTo(host, QPoint(viewport()->width(), 0));
    const int margin = 18;
    const int x = qBound(8, rightEdge.x() - findBar_->width() - 8,
                         qMax(8, host->width() - findBar_->width() - 8));
    findBar_->move(x, margin);
    if (wasVisible) findBar_->show();
    findBar_->raise();
}

void RichEditor::showFindBar(bool showReplace) {
    ensureFindBar();
    const bool wasVisible = findBar_->isVisible();
    if (showReplace) setFindReplaceVisible(true);
    positionFindBar();
    findBar_->show();
    findBar_->raise();
    if (!wasVisible) {
        if (findQuery_.isEmpty() && textCursor().hasSelection()) {
            QString seed = textCursor().selectedText();
            seed.replace(QChar(QChar::ParagraphSeparator), ' ');
            seed.replace(QChar(QChar::LineSeparator), ' ');
            seed = seed.trimmed();
            if (!seed.isEmpty() && seed.size() <= 200) {
                findQueryEdit_->setText(seed);   // textChanged refreshes
            } else {
                refreshFindMatches(true);
            }
        } else {
            refreshFindMatches(true);
        }
    }
    findQueryEdit_->setFocus(Qt::OtherFocusReason);
    findQueryEdit_->selectAll();
}

void RichEditor::closeFindBar() {
    if (!findBar_) return;
    findBar_->hide();
    rebuildFindSelections();   // clears the transient match highlights
    setFocus(Qt::OtherFocusReason);
}

bool RichEditor::findBarVisible() const {
    return findBar_ && findBar_->isVisible();
}

void RichEditor::setFindSession(const QString &query, bool matchCase, bool wholeWord, bool replaceVisible) {
    findQuery_ = query;
    findMatchCase_ = matchCase;
    findWholeWord_ = wholeWord;
    findReplaceVisible_ = replaceVisible;
    if (findQueryEdit_) {
        QSignalBlocker block(findQueryEdit_);
        findQueryEdit_->setText(query);
    }
    if (findMatchCaseAction_) {
        QSignalBlocker block(findMatchCaseAction_);
        findMatchCaseAction_->setChecked(matchCase);
    }
    if (findWholeWordAction_) {
        QSignalBlocker block(findWholeWordAction_);
        findWholeWordAction_->setChecked(wholeWord);
    }
    if (findReplaceToggle_) {
        QSignalBlocker block(findReplaceToggle_);
        findReplaceToggle_->setChecked(replaceVisible);
    }
    if (findReplaceRow_) findReplaceRow_->setVisible(replaceVisible);
    if (findBarVisible()) refreshFindMatches(true);
}

void RichEditor::setFindMatchCase(bool enabled) {
    if (findMatchCase_ == enabled) return;
    findMatchCase_ = enabled;
    if (findMatchCaseAction_) {
        QSignalBlocker block(findMatchCaseAction_);
        findMatchCaseAction_->setChecked(enabled);
    }
    findNoticeText_.clear();
    refreshFindMatches(true);
    if (findStateChanged) findStateChanged();
}

void RichEditor::setFindWholeWord(bool enabled) {
    if (findWholeWord_ == enabled) return;
    findWholeWord_ = enabled;
    if (findWholeWordAction_) {
        QSignalBlocker block(findWholeWordAction_);
        findWholeWordAction_->setChecked(enabled);
    }
    findNoticeText_.clear();
    refreshFindMatches(true);
    if (findStateChanged) findStateChanged();
}

void RichEditor::setFindReplaceVisible(bool visible) {
    findReplaceVisible_ = visible;
    if (findReplaceToggle_) {
        QSignalBlocker block(findReplaceToggle_);
        findReplaceToggle_->setChecked(visible);
    }
    if (findReplaceRow_) findReplaceRow_->setVisible(visible);
    if (findBar_) {
        findBar_->adjustSize();
        positionFindBar();
    }
    if (findStateChanged) findStateChanged();
}

void RichEditor::classifyFindMatch(FindMatch &match) const {
    QTextCursor cursor(document());
    cursor.setPosition(match.start);
    // Table cells remain searchable. Bulk replacement in cells still needs
    // separate cursor/undo acceptance; skip it until that path is validated.
    for (QTextFrame *frame = cursor.currentFrame(); frame; frame = frame->parentFrame()) {
        if (qobject_cast<QTextTable *>(frame)) {
            match.table = true;
            break;
        }
    }
    const QTextBlock block = cursor.block();
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (!fragment.isValid()) continue;
        const int fragmentStart = fragment.position();
        const int fragmentEnd = fragmentStart + fragment.length();
        if (fragmentEnd <= match.start || fragmentStart >= match.end) continue;
        const auto format = fragment.charFormat();
        if (isReferenceChipFormat(format)) {
            // Object/block reference titles are link text, not editable prose.
            match.readOnly = true;
        } else if (format.isImageFormat()) {
            // Dividers, formulas and images are object-replacement characters;
            // a query containing U+FFFC must never delete them.
            match.readOnly = true;
            match.image = true;
        }
    }
}

void RichEditor::refreshFindMatches(bool selectFirst) {
    const int previousStart =
        (findIndex_ >= 0 && findIndex_ < int(findMatches_.size())) ? findMatches_.at(findIndex_).start : -1;
    findMatches_.clear();
    findIndex_ = -1;
    if (!findQuery_.isEmpty()) {
        QTextDocument::FindFlags flags;
        if (findMatchCase_) flags |= QTextDocument::FindCaseSensitively;
        if (findWholeWord_) flags |= QTextDocument::FindWholeWords;
        int from = 0;
        const int limit = document()->characterCount();
        while (from < limit) {
            const QTextCursor found = document()->find(findQuery_, from, flags);
            if (found.isNull()) break;
            FindMatch match{found.selectionStart(), found.selectionEnd(), false, false, false};
            if (match.end <= match.start) break;
            classifyFindMatch(match);
            findMatches_.append(match);
            from = match.end;
        }
    }
    if (selectFirst && !findMatches_.isEmpty()) {
        selectFindMatch(0);
        return;
    }
    if (previousStart >= 0) {
        for (int i = 0; i < int(findMatches_.size()); ++i) {
            if (findMatches_.at(i).start >= previousStart) {
                findIndex_ = i;
                break;
            }
        }
    }
    updateFindUi();
}

void RichEditor::selectFindMatch(int index) {
    if (index < 0 || index >= int(findMatches_.size())) return;
    findIndex_ = index;
    const FindMatch match = findMatches_.at(index);
    // Reveal the match by opening its collapsed ancestors. This is a document
    // state change, shared with the other clients just like a triangle click.
    QTextCursor target(document()); target.setPosition(match.start);
    for (auto *frame = target.currentFrame(); frame; frame = frame->parentFrame()) {
        const auto kind = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
        if (kind == "toggle" && frame->firstCursorPosition().block().text().startsWith(QString::fromUtf8("▶ "))) {
            auto marker = frame->firstCursorPosition(); marker.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor); marker.insertText(QString::fromUtf8("▼"));
        }
    }
    refreshToggleVisibility();
    QTextCursor cursor(document());
    cursor.setPosition(match.start);
    cursor.setPosition(match.end, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    ensureCursorVisible();
    if (revealFindMatch) {
        QTextCursor reveal(document());
        reveal.setPosition(match.start);
        QRect rect = cursorRect(reveal);
        rect.setHeight(qMax(rect.height(), fontMetrics().height()));
        revealFindMatch(rect);
    }
    updateFindUi();
}

void RichEditor::updateFindUi() {
    if (!findBar_) return;
    const int count = int(findMatches_.size());
    const int current = findIndex_ >= 0 ? findIndex_ + 1 : 0;
    if (findCounter_) {
        if (findQuery_.isEmpty()) findCounter_->clear();
        else findCounter_->setText(QString("%1/%2").arg(current).arg(count));
    }
    if (findPrevButton_) findPrevButton_->setEnabled(count > 0);
    if (findNextButton_) findNextButton_->setEnabled(count > 0);
    if (findNoticeLabel_) {
        QString notice = findNoticeText_;
        if (notice.isEmpty() && !findQuery_.isEmpty() && count == 0) notice = "No matches";
        findNoticeLabel_->setText(notice);
        findNoticeLabel_->setVisible(!notice.isEmpty());
    }
    if (findOptionsButton_) {
        QStringList active;
        if (findMatchCase_) active << "match case";
        if (findWholeWord_) active << "whole word";
        findOptionsButton_->setToolTip(active.isEmpty() ? "Search options"
                                                        : "Search options · " + active.join(", "));
    }
    rebuildFindSelections();
}

void RichEditor::rebuildFindSelections() {
    findSelections_.clear();
    if (findBarVisible() && !findMatches_.isEmpty()) {
        const QColor matchColor(dark_ ? "#3a3320" : "#fdf3c8");
        const QColor currentColor(dark_ ? "#6b5520" : "#f7d774");
        findSelections_.reserve(findMatches_.size());
        for (int i = 0; i < int(findMatches_.size()); ++i) {
            const FindMatch &match = findMatches_.at(i);
            QTextEdit::ExtraSelection selection;
            selection.cursor = QTextCursor(document());
            selection.cursor.setPosition(match.start);
            selection.cursor.setPosition(match.end, QTextCursor::KeepAnchor);
            selection.format.setBackground(i == findIndex_ ? currentColor : matchColor);
            findSelections_.append(selection);
        }
    }
    applyExtraSelections();
}

void RichEditor::applyExtraSelections() {
    // setExtraSelections() replaces the whole list: keep the focusBlock()
    // flash and the find highlights in one union.
    QList<QTextEdit::ExtraSelection> all = findSelections_;
    all += flashSelections_;
    setExtraSelections(all);
}

void RichEditor::updateFindBarIcons() {
    const QColor color(dark_ ? "#c9c8cf" : "#5f5e66");
    if (findPrevButton_) findPrevButton_->setIcon(LumenIcons::icon("chevron-up", color, 16));
    if (findNextButton_) findNextButton_->setIcon(LumenIcons::icon("chevron-down", color, 16));
    if (findReplaceToggle_) findReplaceToggle_->setIcon(LumenIcons::icon("arrow-forward", color, 16));
    if (findCloseButton_) findCloseButton_->setIcon(LumenIcons::icon("x", color, 16));
}

bool RichEditor::findNextMatch() {
    if (findQuery_.isEmpty()) {
        showFindBar();
        return false;
    }
    if (!findBarVisible() || findMatches_.isEmpty()) refreshFindMatches(false);
    if (findMatches_.isEmpty()) {
        updateFindUi();
        return false;
    }
    const int count = int(findMatches_.size());
    const int next = findIndex_ < 0 ? 0 : (findIndex_ + 1) % count;
    selectFindMatch(next);
    return true;
}

bool RichEditor::findPreviousMatch() {
    if (findQuery_.isEmpty()) {
        showFindBar();
        return false;
    }
    if (!findBarVisible() || findMatches_.isEmpty()) refreshFindMatches(false);
    if (findMatches_.isEmpty()) {
        updateFindUi();
        return false;
    }
    const int count = int(findMatches_.size());
    const int previous = findIndex_ < 0 ? count - 1 : (findIndex_ - 1 + count) % count;
    selectFindMatch(previous);
    return true;
}

bool RichEditor::replaceFindCurrent(const QString &replacement) {
    // Refresh unconditionally: the live refresh is a 0 ms timer, so a document
    // edit immediately followed by Replace could otherwise use stale offsets.
    refreshFindMatches(false);
    if (findIndex_ < 0 || findIndex_ >= int(findMatches_.size())) return false;
    const FindMatch match = findMatches_.at(findIndex_);
    if (match.readOnly || match.table) {
        findNoticeText_ = match.table ? "Table cells are not replaceable yet"
                                      : match.image ? "Embedded objects are read-only"
                                                    : "Reference titles are read-only";
        if (int(findMatches_.size()) > 1) selectFindMatch((findIndex_ + 1) % int(findMatches_.size()));
        else updateFindUi();
        return false;
    }
    findUpdating_ = true;
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.setPosition(match.start);
    cursor.setPosition(match.end, QTextCursor::KeepAnchor);
    cursor.insertText(replacement);
    cursor.endEditBlock();
    findUpdating_ = false;
    const int after = match.start + replacement.size();
    findNoticeText_.clear();
    refreshFindMatches(false);
    int next = -1;
    for (int i = 0; i < int(findMatches_.size()); ++i) {
        if (findMatches_.at(i).start >= after) {
            next = i;
            break;
        }
    }
    if (next < 0 && !findMatches_.isEmpty()) next = 0;
    if (next >= 0) selectFindMatch(next);
    else updateFindUi();
    return true;
}

int RichEditor::replaceFindAll(const QString &replacement) {
    // Same stale-offset guard as replaceFindCurrent().
    refreshFindMatches(false);
    QList<FindMatch> targets;
    int skippedChips = 0, skippedTables = 0, skippedImages = 0;
    for (const auto &match : findMatches_) {
        if (match.table) {
            ++skippedTables;
            continue;
        }
        if (match.readOnly) {
            if (match.image) ++skippedImages;
            else ++skippedChips;
            continue;
        }
        targets.append(match);
    }
    auto countText = [](int count, const QString &singular, const QString &plural) {
        return QString("%1 %2").arg(count).arg(count == 1 ? singular : plural);
    };
    QStringList skipped;
    if (skippedChips > 0) skipped << countText(skippedChips, "reference title", "reference titles");
    if (skippedTables > 0) skipped << countText(skippedTables, "table cell", "table cells");
    if (skippedImages > 0) skipped << countText(skippedImages, "embedded object", "embedded objects");
    if (targets.isEmpty()) {
        findNoticeText_ = skipped.isEmpty() ? QString() : "Not replaceable: " + skipped.join(", ");
        updateFindUi();
        return 0;
    }
    findUpdating_ = true;
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    for (int i = int(targets.size()) - 1; i >= 0; --i) {
        cursor.setPosition(targets.at(i).start);
        cursor.setPosition(targets.at(i).end, QTextCursor::KeepAnchor);
        cursor.insertText(replacement);
    }
    cursor.endEditBlock();
    findUpdating_ = false;
    const int firstStart = targets.first().start;
    refreshFindMatches(false);
    QTextCursor after(document());
    after.setPosition(qBound(0, firstStart, qMax(0, document()->characterCount() - 1)));
    setTextCursor(after);
    const int replaced = int(targets.size());
    findNoticeText_ = QString("Replaced %1 occurrence%2").arg(replaced).arg(replaced == 1 ? "" : "s");
    if (!skipped.isEmpty()) findNoticeText_ += " · skipped " + skipped.join(", ");
    updateFindUi();
    return replaced;
}

bool RichEditor::canInsertFromMimeData(const QMimeData *source) const {
    if (!source) return false;
    if (source->hasFormat(LumenClipboard::Mime) || source->hasImage()) return true;
    if (source->hasUrls()) {
        for (const auto &url : source->urls()) {
            if (url.isLocalFile()) {
                const auto ext = QFileInfo(url.toLocalFile()).suffix().toLower();
                if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" || ext == "bmp" || ext == "gif")
                    return true;
            }
        }
    }
    return QTextEdit::canInsertFromMimeData(source);
}
void RichEditor::insertFromMimeData(const QMimeData *source) {
    if (!source) return;
    if (!isReadOnly() && source->hasText() && !source->hasImage() &&
        textCursor().block().blockFormat().property(BlockStyleProperty).toString() == "code") {
        // Pasted code stays inside one block: lines are separated by U+2028, not by new blocks.
        QString text = source->text();
        text.replace("\r\n", "\n").replace('\r', '\n').replace('\n', QChar(QChar::LineSeparator));
        text.replace(QChar(QChar::ParagraphSeparator), QChar(QChar::LineSeparator));
        textCursor().insertText(text);
        ensureCursorVisible();
        return;
    }
    if (!isReadOnly() && source->hasFormat(LumenClipboard::Mime)) { pasteStructuredMime(source); return; }
    if (!isReadOnly() && source->hasText() && !source->hasImage() && !source->hasUrls()) {
        const QString text = source->text().trimmed();
        if (text.startsWith("lumen://block/") && QUrl(text, QUrl::StrictMode).scheme() == "lumen"
            && !text.contains(' ') && !text.contains('\n')) {
            insertBlockReferencePaste(text);
            return;
        }
    }
    if (imagePasted) {
        if (source->hasImage()) {
            const auto image = qvariant_cast<QImage>(source->imageData());
            if (!image.isNull()) {
                imagePasted(image);
                return;
            }
        }
        if (source->hasUrls()) {
            for (const auto &url : source->urls()) {
                if (url.isLocalFile()) {
                    QImage image(url.toLocalFile());
                    if (!image.isNull()) {
                        imagePasted(image);
                        return;
                    }
                }
            }
        }
    }
    if (webEmbedRequested && source->hasText() && !source->hasUrls()) {
        const QString text = source->text().trimmed();
        if ((text.startsWith("http://", Qt::CaseInsensitive) || text.startsWith("https://", Qt::CaseInsensitive))
            && !text.contains(' ') && !text.contains('\n') && QUrl(text).isValid()) {
            auto cursor = textCursor();
            QPointer<RichEditor> guard(this);
            const auto generation = contentGeneration_;
            webEmbedRequested(text, [guard, cursor, generation](QJsonObject obj) {
                if (!guard || obj.isEmpty() || guard->contentGeneration_ != generation || cursor.document() != guard->document()) return;
                guard->insertReferenceBlockAt(cursor, obj);
            });
            return;
        }
    }
    if (isReadOnly()) return;
    if (source->hasHtml()) { pasteNormalizedHtml(source->html()); return; }
    if (source->hasText()) { pastePlainText(source->text()); return; }
    QTextEdit::insertFromMimeData(source);
}
bool RichEditor::handleReferenceDeletion(QKeyEvent *event) {
    if (event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete)
        return false;
    if (isReadOnly())
        return false;

    const auto spans = referenceSpans(document());
    if (spans.isEmpty())
        return false;

    auto cursor = textCursor();
    if (cursor.hasSelection()) {
        int selStart = cursor.selectionStart();
        int selEnd = cursor.selectionEnd();
        bool intersects = false;
        for (const auto &span : spans) {
            if (selStart < span.end && selEnd > span.start) {
                intersects = true;
                selStart = qMin(selStart, span.start);
                selEnd = qMax(selEnd, span.end);
            }
        }
        if (intersects) {
            cursor.beginEditBlock();
            cursor.setPosition(selStart);
            cursor.setPosition(selEnd, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.endEditBlock();
            setTextCursor(cursor);
            ensureBlockIds();
            return true;
        }
        return false;
    }

    const int pos = cursor.position();
    const bool isBackspace = event->key() == Qt::Key_Backspace;

    for (const auto &span : spans) {
        bool match = false;
        if (isBackspace) {
            if (pos > span.start && pos <= span.end) {
                match = true;
            }
        } else { // Delete
            if (pos >= span.start && pos < span.end) {
                match = true;
            }
        }

        if (match) {
            cursor.beginEditBlock();
            int endPos = span.end;
            if (endPos < document()->characterCount() - 1 && document()->characterAt(endPos) == ' ') {
                endPos++;
            }
            cursor.setPosition(span.start);
            cursor.setPosition(endPos, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            cursor.endEditBlock();
            setTextCursor(cursor);
            ensureBlockIds();
            return true;
        }
    }
    return false;
}

bool RichEditor::handleReferenceNavigation(QKeyEvent *event) {
    if (textCursor().hasSelection()) return false;
    const int pos = textCursor().position();
    const auto spans = referenceSpans(document());
    for (const auto &span : spans) {
        if (pos > span.start && pos < span.end) {
            auto cursor = textCursor();
            if (event->key() == Qt::Key_Left) {
                cursor.setPosition(span.start);
                setTextCursor(cursor);
                return true;
            } else if (event->key() == Qt::Key_Right) {
                cursor.setPosition(span.end);
                setTextCursor(cursor);
                return true;
            } else if (!event->text().isEmpty() && event->text().at(0).isPrint()) {
                cursor.setPosition(span.end);
                setTextCursor(cursor);
                setCurrentCharFormat(QTextCharFormat());
                return false;
            }
        }
    }
    return false;
}

bool RichEditor::handleMarkdownPrefix(QKeyEvent *event) {
    if (event->key() != Qt::Key_Space || (event->modifiers() != Qt::NoModifier && event->modifiers() != Qt::ShiftModifier))
        return false;
    auto cursor = textCursor();
    if (cursor.hasSelection()) return false;
    const auto block = cursor.block();
    const QString style = block.blockFormat().property(BlockStyleProperty).toString();
    if (style == "code") return false;
    const int posInBlock = cursor.positionInBlock();
    const QString prefix = block.text().left(posInBlock);

    QString targetStyle;
    int removeLen = 0;
    bool checkedTodo = false;

    if (prefix == "#") { targetStyle = "heading1"; removeLen = 1; }
    else if (prefix == "##") { targetStyle = "heading2"; removeLen = 2; }
    else if (prefix == "###") { targetStyle = "heading3"; removeLen = 3; }
    else if (prefix == "####") { targetStyle = "heading4"; removeLen = 4; }
    else if (prefix == "-" || prefix == "*" || prefix == "+") { targetStyle = "bullet"; removeLen = 1; }
    else if (prefix == "1.") { targetStyle = "numbered"; removeLen = 2; }
    else if (prefix == "a." || prefix == "A.") { targetStyle = "alphabetic"; removeLen = 2; }
    else if (prefix == "()" || prefix == "[]" || prefix == "[ ]") { targetStyle = "todo"; removeLen = prefix.length(); }
    else if (prefix == "[x]" || prefix == "[X]") { targetStyle = "todo"; removeLen = prefix.length(); checkedTodo = true; }
    else if (prefix == ">" || prefix == "\"") { targetStyle = "quote"; removeLen = 1; }
    else if (prefix == ">>") { targetStyle = "group"; removeLen = 2; }

    if (removeLen > 0) {
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, removeLen);
        cursor.removeSelectedText();
        applyBlockStyle(targetStyle);
        if (checkedTodo) {
            auto blk = textCursor().block();
            if (blk.text().startsWith(QString::fromUtf8("○ "))) {
                QTextCursor tc(blk);
                tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                tc.insertText(QString::fromUtf8("✔ "));
            }
        }
        cursor.endEditBlock();
        return true;
    }
    return false;
}

bool RichEditor::handleInlineMarkdown(QKeyEvent *event) {
    if (isReadOnly() || textCursor().hasSelection()) return false;
    auto cursor = textCursor();
    const auto block = cursor.block();
    if (block.blockFormat().property(BlockStyleProperty).toString() == "code") return false;
    const QString prefix = block.text().left(cursor.positionInBlock());
    const QString keyText = event->text();

    // 1. Horizontal divider trigger: "---" or "***"
    if ((keyText == "-" && prefix == "--") || (keyText == "*" && prefix == "**")) {
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, 2);
        cursor.removeSelectedText();
        cursor.endEditBlock();
        insertDivider();
        return true;
    }

    // 2. Double brackets trigger "[[" for reference picker
    if (keyText == "[" && prefix.endsWith("[")) {
        cursor.beginEditBlock();
        cursor.deletePreviousChar();
        cursor.endEditBlock();
        showReferencePicker();
        return true;
    }

    // 3. Bold: **text** or __text__
    if ((keyText == "*" && prefix.endsWith("*")) || (keyText == "_" && prefix.endsWith("_"))) {
        const QChar delim = keyText.at(0);
        const QString d2 = QString(delim) + delim;
        const int prevEnd = prefix.length() - 1;
        const int openIdx = prefix.lastIndexOf(d2, prevEnd - 2);
        if (openIdx >= 0) {
            const QString inner = prefix.mid(openIdx + 2, prevEnd - (openIdx + 2));
            if (!inner.isEmpty() && !inner.front().isSpace() && !inner.back().isSpace()) {
                cursor.beginEditBlock();
                cursor.setPosition(block.position() + openIdx);
                cursor.setPosition(block.position() + prefix.length(), QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
                QTextCharFormat fmt;
                fmt.setFontWeight(QFont::Bold);
                cursor.insertText(inner, fmt);
                QTextCharFormat normal;
                cursor.insertText(" ", normal);
                cursor.endEditBlock();
                setTextCursor(cursor);
                return true;
            }
        }
    }

    // 4. Strikethrough: ~~text~~
    if (keyText == "~" && prefix.endsWith("~")) {
        const int prevEnd = prefix.length() - 1;
        const int openIdx = prefix.lastIndexOf("~~", prevEnd - 2);
        if (openIdx >= 0) {
            const QString inner = prefix.mid(openIdx + 2, prevEnd - (openIdx + 2));
            if (!inner.isEmpty() && !inner.front().isSpace() && !inner.back().isSpace()) {
                cursor.beginEditBlock();
                cursor.setPosition(block.position() + openIdx);
                cursor.setPosition(block.position() + prefix.length(), QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
                QTextCharFormat fmt;
                fmt.setFontStrikeOut(true);
                cursor.insertText(inner, fmt);
                QTextCharFormat normal;
                cursor.insertText(" ", normal);
                cursor.endEditBlock();
                setTextCursor(cursor);
                return true;
            }
        }
    }

    // 5. Inline code: `text`
    if (keyText == "`") {
        const int openIdx = prefix.lastIndexOf('`');
        if (openIdx >= 0 && (openIdx == 0 || prefix.at(openIdx - 1) != '\\')) {
            const QString inner = prefix.mid(openIdx + 1);
            if (!inner.isEmpty() && !inner.front().isSpace() && !inner.back().isSpace() && !inner.contains('`')) {
                cursor.beginEditBlock();
                cursor.setPosition(block.position() + openIdx);
                cursor.setPosition(block.position() + prefix.length(), QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
                QTextCharFormat fmt;
                fmt.setFontFixedPitch(true);
                fmt.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
                fmt.setBackground(QColor(dark_ ? "#2b2b32" : "#eaeaea"));
                cursor.insertText(inner, fmt);
                QTextCharFormat normal;
                cursor.insertText(" ", normal);
                cursor.endEditBlock();
                setTextCursor(cursor);
                return true;
            }
        }
    }

    return false;
}

void RichEditor::keyPressEvent(QKeyEvent *event) {
    // Find-in-page works even in a read-only editor: it only selects text.
    if (event->matches(QKeySequence::Find)) { showFindBar(); return; }
    if (event->key() == Qt::Key_F3) {
        if (event->modifiers() & Qt::ShiftModifier) findPreviousMatch();
        else findNextMatch();
        return;
    }
    if (event->key() == Qt::Key_R && event->modifiers() == Qt::ControlModifier) {
        if (findBarVisible()) setFindReplaceVisible(!findReplaceVisible_);
        else showFindBar(true);
        return;
    }
    if (isReadOnly()) { QTextEdit::keyPressEvent(event); return; }
    if (event->key() == Qt::Key_V && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
        pastePlainText(QGuiApplication::clipboard()->text()); return;
    }
    const auto nativeInline = QJsonDocument::fromJson(textCursor().charFormat().property(NativeRunProperty).toByteArray()).object().value("canonical_inline").toObject();
    const bool plainTyping = !event->text().isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (!textCursor().hasSelection() && nativeInline.value("type") == "temporal"
        && (plainTyping || event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)) {
        for (auto it = textCursor().block().begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (textCursor().position() < fragment.position() || textCursor().position() > fragment.position() + fragment.length()) continue;
            const auto in = QJsonDocument::fromJson(fragment.charFormat().property(NativeRunProperty).toByteArray()).object().value("canonical_inline").toObject();
            if (in.value("type") != "temporal") continue;
            auto after = textCursor(); after.beginEditBlock(); after.setPosition(fragment.position() + fragment.length());
            if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete) {
                after.setPosition(fragment.position(), QTextCursor::KeepAnchor); after.removeSelectedText();
                setTextCursor(after); setCurrentCharFormat(QTextCharFormat());
            } else { setTextCursor(after); setCurrentCharFormat(QTextCharFormat()); QTextEdit::keyPressEvent(event); }
            after.endEditBlock(); return;
        }
    }
    const auto nativeNode = QJsonDocument::fromJson(textCursor().blockFormat().property(NativeBlockProperty).toByteArray()).object().value("canonical_node").toObject();
    if ((nativeNode.value("type") == "image" || nativeNode.value("type") == "math"
         || textCursor().blockFormat().property(BlockStyleProperty).toString() == "math") && !textCursor().hasSelection()
        && ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
            || (!event->text().isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))))) {
        auto after = textCursor(); after.beginEditBlock(); after.movePosition(QTextCursor::EndOfBlock);
        after.insertBlock(QTextBlockFormat(), QTextCharFormat()); setTextCursor(after);
        if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter) QTextEdit::keyPressEvent(event);
        after.endEditBlock(); ensureBlockIds(); return;
    }
    if (handleInlineKey(event)) return;
    if (event->key() == Qt::Key_Escape && findBarVisible()) { closeFindBar(); return; }
    if (handleReferenceDeletion(event)) return;
    if (handleReferenceNavigation(event)) return;
    if (handleCodeKey(event)) return;
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::ControlModifier) {
        if (auto *table = textCursor().currentTable()) {
            auto after = QTextCursor(document()); after.setPosition(table->lastPosition() + 1);
            setTextCursor(after); insertTableBlock(); return;
        }
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier) {
        if (auto *table = textCursor().currentTable()) {
            const auto node = QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray()).object().value("canonical_node").toObject();
            if (node.value("type") == "table") {
                auto after = QTextCursor(document()); after.beginEditBlock(); after.setPosition(table->lastPosition() + 1);
                if (!after.block().text().isEmpty()) after.insertBlock(QTextBlockFormat(), QTextCharFormat());
                after.setBlockFormat(QTextBlockFormat()); after.setBlockCharFormat(QTextCharFormat()); after.setCharFormat(QTextCharFormat());
                after.endEditBlock(); setTextCursor(after); ensureBlockIds(); return;
            }
        }
    }
    if (handleMarkupShortcut(event)) return;
    if (handleMarkdownPrefix(event)) return;
    if (handleInlineMarkdown(event)) return;
    const auto cursor = textCursor();
    const auto blk = cursor.block();
    const QString bText = blk.text();
    const QString bStyle = blk.blockFormat().property(BlockStyleProperty).toString();
    const bool noMods = event->modifiers() == Qt::NoModifier;
    const bool shiftOnly = event->modifiers() == Qt::ShiftModifier;

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && noMods && !cursor.hasSelection()) {
        auto *frame = cursor.currentFrame();
        auto kind = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
        if (bStyle == "toggle" && kind == "toggle" && !bText.mid(2).trimmed().isEmpty()) {
            QTextCursor transaction = cursor; transaction.beginEditBlock();
            if (bText.startsWith(QString::fromUtf8("▶ "))) {
                QTextCursor marker(blk); marker.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor); marker.insertText(QString::fromUtf8("▼"));
            }
            for (auto *child : frame->childFrames()) {
                const auto childKind = QJsonDocument::fromJson(child->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
                if (childKind == "toggle-body") { setTextCursor(child->firstCursorPosition()); break; }
            }
            transaction.endEditBlock(); refreshToggleVisibility(); return;
        }
        if (kind == "toggle-body" && bText.isEmpty() && blk == frame->lastCursorPosition().block()) {
            auto *parent = frame->parentFrame();
            QTextCursor after(document()); after.setPosition(parent->lastPosition() + 1);
            after.setBlockFormat(QTextBlockFormat()); after.setCharFormat(QTextCharFormat());
            setTextCursor(after); ensureBlockIds(); return;
        }
    }

    // Enter en tareas (todo), toggles y listas vacías
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && noMods && !cursor.hasSelection()) {
        const bool hasTodoPrefix = bText.startsWith(QString::fromUtf8("○ ")) || bText.startsWith(QString::fromUtf8("✔ "));
        if (hasTodoPrefix) {
            if (bText.mid(2).trimmed().isEmpty()) {
                QTextCursor tc(blk);
                tc.beginEditBlock();
                tc.movePosition(QTextCursor::StartOfBlock);
                tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                tc.removeSelectedText();
                QTextBlockFormat pFmt = tc.blockFormat();
                pFmt.setProperty(BlockStyleProperty, "paragraph");
                pFmt.setLeftMargin(0);
                pFmt.setTopMargin(0);
                pFmt.setBottomMargin(8);
                tc.setBlockFormat(pFmt);
                tc.endEditBlock();
                setTextCursor(tc);
                ensureBlockIds();
                return;
            }
            QTextCursor tc = cursor;
            tc.beginEditBlock();
            if (tc.positionInBlock() < 2) {
                tc.setPosition(blk.position() + 2);
            }
            QTextBlockFormat newFmt = blk.blockFormat();
            newFmt.setProperty(BlockStyleProperty, "todo");
            newFmt.clearProperty(BlockIdProperty);
            newFmt.setLeftMargin(0);
            newFmt.setTopMargin(3);
            newFmt.setBottomMargin(4);
            tc.insertBlock(newFmt, QTextCharFormat());
            tc.movePosition(QTextCursor::StartOfBlock);
            tc.insertText(QString::fromUtf8("○ "), QTextCharFormat());
            tc.endEditBlock();
            setTextCursor(tc);
            ensureBlockIds();
            return;
        }
        const bool hasTogglePrefix = bText.startsWith(QString::fromUtf8("▶ ")) || bText.startsWith(QString::fromUtf8("▼ "));
        if (hasTogglePrefix && bText.mid(2).trimmed().isEmpty()) {
            QTextCursor tc(blk);
            tc.beginEditBlock();
            tc.movePosition(QTextCursor::StartOfBlock);
            tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            tc.removeSelectedText();
            QTextBlockFormat pFmt = tc.blockFormat();
            pFmt.setProperty(BlockStyleProperty, "paragraph");
            pFmt.setLeftMargin(0);
            pFmt.setTopMargin(0);
            pFmt.setBottomMargin(8);
            tc.setBlockFormat(pFmt);
            tc.endEditBlock();
            setTextCursor(tc);
            ensureBlockIds();
            return;
        }
        if (blk.textList() && bText.trimmed().isEmpty()) {
            if (blk.textList()->format().indent() > 1) {
                QTextCursor tc(blk);
                tc.beginEditBlock();
                QTextListFormat lf = blk.textList()->format();
                lf.setIndent(lf.indent() - 1);
                tc.createList(lf);
                tc.endEditBlock();
                setTextCursor(tc);
            } else {
                applyBlockStyle("paragraph");
            }
            ensureBlockIds();
            return;
        }
    }

    // Backspace al inicio de tarea, toggle o elemento de lista
    if (event->key() == Qt::Key_Backspace && noMods && !cursor.hasSelection()) {
        const bool hasPrefix = bText.startsWith(QString::fromUtf8("○ ")) || bText.startsWith(QString::fromUtf8("✔ ")) ||
                               bText.startsWith(QString::fromUtf8("▶ ")) || bText.startsWith(QString::fromUtf8("▼ "));
        if (hasPrefix && cursor.positionInBlock() <= 2) {
            QTextCursor tc(blk);
            tc.beginEditBlock();
            tc.movePosition(QTextCursor::StartOfBlock);
            tc.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            tc.removeSelectedText();
            QTextBlockFormat pFmt = tc.blockFormat();
            pFmt.setProperty(BlockStyleProperty, "paragraph");
            pFmt.setLeftMargin(0);
            pFmt.setTopMargin(0);
            pFmt.setBottomMargin(8);
            tc.setBlockFormat(pFmt);
            tc.endEditBlock();
            setTextCursor(tc);
            ensureBlockIds();
            return;
        }
        if (blk.textList() && cursor.positionInBlock() == 0) {
            if (blk.textList()->format().indent() > 1) {
                QTextCursor tc(blk);
                tc.beginEditBlock();
                QTextListFormat lf = blk.textList()->format();
                lf.setIndent(lf.indent() - 1);
                tc.createList(lf);
                tc.endEditBlock();
            } else {
                applyBlockStyle("paragraph");
            }
            ensureBlockIds();
            return;
        }
    }

    // Tab / Shift+Tab: sangría controlada por bloque o lista (nunca tabulador crudo de 80 px)
    if ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) && (noMods || shiftOnly)) {
        const bool outdent = (event->key() == Qt::Key_Backtab) || shiftOnly;
        // A divider keeps the legacy fall-through (raw tab), unchanged.
        if (blk.textList() || bStyle != "divider") {
            applyIndentChange(blk, outdent ? -1 : 1);
            return;
        }
    }

    const auto prefix = cursor.block().text().left(cursor.positionInBlock());
    if (event->text() == "(" && prefix.endsWith('(') && blockSearchRequested) {
        QTextEdit::keyPressEvent(event); showBlockReferencePicker(); return;
    }
    if (event->key() == Qt::Key_K && event->modifiers() == Qt::ControlModifier && cursor.hasSelection()) { showLinkEditor(); return; }
    if (event->key() == Qt::Key_Slash && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) { showBlockMenu(); return; }
    if (event->key() == Qt::Key_D && (event->modifiers() == Qt::ControlModifier) && !cursor.hasSelection()) {
        duplicateBlock(cursor.block());
        return;
    }
    if (event->key() == Qt::Key_G && (event->modifiers() == Qt::ControlModifier)) {
        groupSelection();
        return;
    }
    if (!referencePopup_ && !blockPopup_ && event->text() == "@" && (prefix.isEmpty() || prefix.back().isSpace())) { showReferencePicker(); return; }
    if (!referencePopup_ && !blockPopup_ && event->text() == "/" && (prefix.isEmpty() || prefix.back().isSpace())) {
        if (referenceDataSet_) showBlockMenu();
        else if (slashRequested) slashRequested();
        return;
    }
    QTextEdit::keyPressEvent(event);
    updateInlineQuery();
    ensureBlockIds();
}
void RichEditor::mouseReleaseEvent(QMouseEvent *event) {
    if (resizingTable_ && event->button() == Qt::LeftButton) {
        resizeTransaction_.endEditBlock(); resizingTable_.clear(); resizingColumn_ = -1;
        viewport()->setCursor(Qt::IBeamCursor); event->accept(); return;
    }
    if (event->button() == Qt::LeftButton && !isReadOnly() && !textCursor().hasSelection()) {
        auto hit = cursorForPosition(event->position().toPoint());
        for (auto it = hit.block().begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (hit.position() < fragment.position() || hit.position() >= fragment.position() + fragment.length()) continue;
            const auto original = QJsonDocument::fromJson(fragment.charFormat().property(NativeRunProperty).toByteArray()).object().value("canonical_inline").toObject();
            if (original.value("type") == "temporal") {
                auto chip = QTextCursor(document()); chip.setPosition(fragment.position());
                chip.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                showTemporalPicker(false, chip); event->accept(); return;
            }
        }
        const auto math = mathCursorAt(event->position().toPoint());
        if (math.hasSelection()) { editMathAt(math); event->accept(); return; }
    }
    if (event->button() == Qt::LeftButton) {
        auto cursor = cursorForPosition(event->position().toPoint());
        auto block = cursor.block();
        const QString bText = block.text();
        if (cursor.positionInBlock() <= 2 &&
            (bText.startsWith(QString::fromUtf8("○ ")) || bText.startsWith(QString::fromUtf8("✔ ")))) {
            QTextCursor toggleCur(block);
            toggleCur.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            if (toggleCur.selectedText() == QString::fromUtf8("○ ")) {
                toggleCur.insertText(QString::fromUtf8("✔ "));
            } else if (toggleCur.selectedText() == QString::fromUtf8("✔ ")) {
                toggleCur.insertText(QString::fromUtf8("○ "));
            }
            event->accept();
            return;
        }
        if (cursor.positionInBlock() <= 2 &&
            (bText.startsWith(QString::fromUtf8("▶ ")) || bText.startsWith(QString::fromUtf8("▼ ")))) {
            QTextCursor toggleCur(block);
            toggleCur.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
            if (toggleCur.selectedText() == QString::fromUtf8("▶ ")) {
                toggleCur.insertText(QString::fromUtf8("▼ "));
            } else if (toggleCur.selectedText() == QString::fromUtf8("▼ ")) {
                toggleCur.insertText(QString::fromUtf8("▶ "));
            }
            event->accept();
            return;
        }
    }
    auto href = anchorAt(event->position().toPoint());
    if (href.isEmpty()) {
        auto cursor = cursorForPosition(event->position().toPoint());
        if (cursor.charFormat().isAnchor()) href = cursor.charFormat().anchorHref();
        else {
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            if (cursor.charFormat().isAnchor()) href = cursor.charFormat().anchorHref();
        }
    }
    const QUrl url(href);
    if (!url.isEmpty() && (event->button() == Qt::LeftButton) && (
        (url.scheme() == "lumen" && (url.host() == "object" || url.host() == "block")) || isReadOnly() || (event->modifiers() & Qt::ControlModifier))) {
        if (url.scheme() == "lumen" && url.host() == "object" && openReference)
            openReference(url.path().mid(1));
        else if (url.scheme() == "lumen" && url.host() == "block" && openReference) {
            const auto objectId = QUrlQuery(url).queryItemValue("object_id");
            if (!objectId.isEmpty()) openReference(objectId);
            else if (!url.path().mid(1).isEmpty()) focusBlock(url.path().mid(1));
        }
        else if ((url.scheme() == "https" || url.scheme() == "http") && url.userInfo().isEmpty()) QDesktopServices::openUrl(url);
        event->accept();
        return;
    }
    QTextEdit::mouseReleaseEvent(event);
    updateInlineQuery();
}
int RichEditor::tableResizeColumnAt(const QPoint &point, QTextTable **hit) const {
    int column = -1;
    std::function<void(QTextFrame *)> visit = [&](QTextFrame *frame) {
        if (auto *table = qobject_cast<QTextTable *>(frame)) {
            const auto node = QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray()).object().value("canonical_node").toObject();
            if (node.value("type") == "table") {
                const auto rect = document()->documentLayout()->frameBoundingRect(table).translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
                if (point.y() >= rect.top() && point.y() <= rect.bottom()) {
                    const auto definitions = node.value("attrs").toObject().value("columns").toArray();
                    qreal total = 0, before = 0; for (const auto &v : definitions) total += v.toObject().value("width").toDouble(160);
                    for (int c = 0; c < definitions.size(); ++c) {
                        before += definitions[c].toObject().value("width").toDouble(160);
                        const auto edge = rect.left() + rect.width() * before / qMax(1.0, total);
                        if (qAbs(point.x() - edge) <= 5) { column = c; if (hit) *hit = table; }
                    }
                }
            }
        }
        for (auto *child : frame->childFrames()) visit(child);
    };
    visit(document()->rootFrame()); return column;
}
void RichEditor::mousePressEvent(QMouseEvent *event) {
    QTextTable *table = nullptr;
    const int column = !isReadOnly() && event->button() == Qt::LeftButton ? tableResizeColumnAt(event->position().toPoint(), &table) : -1;
    if (column >= 0 && table) {
        resizingTable_ = table; resizingColumn_ = column; resizingStartX_ = event->position().x();
        const auto node = QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray()).object().value("canonical_node").toObject();
        resizingStartWidth_ = node.value("attrs").toObject().value("columns").toArray()[column].toObject().value("width").toDouble(160);
        resizeTransaction_ = table->firstCursorPosition(); resizeTransaction_.beginEditBlock();
        viewport()->setCursor(Qt::SizeHorCursor); event->accept(); return;
    }
    QTextEdit::mousePressEvent(event);
}
void RichEditor::contextMenuEvent(QContextMenuEvent *event) {
    auto *menu = createStandardContextMenu();
    const auto cursor = cursorForPosition(event->pos());
    auto *table = cursor.currentTable();
    if (table && !isReadOnly()) {
        const auto metadata = QJsonDocument::fromJson(table->format().property(NativeFrameProperty).toByteArray()).object();
        const auto canonical = metadata.value("canonical_node").toObject();
        const auto identity = canonical.value("id").toString();
        const auto kind = canonical.value("type").toString();
        if (!identity.isEmpty()) {
            menu->addSeparator();
            if (kind == "columns") {
                menu->addAction("Pasar a una columna", this, [this, identity] {
                    applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "flatten_columns"}, {"node_id", identity}}});
                });
            } else if (kind == "table") {
                const auto cell = table->cellAt(cursor); const int col = cell.column(), row = cell.row();
                const auto definitions = canonical.value("attrs").toObject().value("columns").toArray();
                const auto rows = canonical.value("children").toArray();
                if (col < definitions.size() && row < rows.size()) {
                    const auto columnID = definitions[col].toObject().value("id").toString();
                    const auto rowID = rows[row].toObject().value("id").toString();
                    auto run = [this, identity](QJsonObject command) { command["node_id"] = identity; QString error;
                        if (!applyCanonicalCommands(QJsonArray{command}, &error)) QToolTip::showText(QCursor::pos(), error, this); };
                    menu->addAction("Seleccionar columna", this, [this, table, col] { auto selection=table->cellAt(0,col).firstCursorPosition(); selection.setPosition(table->cellAt(table->rows()-1,col).lastCursorPosition().position(),QTextCursor::KeepAnchor);setTextCursor(selection); });
                    menu->addAction("Seleccionar fila", this, [this, table, row] { auto selection=table->cellAt(row,0).firstCursorPosition(); selection.setPosition(table->cellAt(row,table->columns()-1).lastCursorPosition().position(),QTextCursor::KeepAnchor);setTextCursor(selection); });
                    menu->addAction("Insertar columna antes", this, [run,col]{run({{"type","table_insert_column"},{"position",col},{"column_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"width",160}});});
                    menu->addAction("Insertar columna después", this, [run,col]{run({{"type","table_insert_column"},{"position",col+1},{"column_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"width",160}});});
                    menu->addAction("Restablecer ancho", this, [run,columnID]{run({{"type","table_set_width"},{"column_id",columnID},{"width",160}});});
                    if(col>0)menu->addAction("Mover columna a la izquierda", this, [run,columnID,col]{run({{"type","table_move_column"},{"column_id",columnID},{"position",col-1}});});
                    if(col+1<definitions.size())menu->addAction("Mover columna a la derecha", this, [run,columnID,col]{run({{"type","table_move_column"},{"column_id",columnID},{"position",col+1}});});
                    if(row>0)menu->addAction("Mover fila arriba", this, [run,rowID,row]{run({{"type","table_move_row"},{"parent_id",rowID},{"position",row-1}});});
                    if(row+1<rows.size())menu->addAction("Mover fila abajo", this, [run,rowID,row]{run({{"type","table_move_row"},{"parent_id",rowID},{"position",row+1}});});
                    menu->addAction("Ordenar ascendente", this, [run,columnID]{run({{"type","table_sort"},{"column_id",columnID},{"direction","asc"}});});
                    menu->addAction("Ordenar descendente", this, [run,columnID]{run({{"type","table_sort"},{"column_id",columnID},{"direction","desc"}});});
                    menu->addAction("Eliminar columna", this, [run,columnID]{run({{"type","table_delete_column"},{"column_id",columnID}});});
                    menu->addAction("Eliminar fila", this, [run,rowID]{run({{"type","table_delete_row"},{"parent_id",rowID}});});
                }
            }
        }
    }
    menu->exec(event->globalPos()); delete menu;
}
void RichEditor::mouseMoveEvent(QMouseEvent *event) {
    if (resizingTable_ && resizingColumn_ >= 0) {
        auto format = resizingTable_->format();
        auto metadata = QJsonDocument::fromJson(format.property(NativeFrameProperty).toByteArray()).object();
        auto node = metadata.value("canonical_node").toObject(); auto attrs = node.value("attrs").toObject();
        auto definitions = attrs.value("columns").toArray();
        auto definition = definitions[resizingColumn_].toObject();
        definition["width"] = qMax(40.0, resizingStartWidth_ + event->position().x() - resizingStartX_); definitions[resizingColumn_] = definition;
        attrs["columns"] = definitions; node["attrs"] = attrs; metadata["canonical_node"] = node;
        format.setProperty(NativeFrameProperty, QJsonDocument(metadata).toJson(QJsonDocument::Compact));
        QList<QTextLength> widths; qreal total = 0;
        for (const auto &v : definitions) { const auto width = v.toObject().value("width").toDouble(160); widths.append(QTextLength(QTextLength::FixedLength, width)); total += width; }
        format.setColumnWidthConstraints(widths); format.setWidth(QTextLength(QTextLength::FixedLength, total));
        resizingTable_->setFormat(format); viewport()->setCursor(Qt::SizeHorCursor); event->accept(); return;
    }
    if (!isReadOnly() && tableResizeColumnAt(event->position().toPoint()) >= 0) { viewport()->setCursor(Qt::SizeHorCursor); return; }
    if (!isReadOnly()) {
        const QPoint pos = event->position().toPoint();
        const auto blk = blockAtViewportPoint(pos);
        if (blk.isValid()) {
            activeHandleBlock_ = blk;
            if (blockHandle_) {
                positionBlockHandle();
                blockHandle_->show();
                blockHandle_->raise();
            }
        }
    }
    if (!isReadOnly() && mathCursorAt(event->position().toPoint()).hasSelection()) {
        viewport()->setCursor(Qt::PointingHandCursor);
        viewport()->setToolTip("Fórmula · clic para editar");
        QTextEdit::mouseMoveEvent(event);
        return;
    }
    auto href = anchorAt(event->position().toPoint());
    if (href.isEmpty()) {
        auto cursor = cursorForPosition(event->position().toPoint());
        if (cursor.charFormat().isAnchor()) href = cursor.charFormat().anchorHref();
        else {
            cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            if (cursor.charFormat().isAnchor()) href = cursor.charFormat().anchorHref();
        }
    }
    viewport()->setCursor(href.isEmpty() ? Qt::IBeamCursor : Qt::PointingHandCursor);
    if (!href.isEmpty()) {
        const QUrl url(href);
        if (url.scheme() == "lumen" && url.host() == "object") {
            QString title;
            for (const auto &entry : objects_) if (value(entry.toObject(), "id") == url.path().mid(1)) title = value(entry.toObject(), "title");
            viewport()->setToolTip(title + (isReadOnly() ? "\nAbrir objeto" : "\nClic para previsualizar · Ctrl+clic para abrir en pestaña objeto"));
        } else viewport()->setToolTip(url.toDisplayString() + "\nClic para previsualizar · Ctrl+clic para abrir en pestaña en el navegador");
    } else viewport()->setToolTip({});
    QTextEdit::mouseMoveEvent(event);
}
void RichEditor::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    refreshDividerRule();
    if (selectionToolbar_ && selectionToolbar_->isVisible()) positionPopup(selectionToolbar_, selectionCursor_, true);
    if (referencePopup_) positionPopup(referencePopup_, queryCursor_);
    if (blockPopup_) positionPopup(blockPopup_, queryCursor_, true);
    if (blockHandle_ && activeHandleBlock_.isValid()) {
        positionBlockHandle();
    }
}
void RichEditor::focusOutEvent(QFocusEvent *event) {
    QTextEdit::focusOutEvent(event);
    if (selectionToolbar_) selectionToolbar_->hide();
    closeInlineMenu(true);
    if (blockHandle_ && !blockHandle_->underMouse()) {
        blockHandle_->hide();
    }
}
void RichEditor::leaveEvent(QEvent *event) {
    if (blockHandle_ && !blockHandle_->underMouse()) {
        blockHandle_->hide();
    }
    QTextEdit::leaveEvent(event);
}

// El tirador debe quedar centrado sobre la línea visible del bloque. Con la
// fórmula anterior (anclado a la parte superior de la línea) el tirador de un
// divisor de 2 px colgaba por debajo de la línea en vez de caber sobre ella.
void RichEditor::positionBlockHandle() {
    if (!blockHandle_ || !activeHandleBlock_.isValid()) return;
    const QRect r = cursorRect(QTextCursor(activeHandleBlock_));
    blockHandle_->move(6, r.top() + (r.height() - blockHandle_->height()) / 2);
}

// Mapa bloque→punto por fila completa (contenido + márgenes verticales), sin
// filtrar por X para que el margen izquierdo del tirador (x = 0..32) funcione
// igual que el área de texto y priorizando la banda del divisor (2 px de alto).
QTextBlock RichEditor::blockAtViewportPoint(const QPoint &point) const {
    auto hit = cursorForPosition(point).block();
    if (!hit.isValid()) return {};
    const qreal docY = point.y() + verticalScrollBar()->value();
    auto containsY = [this, docY](const QTextBlock &b) {
        if (!b.isValid()) return false;
        const QRectF geo = document()->documentLayout()->blockBoundingRect(b);
        const qreal top = geo.top() - b.blockFormat().topMargin();
        const qreal bottom = geo.bottom() + b.blockFormat().bottomMargin();
        return docY >= top && docY <= bottom;
    };
    const QTextBlock candidates[3] = {hit, hit.next(), hit.previous()};
    for (const auto &c : candidates) {
        if (c.isValid() && c.blockFormat().property(BlockStyleProperty).toString() == "divider" && containsY(c)) {
            return c;
        }
    }
    for (const auto &c : candidates) {
        if (containsY(c)) return c;
    }
    return {};
}

bool RichEditor::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ShortcutOverride &&
        (watched == findQueryEdit_ || watched == findReplaceEdit_)) {
        // The find fields own the keyboard while they have focus. Without
        // this, MainWindow's WindowShortcut actions (Ctrl+B/I/N/T/W...) fire
        // while the user types a query. The bar handles its own keys below.
        event->accept();
        return true;
    }
    if (findBarHost_ && watched == findBarHost_ && event->type() == QEvent::Resize) {
        positionFindBar();
    }
    if (event->type() == QEvent::KeyPress && (watched == findQueryEdit_ || watched == findReplaceEdit_)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            closeFindBar();
            return true;
        }
        if (key->matches(QKeySequence::Find)) {
            findQueryEdit_->setFocus(Qt::OtherFocusReason);
            findQueryEdit_->selectAll();
            return true;
        }
        if (key->key() == Qt::Key_R && key->modifiers() == Qt::ControlModifier) {
            setFindReplaceVisible(!findReplaceVisible_);
            return true;
        }
        if (key->key() == Qt::Key_F3) {
            if (key->modifiers() & Qt::ShiftModifier) findPreviousMatch();
            else findNextMatch();
            return true;
        }
        if (key->key() == Qt::Key_Down) {
            findNextMatch();
            return true;
        }
        if (key->key() == Qt::Key_Up) {
            findPreviousMatch();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (watched == findReplaceEdit_ && !(key->modifiers() & Qt::ShiftModifier))
                replaceFindCurrent(findReplaceEdit_->text());
            else if (key->modifiers() & Qt::ShiftModifier)
                findPreviousMatch();
            else
                findNextMatch();
            return true;
        }
    }
    if (watched == blockHandle_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                dragStartPos_ = me->pos();
                isDraggingHandle_ = false;
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            if ((me->buttons() & Qt::LeftButton) &&
                (me->pos() - dragStartPos_).manhattanLength() >= QApplication::startDragDistance()) {
                isDraggingHandle_ = true;
                if (activeHandleBlock_.isValid()) {
                    draggedBlock_ = activeHandleBlock_;
                    auto *drag = new QDrag(this);
                    auto *mime = new QMimeData;
                    mime->setData("application/x-lumen-block", activeHandleBlock_.text().toUtf8());
                    mime->setText(activeHandleBlock_.text());
                    drag->setMimeData(mime);

                    const bool isDiv = activeHandleBlock_.blockFormat().property(BlockStyleProperty).toString() == "divider";
                    const QString previewLabel = isDiv
                        ? QString::fromUtf8("─── Divisor ───")
                        : (activeHandleBlock_.text().isEmpty() ? QStringLiteral("Bloque") : activeHandleBlock_.text());
                    const int dragW = qBound(160, fontMetrics().horizontalAdvance(previewLabel) + 32, 340);
                    QPixmap preview(dragW, 28);
                    preview.fill(dark_ ? QColor("#22222a") : QColor("#f0f0f5"));
                    QPainter p(&preview);
                    p.setPen(dark_ ? QColor("#e2e8f0") : QColor("#1e293b"));
                    p.drawText(preview.rect().adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft,
                               fontMetrics().elidedText(previewLabel, Qt::ElideRight, preview.width() - 20));
                    p.end();
                    drag->setPixmap(preview);
                    drag->setHotSpot(QPoint(12, 14));
                    drag->exec(Qt::MoveAction);
                    isDraggingHandle_ = false;
                    dropIndicatorRect_ = QRect();
                    viewport()->update();
                }
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton && !isDraggingHandle_) {
                if (activeHandleBlock_.isValid()) {
                    showBlockActionMenu(activeHandleBlock_, blockHandle_->mapToGlobal(QPoint(blockHandle_->width() + 4, 0)));
                }
                return true;
            }
            isDraggingHandle_ = false;
        }
    }
    return QTextEdit::eventFilter(watched, event);
}

void RichEditor::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat("application/x-lumen-block") || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    } else {
        QTextEdit::dragEnterEvent(event);
    }
}

void RichEditor::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat("application/x-lumen-block") || event->mimeData()->hasText()) {
        const QPoint pos = event->position().toPoint();
        const auto blk = blockAtViewportPoint(pos);
        if (blk.isValid()) {
            dropTargetBlock_ = blk;
            const QRect r = cursorRect(QTextCursor(blk));
            const int viewportW = viewport()->width();

            // When hovering towards the right portion of the editor viewport, propose creating 2 columns!
            if (pos.x() > viewportW * 0.40) {
                dropMode_ = DropSideColumn;
                const int colX = qMin(viewportW - 12, qMax(pos.x(), r.right() + 12));
                dropIndicatorRect_ = QRect(colX, r.top(), 3, qMax(22, r.height()));
            } else if (pos.y() < r.center().y()) {
                dropMode_ = DropBefore;
                dropIndicatorRect_ = QRect(r.left(), qMax(0, r.top() - 2), qMax(120, viewportW - r.left() - 20), 3);
            } else {
                dropMode_ = DropAfter;
                dropIndicatorRect_ = QRect(r.left(), r.bottom(), qMax(120, viewportW - r.left() - 20), 3);
            }
            viewport()->update();
            event->acceptProposedAction();
            return;
        }
    }
    QTextEdit::dragMoveEvent(event);
}

void RichEditor::dragLeaveEvent(QDragLeaveEvent *event) {
    dropIndicatorRect_ = QRect();
    dropMode_ = DropNone;
    viewport()->update();
    QTextEdit::dragLeaveEvent(event);
}

// Traslada un bloque del documento conservando su contenido y formato completos:
// formato de bloque (estilo, id, márgenes), formato de carácter (imagen del
// divisor, fórmulas, enlaces, negritas) y pertenencia a listas.
void RichEditor::performBlockMove(const QTextBlock &source, const QTextBlock &target, DropMode mode) {
    if (!source.isValid() || !target.isValid()) return;
    const auto sourceNative = QJsonDocument::fromJson(source.blockFormat().property(NativeBlockProperty).toByteArray()).object();
    if (sourceNative.contains("canonical_node") && (mode == DropBefore || mode == DropAfter)) {
        const auto blocks = canonicalDocument().value("blocks").toArray();
        const auto sourceLocation = canonicalLocation(blocks, blockId(source));
        const auto targetLocation = canonicalLocation(blocks, blockId(target));
        if (!sourceLocation.isEmpty() && !targetLocation.isEmpty() && source != target) {
            int position = targetLocation.value("position").toInt() + (mode == DropAfter ? 1 : 0);
            if (sourceLocation.value("parent_id") == targetLocation.value("parent_id") && sourceLocation.value("position").toInt() < position) --position;
            applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "move"}, {"node_id", blockId(source)},
                {"parent_id", targetLocation.value("parent_id")}, {"position", position}}});
        }
        return;
    }
    if (mode != DropBefore && mode != DropAfter) return;
    if (source == target) return;
    auto *sourceFrame = QTextCursor(source).currentFrame();
    const auto sourceKind = QJsonDocument::fromJson(sourceFrame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
    if (sourceKind == "toggle" && source == sourceFrame->firstCursorPosition().block()) {
        if (target.position() >= sourceFrame->firstPosition() - 1 && target.position() <= sourceFrame->lastPosition()) return;
        QTextCursor original(document());
        original.setPosition(sourceFrame->firstPosition() - 1);
        original.setPosition(sourceFrame->lastPosition() + 1, QTextCursor::KeepAnchor);
        const QTextDocumentFragment fragment(original);
        QTextCursor destination(target);
        auto *targetFrame = destination.currentFrame();
        const auto targetKind = QJsonDocument::fromJson(targetFrame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
        if (targetKind == "toggle" && target == targetFrame->firstCursorPosition().block())
            destination.setPosition(mode == DropBefore ? targetFrame->firstPosition() - 1 : targetFrame->lastPosition() + 1);
        else destination.movePosition(mode == DropBefore ? QTextCursor::StartOfBlock : QTextCursor::EndOfBlock);
        destination.beginEditBlock(); destination.insertFragment(fragment); original.removeSelectedText();
        for (auto edge : {destination, original}) if (edge.block().text().isEmpty()) {
            auto format = edge.blockFormat(); format.setProperty(LayoutBoundaryProperty, true); format.setLineHeight(1, QTextBlockFormat::FixedHeight); edge.setBlockFormat(format);
        }
        destination.endEditBlock();
        setTextCursor(destination); ensureBlockIds(); refreshToggleVisibility(); return;
    }
    if (mode == DropBefore && source.next() == target) return;
    if (mode == DropAfter && target.next() == source) return;

    const QTextBlockFormat sourceFormat = source.blockFormat();
    const bool fromList = source.textList() != nullptr;
    const QTextListFormat sourceListFormat = fromList ? source.textList()->format() : QTextListFormat();
    struct Run { QString text; QTextCharFormat format; };
    QList<Run> runs;
    for (auto it = source.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        runs.append({fragment.text(), fragment.charFormat()});
    }

    QTextCursor edit(document());
    edit.beginEditBlock();

    // 1. Insertar primero en el destino mientras `source` y `target` están intactos.
    QTextCursor ins(document());
    QTextBlockFormat cleanSourceFormat = sourceFormat;
    cleanSourceFormat.setObjectIndex(-1);

    if (mode == DropAfter && !target.next().isValid()) {
        ins.movePosition(QTextCursor::End);
        ins.insertBlock(cleanSourceFormat, QTextCharFormat());
        ins.setBlockCharFormat(QTextCharFormat());
        ins.setCharFormat(QTextCharFormat());
        for (const auto &run : runs) ins.insertText(run.text, run.format);
    } else {
        const QTextBlock destBlock = (mode == DropBefore) ? target : target.next();
        const QTextBlockFormat destFmt = destBlock.blockFormat();
        const QTextCharFormat destCharFmt = destBlock.charFormat();
        ins.setPosition(destBlock.position());
        for (const auto &run : runs) ins.insertText(run.text, run.format);
        ins.insertBlock(destFmt, destCharFmt);
        ins.movePosition(QTextCursor::PreviousBlock);
        ins.setBlockFormat(cleanSourceFormat);
        ins.setBlockCharFormat(QTextCharFormat());
    }

    const QTextBlock inserted = ins.block();
    if (fromList) {
        QTextList *current = inserted.textList();
        if (!current || current->format() != sourceListFormat) {
            if (current) current->remove(inserted);
            const QTextBlock adj = mode == DropBefore ? inserted.next() : inserted.previous();
            if (adj.isValid() && adj.textList() && adj.textList()->format() == sourceListFormat &&
                adj != source) {
                adj.textList()->add(inserted);
            } else {
                QTextCursor(inserted).createList(sourceListFormat);
            }
        }
    } else if (inserted.textList()) {
        inserted.textList()->remove(inserted);
    }

    // 2. Eliminar el bloque origen sin contaminar el formato del bloque vecino.
    if (source.textList()) {
        source.textList()->remove(source);
    }
    if (source == document()->begin()) {
        const QTextBlock nextBlk = source.next();
        const QTextBlockFormat nextFmt = nextBlk.blockFormat();
        const QTextCharFormat nextCharFmt = nextBlk.charFormat();
        QTextList *nextList = nextBlk.textList();
        QTextCursor delCur(document());
        delCur.setPosition(0);
        delCur.setPosition(nextBlk.position(), QTextCursor::KeepAnchor);
        delCur.removeSelectedText();
        delCur.setBlockFormat(nextFmt);
        delCur.setBlockCharFormat(nextCharFmt);
        if (nextList) {
            nextList->add(delCur.block());
        } else if (delCur.block().textList()) {
            delCur.block().textList()->remove(delCur.block());
        }
    } else {
        QTextCursor delCur(document());
        delCur.setPosition(source.position() - 1);
        delCur.setPosition(source.position() + source.length() - 1, QTextCursor::KeepAnchor);
        delCur.removeSelectedText();
    }

    // Group parity: a block dragged out of its group leaves the box; a block
    // dropped between two members of the same group joins it. Then the box
    // margins are recomputed so first/last members frame every run again.
    if (inserted.isValid()) {
        const QString movedGroup = inserted.blockFormat().property(GroupBlockProperty).toString();
        const QTextBlock prev = inserted.previous();
        const QTextBlock next = inserted.next();
        const QString prevGroup = prev.isValid() ? prev.blockFormat().property(GroupBlockProperty).toString() : QString();
        const QString nextGroup = next.isValid() ? next.blockFormat().property(GroupBlockProperty).toString() : QString();
        if (!movedGroup.isEmpty() && movedGroup != prevGroup && movedGroup != nextGroup) {
            restoreUngroupedMargins(inserted, dark_);
        } else if (movedGroup.isEmpty() && !prevGroup.isEmpty() && prevGroup == nextGroup) {
            auto format = inserted.blockFormat();
            format.setProperty(GroupBlockProperty, prevGroup);
            QTextCursor join(inserted);
            join.setBlockFormat(format);
            normalizeGroupBoxes();
        } else if (!movedGroup.isEmpty()) {
            normalizeGroupBoxes();
        }
    }

    edit.endEditBlock();
    if (inserted.isValid()) {
        setTextCursor(QTextCursor(inserted));
    }
    ensureBlockIds();
}

void RichEditor::dropEvent(QDropEvent *event) {
    const QString blockMime = "application/x-lumen-block";
    if (event->mimeData()->hasFormat(blockMime) || event->mimeData()->hasText()) {
        const QString text = event->mimeData()->hasFormat(blockMime)
            ? QString::fromUtf8(event->mimeData()->data(blockMime))
            : event->mimeData()->text();

        if (dropTargetBlock_.isValid()) {
            QTextCursor docCur(document());
            docCur.beginEditBlock();

            if (dropMode_ == DropSideColumn && draggedBlock_.isValid()
                && QJsonDocument::fromJson(draggedBlock_.blockFormat().property(NativeBlockProperty).toByteArray()).object().contains("canonical_node")) {
                docCur.endEditBlock();
                const auto blocks = canonicalDocument().value("blocks").toArray();
                const auto source = canonicalLocation(blocks, blockId(draggedBlock_));
                const auto target = canonicalLocation(blocks, blockId(dropTargetBlock_));
                const auto sourceNode = source.value("node").toObject(), targetNode = target.value("node").toObject();
                if (!source.isEmpty() && !target.isEmpty() && sourceNode.value("id") != targetNode.value("id")
                    && canonicalLocation(sourceNode.value("children").toArray(), targetNode.value("id").toString()).isEmpty()
                    && canonicalLocation(targetNode.value("children").toArray(), sourceNode.value("id").toString()).isEmpty()) {
                    auto fresh = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
                    QJsonArray columnNodes;
                    columnNodes.append(QJsonObject{{"id", fresh()}, {"type", "column"}, {"attrs", QJsonObject{{"width", 1}}}, {"children", QJsonArray{targetNode}}});
                    columnNodes.append(QJsonObject{{"id", fresh()}, {"type", "column"}, {"attrs", QJsonObject{{"width", 1}}}, {"children", QJsonArray{sourceNode}}});
                    const auto columns = QJsonObject{{"id", fresh()}, {"type", "columns"}, {"children", columnNodes}};
                    int position = target.value("position").toInt();
                    if (source.value("parent_id") == target.value("parent_id") && source.value("position").toInt() < position) --position;
                    applyCanonicalCommands(QJsonArray{
                        QJsonObject{{"type", "delete"}, {"node_ids", QJsonArray{sourceNode.value("id"), targetNode.value("id")}}},
                        QJsonObject{{"type", "insert"}, {"parent_id", target.value("parent_id")}, {"position", position}, {"node", columns}}});
                }
                dropIndicatorRect_ = QRect(); dropMode_ = DropNone; viewport()->update(); event->acceptProposedAction(); return;
            } else if (dropMode_ == DropSideColumn) {
                // Remove source block if valid in this document
                if (draggedBlock_.isValid()) {
                    QTextCursor delCur(draggedBlock_);
                    delCur.select(QTextCursor::BlockUnderCursor);
                    delCur.removeSelectedText();
                    if (delCur.atBlockEnd() && !delCur.atEnd() && delCur.block().text().isEmpty()) {
                        delCur.deleteChar();
                    }
                }

                // Form 2-column layout beside dropTargetBlock_ (multi-column columns block)
                const QString targetText = dropTargetBlock_.text();
                QTextCursor replaceCur(dropTargetBlock_);
                replaceCur.select(QTextCursor::BlockUnderCursor);
                replaceCur.removeSelectedText();

                QTextTableFormat tableFormat;
                tableFormat.setAlignment(Qt::AlignLeft);
                tableFormat.setBorder(0);
                tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_None);
                tableFormat.setCellSpacing(14);
                tableFormat.setCellPadding(6);
                tableFormat.setLeftMargin(0);
                tableFormat.setRightMargin(0);
                tableFormat.setTopMargin(6);
                tableFormat.setBottomMargin(10);
                tableFormat.setProperty(BlockStyleProperty, "columns");
                QList<QTextLength> columnWidths;
                columnWidths << QTextLength(QTextLength::PercentageLength, 50);
                columnWidths << QTextLength(QTextLength::PercentageLength, 50);
                tableFormat.setColumnWidthConstraints(columnWidths);

                QTextTable *table = replaceCur.insertTable(1, 2, tableFormat);
                QTextCursor c0 = table->cellAt(0, 0).firstCursorPosition();
                c0.insertText(targetText.isEmpty() ? " " : targetText);
                QTextCursor c1 = table->cellAt(0, 1).firstCursorPosition();
                c1.insertText(text.isEmpty() ? " " : text);
                setTextCursor(c1);
            } else if (draggedBlock_.isValid()) {
                // Traslado interno: conserva el bloque completo (formato, id,
                // imagen del divisor, pertenencia a listas).
                performBlockMove(draggedBlock_, dropTargetBlock_, dropMode_);
            } else {
                // Arrastre externo de texto: se inserta como texto plano.
                if (dropMode_ == DropBefore) {
                    QTextCursor insCur(dropTargetBlock_);
                    insCur.movePosition(QTextCursor::StartOfBlock);
                    insCur.insertText(text + "\n");
                    setTextCursor(insCur);
                } else { // DropAfter
                    QTextCursor insCur(dropTargetBlock_);
                    insCur.movePosition(QTextCursor::EndOfBlock);
                    insCur.insertText("\n" + text);
                    setTextCursor(insCur);
                }
            }

            docCur.endEditBlock();
            ensureBlockIds();
            dropIndicatorRect_ = QRect();
            dropMode_ = DropNone;
            viewport()->update();
            event->acceptProposedAction();
            return;
        }
    }
    dropIndicatorRect_ = QRect();
    dropMode_ = DropNone;
    viewport()->update();
    QTextEdit::dropEvent(event);
}

void RichEditor::refreshToggleVisibility() {
    if (refreshingToggles_) return;
    refreshingToggles_ = true;
    bool changed = false, formatting = false;
    QTextCursor transaction(document());
    std::function<void(QTextFrame *, bool)> visit = [&](QTextFrame *frame, bool visible) {
        const auto kind = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
        const bool open = kind != "toggle" || !frame->firstCursorPosition().block().text().startsWith(QString::fromUtf8("▶ "));
        // Hidden tables/columns must also relinquish their cell padding and
        // borders; hiding text alone leaves an empty table on the page.
        auto format = frame->frameFormat();
        const auto storedStyle = format.property(CollapsedFrameStyleProperty);
        if ((!visible && !storedStyle.isValid()) || (visible && storedStyle.isValid())) {
            if (!visible && frame != document()->rootFrame() && (format.padding() || format.border() || format.topMargin() || format.bottomMargin() || qobject_cast<QTextTable *>(frame))) {
                const auto original = format;
                if (auto *table = qobject_cast<QTextTable *>(frame)) {
                    auto tableFormat = table->format(); tableFormat.setCellPadding(0); tableFormat.setCellSpacing(0); format = tableFormat;
                }
                format.setPadding(0); format.setBorder(0); format.setTopMargin(0); format.setBottomMargin(0); format.clearBackground();
                format.setProperty(CollapsedFrameStyleProperty, QVariant::fromValue(original));
            } else if (visible && storedStyle.isValid()) format = storedStyle.value<QTextFrameFormat>();
            if (format != frame->frameFormat()) {
                if (!formatting) { transaction.joinPreviousEditBlock(); formatting = true; }
                frame->setFrameFormat(format); changed = true;
            }
        }
        for (auto it = frame->begin(); it != frame->end(); ++it) {
            if (auto *child = it.currentFrame()) visit(child, visible && open);
            else if (auto block = it.currentBlock(); block.isValid()) {
                if (block.isVisible() != visible) {
                    block.setVisible(visible); block.setLineCount(visible ? 1 : 0); changed = true;
                }
            }
        }
    };
    visit(document()->rootFrame(), true);
    if (formatting) transaction.endEditBlock();
    refreshingToggles_ = false;
    if (changed) {
        toggleLayoutPending_ = true; idTimer_.start(0);
        document()->markContentsDirty(0, document()->characterCount()); viewport()->update();
    }
}

void RichEditor::paintEvent(QPaintEvent *event) {
    QTextEdit::paintEvent(event);
    QPainter guide(viewport()); guide.setPen(QColor(dark_ ? "#505057" : "#c2c2c8"));
    std::function<void(QTextFrame *)> drawGuides = [&](QTextFrame *frame) {
        const auto kind = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
        if (kind == "toggle-body" && frame->firstCursorPosition().block().isVisible()) {
            const auto top = cursorRect(frame->firstCursorPosition());
            const auto bottom = cursorRect(frame->lastCursorPosition());
            guide.drawLine(top.left() - 14, top.top(), top.left() - 14, bottom.bottom());
        }
        for (auto *child : frame->childFrames()) drawGuides(child);
    };
    drawGuides(document()->rootFrame()); guide.end();
    if (!dropIndicatorRect_.isNull()) {
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(dropIndicatorRect_, QColor("#6366f1"));
        if (dropMode_ == DropSideColumn) {
            painter.setBrush(QColor("#6366f1"));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(dropIndicatorRect_.center().x() - 3, dropIndicatorRect_.top() - 2, 7, 7);
            painter.drawEllipse(dropIndicatorRect_.center().x() - 3, dropIndicatorRect_.bottom() - 4, 7, 7);
        } else {
            painter.setBrush(QColor("#6366f1"));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(dropIndicatorRect_.left() - 3, dropIndicatorRect_.center().y() - 3, 7, 7);
        }
    }
}

void RichEditor::showBlockActionMenu(const QTextBlock &block, const QPoint &globalPos) {
    if (isReadOnly() || !block.isValid()) return;
    QMenu menu(this);
    menu.setObjectName("blockActionMenu");
    menu.addAction("Seleccionar bloque y descendientes", this, [this, identity = blockId(block)] { selectCanonicalBlock(identity); });

    const QString bg = dark_ ? "#1c1c20" : "#ffffff";
    const QString fg = dark_ ? "#e4e3e2" : "#1f1f23";
    const QString border = dark_ ? "#2c2c32" : "#e2e2e7";
    const QString hover = dark_ ? "#2c2c34" : "#f1f1f5";
    menu.setStyleSheet(QString(
        "QMenu#blockActionMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 6px; font-size: 14px; }"
        "QMenu#blockActionMenu::item { padding: 6px 28px 6px 12px; border-radius: 6px; margin: 1px 2px; }"
        "QMenu#blockActionMenu::item:selected { background-color: %4; color: %2; }"
        "QMenu#blockActionMenu::separator { height: 1px; background-color: %3; margin: 5px 6px; }"
    ).arg(bg, fg, border, hover));

    const auto format = block.blockFormat();
    const QString currentStyle = format.property(BlockStyleProperty).toString();
    const int headingLevel = format.headingLevel();
    const bool isBullet = block.textList() && block.textList()->format().style() == QTextListFormat::ListDisc;
    const bool isNumbered = block.textList() && block.textList()->format().style() == QTextListFormat::ListDecimal;
    const bool isAlpha = block.textList() && block.textList()->format().style() == QTextListFormat::ListLowerAlpha;
    const bool isTodo = currentStyle == "todo" || block.text().startsWith("○ ") || block.text().startsWith("✔ ");
    const bool isQuote = currentStyle == "quote" || format.leftMargin() >= 18;
    const bool isToggle = currentStyle == "toggle" || block.text().startsWith("▶ ") || block.text().startsWith("▼ ");
    const bool isGroup = currentStyle == "group";

    auto *turnIntoMenu = menu.addMenu(LumenIcons::icon("typography", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Turn into");
    turnIntoMenu->setStyleSheet(menu.styleSheet());
    auto addTurnInto = [this, turnIntoMenu, &block](const QString &title, const QString &iconName, const QString &style, bool checked) {
        auto *action = turnIntoMenu->addAction(LumenIcons::icon(iconName, QColor(dark_ ? "#9ca3af" : "#6b7280"), 16), title,
            [this, block, style] {
                setTextCursor(QTextCursor(block));
                applyBlockStyle(style);
            });
        action->setCheckable(true);
        action->setChecked(checked);
        return action;
    };
    addTurnInto("Estándar", "typography", "paragraph",
                headingLevel == 0 && !isBullet && !isNumbered && !isAlpha && !isTodo && !isQuote && !isToggle && !isGroup);
    addTurnInto("Pequeño", "text-size", "small", currentStyle == "small");
    addTurnInto("Encabezado 1", "h-1", "heading1", headingLevel == 1);
    addTurnInto("Encabezado 2", "h-2", "heading2", headingLevel == 2);
    addTurnInto("Encabezado 3", "h-3", "heading3", headingLevel == 3);
    addTurnInto("Encabezado 4", "h-4", "heading4", headingLevel == 4);
    turnIntoMenu->addSeparator();
    addTurnInto("Con viñetas", "list", "bullet", isBullet);
    addTurnInto("Numerada", "list-numbers", "numbered", isNumbered);
    addTurnInto("Alfabética", "list-letters", "alphabetic", isAlpha);
    turnIntoMenu->addSeparator();
    addTurnInto("To-do", "circle-check", "todo", isTodo);
    addTurnInto("Citar", "quote", "quote", isQuote);
    addTurnInto("Toggle", "chevron-right", "toggle", isToggle);
    addTurnInto("Código", "code", "code", currentStyle == "code");
    addTurnInto("Matemáticas", "sigma", "math", currentStyle == "math");

    // 2. "Style": per-block text and background colors from the app palette.
    auto *styleMenu = menu.addMenu(LumenIcons::icon("adjustments-horizontal", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Style");
    styleMenu->setStyleSheet(menu.styleSheet());
    const QString storedTextColor = format.property(BlockTextColorProperty).toString();
    const QString storedBackground = format.property(BlockBackgroundProperty).toString();
    auto *textColorMenu = styleMenu->addMenu("Text color");
    textColorMenu->setStyleSheet(menu.styleSheet());
    for (const auto &swatch : textColorPalette()) {
        const QColor color = swatchColor(swatch, dark_);
        auto *action = textColorMenu->addAction(swatchIcon(color, false), swatch.name, [this, block, color, storedBackground] {
            setBlockStyleColors(block, color.name(), storedBackground);
        });
        action->setCheckable(true);
        action->setChecked(storedTextColor == color.name());
    }
    textColorMenu->addAction("Default", [this, block, storedBackground] {
        setBlockStyleColors(block, QString(), storedBackground);
    });
    auto *backgroundColorMenu = styleMenu->addMenu("Background color");
    backgroundColorMenu->setStyleSheet(menu.styleSheet());
    for (const auto &swatch : backgroundPalette()) {
        const QColor color = swatchColor(swatch, dark_);
        auto *action = backgroundColorMenu->addAction(swatchIcon(color, false), swatch.name, [this, block, color, storedTextColor] {
            setBlockStyleColors(block, storedTextColor, color.name());
        });
        action->setCheckable(true);
        action->setChecked(storedBackground == color.name());
    }
    backgroundColorMenu->addAction("Default", [this, block, storedTextColor] {
        setBlockStyleColors(block, storedTextColor, QString());
    });
    styleMenu->addAction("Reset to default", [this, block] {
        setBlockStyleColors(block, QString(), QString());
    });

    menu.addAction(LumenIcons::icon("mood-smile", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Icon...", [this, block] {
        setTextCursor(QTextCursor(block));
        showIconPicker();
    });

    if (blockInGroup(block)) {
        menu.addAction(LumenIcons::icon("square", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Ungroup", [this, block] {
            ungroupBlock(block);
        });
    } else {
        menu.addAction(LumenIcons::icon("square", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Agrupar\tCtrl G", [this, block] {
            if (textCursor().hasSelection())
                groupSelection();
            else {
                setTextCursor(QTextCursor(block));
                applyBlockStyle("group");
            }
        });
    }

    // 5. "Move": move and indent actions using the existing shortcuts.
    auto *moveMenu = menu.addMenu(LumenIcons::icon("arrows-sort", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Move");
    moveMenu->setStyleSheet(menu.styleSheet());
    auto *moveUp = moveMenu->addAction(LumenIcons::icon("arrow-up", QColor(dark_ ? "#9ca3af" : "#6b7280"), 16), "Move up",
        [this, block] { moveBlockUp(block); });
    moveUp->setEnabled(block.previous().isValid());
    auto *moveDown = moveMenu->addAction(LumenIcons::icon("arrow-down", QColor(dark_ ? "#9ca3af" : "#6b7280"), 16), "Move down",
        [this, block] { moveBlockDown(block); });
    moveDown->setEnabled(block.next().isValid());
    auto *indentAction = moveMenu->addAction(LumenIcons::icon("chevron-right", QColor(dark_ ? "#9ca3af" : "#6b7280"), 16), "Indent\tTab",
        [this, block] { indentBlock(block); });
    auto *outdentAction = moveMenu->addAction(LumenIcons::icon("chevron-left", QColor(dark_ ? "#9ca3af" : "#6b7280"), 16), "Outdent\tShift+Tab",
        [this, block] { outdentBlock(block); });
    const bool canIndent = currentStyle != "divider" && (block.textList()
        ? block.textList()->format().indent() < 6 : block.blockFormat().indent() < 6);
    const bool canOutdent = block.textList()
        ? block.textList()->format().indent() > 1 : block.blockFormat().indent() > 0;
    indentAction->setEnabled(canIndent);
    outdentAction->setEnabled(canOutdent);

    menu.addSeparator();

    // 7. Convertir en... >
    auto *convertMenu = menu.addMenu(LumenIcons::icon("box", QColor("#8b5cf6"), 18), "Convertir en...");
    convertMenu->setStyleSheet(menu.styleSheet());

    QJsonArray availableTypes = types_;
    if (availableTypes.isEmpty()) {
        const QList<QPair<QString, QString>> defaults{
            {"page", "Página"}, {"note", "Nota"}, {"atomic", "Nota atómica"},
            {"task", "Tarea"}, {"book", "Libro"}, {"person", "Persona"}
        };
        for (const auto &d : defaults) {
            convertMenu->addAction(typeIcon(d.first, 18), d.second, [this, block, tid = d.first] {
                convertBlockToObject(block, tid);
            });
        }
    } else {
        for (const auto &tVal : availableTypes) {
            const auto tObj = tVal.toObject();
            const QString tid = value(tObj, "id");
            QString tname = value(tObj, "name");
            if (tname.isEmpty()) tname = value(tObj, "title");
            if (tname.isEmpty()) tname = tid;
            convertMenu->addAction(typeIcon(tid, 18), tname, [this, block, tid] {
                convertBlockToObject(block, tid);
            });
        }
    }

    // 8. Copiar >
    auto *copyMenu = menu.addMenu(LumenIcons::icon("copy", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Copiar");
    copyMenu->setStyleSheet(menu.styleSheet());
    copyMenu->addAction("Copiar texto", [block] {
        QGuiApplication::clipboard()->setText(block.text());
    });
    copyMenu->addAction("Copiar bloque", [block] {
        QGuiApplication::clipboard()->setText(block.text());
    });
    copyMenu->addSeparator();
    copyMenu->addAction(LumenIcons::icon("link", QColor(dark_ ? "#9ca3af" : "#6b7280"), 16), "Copy block reference", [this, block] {
        copyBlockReference(block);
    });

    // 9. Duplicar (Ctrl D)
    menu.addAction(LumenIcons::icon("copy", QColor(dark_ ? "#9ca3af" : "#6b7280"), 18), "Duplicar\tCtrl D", [this, block] {
        duplicateBlock(block);
    });

    // 10. Eliminar
    menu.addAction(LumenIcons::icon("x", QColor("#ef4444"), 18), "Eliminar", [this, block] {
        deleteBlock(block);
    });

    menu.exec(globalPos);
}

void RichEditor::convertBlockToObject(const QTextBlock &block, const QString &typeId) {
    if (!block.isValid()) return;
    QString rawText = block.text().trimmed();
    if (rawText.startsWith("○ ") || rawText.startsWith("✔ ") ||
        rawText.startsWith("▶ ") || rawText.startsWith("▼ ")) {
        rawText = rawText.mid(2).trimmed();
    }
    if (rawText.isEmpty()) rawText = "Sin título";

    const QString bId = blockId(block);

    auto onCreated = [this, bId](const QJsonObject &newObj) {
        if (newObj.isEmpty()) return;
        QTextBlock targetBlock;
        for (auto b = document()->begin(); b.isValid(); b = b.next()) {
            if (blockId(b) == bId) {
                targetBlock = b;
                break;
            }
        }
        if (!targetBlock.isValid()) targetBlock = textCursor().block();

        QTextCursor cur(targetBlock);
        cur.beginEditBlock();
        cur.select(QTextCursor::BlockUnderCursor);
        cur.removeSelectedText();
        insertReferenceBlockAt(cur, newObj);
        cur.endEditBlock();
        ensureBlockIds();
    };

    if (convertBlockRequested) {
        convertBlockRequested(typeId, rawText, onCreated);
    } else if (referenceCreateRequested) {
        referenceCreateRequested(typeId, rawText, onCreated);
    }
}

void RichEditor::duplicateBlock(const QTextBlock &block) {
    if (!block.isValid()) return;
    const auto blocks = canonicalDocument().value("blocks").toArray();
    const auto location = canonicalLocation(blocks, blockId(block));
    if (!location.isEmpty()) {
        applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "copy"}, {"node_id", blockId(block)},
            {"parent_id", location.value("parent_id")}, {"position", location.value("position").toInt() + 1}}}); return;
    }
    auto *frame = QTextCursor(block).currentFrame();
    const auto kind = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
    if (kind == "toggle" && block == frame->firstCursorPosition().block()) {
        QTextCursor original(document()); original.setPosition(frame->firstPosition() - 1);
        original.setPosition(frame->lastPosition() + 1, QTextCursor::KeepAnchor);
        const QTextDocumentFragment fragment(original);
        QTextCursor copy(document()); copy.setPosition(frame->lastPosition() + 1); copy.beginEditBlock();
        const int start = copy.position(); copy.insertFragment(fragment); const int end = copy.position();
        QTextCursor beforeCopy(document()); beforeCopy.setPosition(start);
        if (beforeCopy.block().text().isEmpty()) {
            auto boundary = beforeCopy.blockFormat(); boundary.setProperty(LayoutBoundaryProperty, true); boundary.setLineHeight(1, QTextBlockFormat::FixedHeight); beforeCopy.setBlockFormat(boundary);
        }
        QHash<QString, QString> groupIds;
        for (auto child = document()->findBlock(start); child.isValid() && child.position() < end; child = child.next()) {
            auto format = child.blockFormat(); format.clearProperty(BlockIdProperty);
            const auto group = format.property(GroupBlockProperty).toString();
            if (!group.isEmpty()) {
                if (!groupIds.contains(group)) groupIds.insert(group, QUuid::createUuid().toString(QUuid::WithoutBraces));
                format.setProperty(GroupBlockProperty, groupIds.value(group));
            }
            QTextCursor(child).setBlockFormat(format);
        }
        std::function<void(QTextFrame *)> renewFrames = [&](QTextFrame *parent) {
            for (auto *child : parent->childFrames()) {
                if (child->firstPosition() > start && child->lastPosition() < end) {
                    auto format = child->frameFormat();
                    auto metadata = QJsonDocument::fromJson(format.property(NativeFrameProperty).toByteArray()).object();
                    if (metadata.contains("id")) {
                        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces); metadata.insert("id", id);
                        const auto storedStyle = format.property(CollapsedFrameStyleProperty);
                        if (storedStyle.isValid()) {
                            auto restored = storedStyle.value<QTextFrameFormat>();
                            auto native = QJsonDocument::fromJson(restored.property(NativeFrameProperty).toByteArray()).object(); native.insert("id", id);
                            restored.setProperty(NativeFrameProperty, QJsonDocument(native).toJson(QJsonDocument::Compact));
                            format.setProperty(CollapsedFrameStyleProperty, QVariant::fromValue(restored));
                        }
                    }
                    format.setProperty(NativeFrameProperty, QJsonDocument(metadata).toJson(QJsonDocument::Compact)); child->setFrameFormat(format);
                }
                renewFrames(child);
            }
        };
        renewFrames(document()->rootFrame()); ensureBlockIds(); copy.endEditBlock();
        setTextCursor(copy); refreshToggleVisibility(); return;
    }
    struct Run { QString text; QTextCharFormat format; };
    QList<Run> runs;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        runs.append({fragment.text(), fragment.charFormat()});
    }
    QTextBlockFormat dupFmt = block.blockFormat();
    dupFmt.clearProperty(BlockIdProperty);
    QTextCursor cur(block);
    cur.beginEditBlock();
    cur.movePosition(QTextCursor::EndOfBlock);
    cur.insertBlock(dupFmt, block.charFormat());
    for (const auto &run : runs) cur.insertText(run.text, run.format);
    cur.endEditBlock();
    setTextCursor(cur);
    ensureBlockIds();
}

void RichEditor::deleteBlock(const QTextBlock &block) {
    if (!block.isValid()) return;
    if (QJsonDocument::fromJson(block.blockFormat().property(NativeBlockProperty).toByteArray()).object().contains("canonical_node")) {
        applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "delete"}, {"node_id", blockId(block)}}}); return;
    }
    auto *frame = QTextCursor(block).currentFrame();
    const auto kind = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("type").toString();
    if (kind == "toggle" && block == frame->firstCursorPosition().block()) {
        QTextCursor remove(document()); remove.beginEditBlock();
        remove.setPosition(frame->firstPosition() - 1);
        remove.setPosition(frame->lastPosition() + 1, QTextCursor::KeepAnchor); remove.removeSelectedText();
        remove.endEditBlock(); setTextCursor(remove); ensureBlockIds(); return;
    }
    QTextCursor cur(document());
    cur.beginEditBlock();
    if (block.textList()) {
        block.textList()->remove(block);
    }
    if (block == document()->begin() && block.next().isValid()) {
        const QTextBlock nextBlk = block.next();
        const QTextBlockFormat nextFmt = nextBlk.blockFormat();
        const QTextCharFormat nextCharFmt = nextBlk.charFormat();
        QTextList *nextList = nextBlk.textList();
        cur.setPosition(0);
        cur.setPosition(nextBlk.position(), QTextCursor::KeepAnchor);
        cur.removeSelectedText();
        cur.setBlockFormat(nextFmt);
        cur.setBlockCharFormat(nextCharFmt);
        if (nextList) {
            nextList->add(cur.block());
        } else if (cur.block().textList()) {
            cur.block().textList()->remove(cur.block());
        }
    } else if (block != document()->begin()) {
        cur.setPosition(block.position() - 1);
        cur.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
        cur.removeSelectedText();
    } else {
        cur.select(QTextCursor::BlockUnderCursor);
        cur.removeSelectedText();
    }
    cur.endEditBlock();
    ensureBlockIds();
}

void RichEditor::groupSelection() {
    if (isReadOnly()) return;
    auto cursor = textCursor();
    QTextBlock first, last;
    if (cursor.hasSelection()) {
        const int start = cursor.selectionStart();
        const int end = cursor.selectionEnd();
        first = document()->findBlock(start);
        // A selection ending exactly at a block start must not pull that
        // block in: only blocks with at least one selected character group.
        last = document()->findBlock(qMax(start, end - 1));
    }
    const bool single = !first.isValid() || !last.isValid() || !(first < last);
    bool canonicalSelection = single;
    if (!single) for (auto block = first; block.isValid(); block = block.next()) {
        if (QJsonDocument::fromJson(block.blockFormat().property(NativeBlockProperty).toByteArray()).object().contains("canonical_node")) canonicalSelection = true;
        if (block == last) break;
    }
    if (canonicalSelection) {
        const auto doc = canonicalDocument(); const auto nodes = doc.value("blocks").toArray();
        QSet<QString> selected;
        if (single) selected.insert(blockId(cursor.block()));
        else {
            for (auto block = first; block.isValid(); block = block.next()) {
                if (!canonicalLocation(nodes, blockId(block)).isEmpty()) selected.insert(blockId(block));
                if (block == last) break;
            }
            std::function<void(QTextFrame *)> frames = [&](QTextFrame *frame) {
                if (cursor.selectionStart() <= frame->firstPosition() - 1 && cursor.selectionEnd() >= frame->lastPosition() + 1) {
                    const auto node = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("canonical_node").toObject();
                    if (!node.isEmpty()) selected.insert(node.value("id").toString());
                }
                for (auto *child : frame->childFrames()) frames(child);
            }; frames(document()->rootFrame());
        }
        QJsonArray roots;
        std::function<void(const QJsonArray &)> gather = [&](const QJsonArray &values) {
            for (const auto &v : values) { const auto n = v.toObject(); if (selected.contains(n.value("id").toString())) roots.append(n.value("id")); else gather(n.value("children").toArray()); }
        }; gather(nodes);
        const auto group = QJsonObject{{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)}, {"type", "group"}, {"children", QJsonArray()}};
        if (roots.isEmpty()) { if (nodes.isEmpty()) applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "insert"}, {"node", group}, {"position", 0}}}); return; }
        const auto where = canonicalLocation(nodes, roots.first().toString()); const auto parent = where.value("parent_id").toString();
        if (canonicalLocation(nodes, parent).value("node").toObject().value("type") == "table_cell") return;
        for (const auto &id : roots) if (canonicalLocation(nodes, id.toString()).value("parent_id").toString() != parent) {
            QToolTip::showText(QCursor::pos(), "Selecciona bloques del mismo nivel para agrupar", this); return;
        }
        auto wrapper = group; QJsonArray children;
        for (const auto &id : roots) children.append(canonicalLocation(nodes, id.toString()).value("node"));
        wrapper["children"] = children;
        applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "delete"}, {"node_ids", roots}},
            QJsonObject{{"type", "insert"}, {"node", wrapper}, {"parent_id", parent}, {"position", where.value("position")}}});
        return;
    }
    const QString groupId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTextCursor transaction(document());
    transaction.beginEditBlock();
    for (QTextBlock block = first; block.isValid(); block = block.next()) {
        auto format = block.blockFormat();
        format.setProperty(GroupBlockProperty, groupId);
        format.setLeftMargin(16);
        format.setRightMargin(16);
        format.setTopMargin(block == first ? 8 : 0);
        format.setBottomMargin(block == last ? 8 : 0);
        // An explicit per-block background color wins over the group box.
        const QColor background = recomputedBlockBackground(format, dark_);
        if (background.isValid()) format.setBackground(background);
        else format.clearBackground();
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(format);
        if (block == last) break;
    }
    transaction.endEditBlock();
    ensureBlockIds();
}

void RichEditor::ungroupBlock(const QTextBlock &block) {
    if (isReadOnly() || !block.isValid()) return;
    for (auto *frame = QTextCursor(block).currentFrame(); frame && frame != document()->rootFrame(); frame = frame->parentFrame()) {
        const auto node = QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("canonical_node").toObject();
        if (node.value("type") == "group") { applyCanonicalCommands(QJsonArray{QJsonObject{{"type", "unwrap"}, {"node_id", node.value("id")}}}); return; }
    }
    const QString groupId = block.blockFormat().property(GroupBlockProperty).toString();
    if (groupId.isEmpty()) {
        // Legacy single-block group: ungrouping returns it to a paragraph.
        if (block.blockFormat().property(BlockStyleProperty).toString() == "group") {
            setTextCursor(QTextCursor(block));
            applyBlockStyle("paragraph");
        }
        return;
    }
    QTextCursor transaction(document());
    transaction.beginEditBlock();
    // Members may have been re-grouped with different neighbors; only the
    // contiguous run sharing this group id belongs to the box being removed.
    for (QTextBlock member = block; member.isValid(); member = member.previous()) {
        if (member.blockFormat().property(GroupBlockProperty).toString() != groupId) break;
        restoreUngroupedMargins(member, dark_);
    }
    for (QTextBlock member = block.next(); member.isValid(); member = member.next()) {
        if (member.blockFormat().property(GroupBlockProperty).toString() != groupId) break;
        restoreUngroupedMargins(member, dark_);
    }
    transaction.endEditBlock();
    ensureBlockIds();
}

bool RichEditor::blockInGroup(const QTextBlock &block) const {
    if (!block.isValid()) return false;
    for (auto *frame = QTextCursor(block).currentFrame(); frame && frame != document()->rootFrame(); frame = frame->parentFrame())
        if (QJsonDocument::fromJson(frame->frameFormat().property(NativeFrameProperty).toByteArray()).object().value("canonical_node").toObject().value("type") == "group") return true;
    return !block.blockFormat().property(GroupBlockProperty).toString().isEmpty()
        || block.blockFormat().property(BlockStyleProperty).toString() == "group";
}

void RichEditor::copyBlockReference(const QTextBlock &block) {
    if (!block.isValid() || documentObjectId_.isEmpty()) return;
    QUrl url("lumen://block/" + blockId(block)); QUrlQuery query;
    query.addQueryItem("object_id", documentObjectId_); url.setQuery(query);
    QGuiApplication::clipboard()->setText(url.toString(QUrl::FullyEncoded));
}

QTextBlock RichEditor::findBlockById(const QString &id) const {
    if (id.isEmpty()) return {};
    for (auto block = document()->begin(); block.isValid(); block = block.next())
        if (blockId(block) == id) return block;
    return {};
}

void RichEditor::insertBlockReferencePaste(const QString &url) {
    const QUrl parsed(url); const auto identity = parsed.path().mid(1);
    if (identity.isEmpty() || identity.contains('/')) return;
    const auto owner = QUrlQuery(parsed).queryItemValue("object_id");
    const auto insertion = textCursor(); const auto generation = contentGeneration_;
    if (blockResolveRequested) {
        QPointer<RichEditor> guard(this);
        blockResolveRequested(identity, owner, [guard, insertion, generation](QJsonObject hit) {
            if (!guard || hit.isEmpty() || guard->contentGeneration_ != generation || insertion.document() != guard->document()) return;
            guard->setTextCursor(insertion); guard->insertBlockReference(hit, insertion.block().text().isEmpty());
        }); return;
    }
    const auto target = findBlockById(identity);
    const auto sourceOwner = !owner.isEmpty() ? owner : (target.isValid() ? documentObjectId_ : QString());
    if (sourceOwner.isEmpty()) { textCursor().insertText(url, QTextCharFormat()); return; }
    insertBlockReference(QJsonObject{{"object_id", sourceOwner}, {"block", QJsonObject{{"id", identity},
        {"text", target.isValid() ? target.text() : url}}}}, insertion.block().text().isEmpty());
}

void RichEditor::setBlockStyleColors(const QTextBlock &block, const QString &textColor, const QString &backgroundColor) {
    if (isReadOnly() || !block.isValid()) return;
    const QColor defaultForeground = palette().color(QPalette::Text);
    const QColor chosenText = textColor.isEmpty() ? defaultForeground : QColor(textColor);
    QTextCursor cursor(block);
    cursor.beginEditBlock();
    auto format = block.blockFormat();
    if (textColor.isEmpty()) format.clearProperty(BlockTextColorProperty);
    else format.setProperty(BlockTextColorProperty, textColor);
    if (!backgroundColor.isEmpty()) {
        format.setProperty(BlockBackgroundProperty, backgroundColor);
        format.setBackground(QColor(backgroundColor));
    } else {
        format.clearProperty(BlockBackgroundProperty);
        const QColor restored = recomputedBlockBackground(format, dark_);
        if (restored.isValid()) format.setBackground(restored);
        else format.clearBackground();
    }
    cursor.setBlockFormat(format);
    // The color must reach the existing fragments, not only new typing.
    QTextCharFormat colorFormat;
    colorFormat.setForeground(chosenText);
    cursor.mergeBlockCharFormat(colorFormat);
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (!fragment.isValid() || fragment.length() == 0) continue;
        QTextCursor fragmentCursor(document());
        fragmentCursor.setPosition(fragment.position());
        fragmentCursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        fragmentCursor.mergeCharFormat(colorFormat);
    }
    cursor.endEditBlock();
    ensureBlockIds();
}

void RichEditor::applyIndentChange(const QTextBlock &block, int delta) {
    if (isReadOnly() || !block.isValid()) return;
    if (block.blockFormat().property(BlockStyleProperty).toString() == "divider") return;
    if (block.textList()) {
        QTextListFormat listFormat = block.textList()->format();
        const int newIndent = qBound(1, listFormat.indent() + delta, 6);
        if (newIndent == listFormat.indent()) return;
        QTextCursor cursor(block);
        cursor.beginEditBlock();
        listFormat.setIndent(newIndent);
        const QTextBlock previous = block.previous();
        const QTextBlock next = block.next();
        if (previous.isValid() && previous.textList() && previous.textList()->format() == listFormat) {
            previous.textList()->add(block);
        } else if (next.isValid() && next.textList() && next.textList()->format() == listFormat) {
            next.textList()->add(block);
        } else {
            cursor.createList(listFormat);
        }
        cursor.endEditBlock();
        return;
    }
    QTextBlockFormat format = block.blockFormat();
    const int newIndent = qBound(0, format.indent() + delta, 6);
    if (newIndent == format.indent()) return;
    QTextCursor cursor(block);
    cursor.beginEditBlock();
    format.setIndent(newIndent);
    cursor.setBlockFormat(format);
    cursor.endEditBlock();
}

void RichEditor::indentBlock(const QTextBlock &block) { applyIndentChange(block, 1); }
void RichEditor::outdentBlock(const QTextBlock &block) { applyIndentChange(block, -1); }

void RichEditor::moveBlockUp(const QTextBlock &block) {
    if (isReadOnly() || !block.isValid()) return;
    const QTextBlock previous = block.previous();
    if (!previous.isValid()) return;
    performBlockMove(block, previous, DropBefore);
}

void RichEditor::moveBlockDown(const QTextBlock &block) {
    if (isReadOnly() || !block.isValid()) return;
    const QTextBlock next = block.next();
    if (!next.isValid()) return;
    performBlockMove(block, next, DropAfter);
}

void RichEditor::normalizeGroupBoxes() {
    for (auto block = document()->begin(); block.isValid();) {
        const QString groupId = block.blockFormat().property(GroupBlockProperty).toString();
        if (groupId.isEmpty()) {
            block = block.next();
            continue;
        }
        QTextBlock last = block;
        while (last.next().isValid() &&
               last.next().blockFormat().property(GroupBlockProperty).toString() == groupId)
            last = last.next();
        for (QTextBlock member = block;; member = member.next()) {
            auto format = member.blockFormat();
            format.setLeftMargin(16);
            format.setRightMargin(16);
            format.setTopMargin(member == block ? 8 : 0);
            format.setBottomMargin(member == last ? 8 : 0);
            const QColor background = recomputedBlockBackground(format, dark_);
            if (background.isValid()) format.setBackground(background);
            else format.clearBackground();
            QTextCursor memberCursor(member);
            memberCursor.setBlockFormat(format);
            if (member == last) break;
        }
        block = last.next();
    }
}
void RichEditor::styleCodeBlock(QTextCursor cursor, const QString &language) {
    auto format = cursor.blockFormat();
    format.setProperty(BlockStyleProperty, "code");
    if (language.isEmpty()) format.clearProperty(CodeLanguageProperty);
    else format.setProperty(CodeLanguageProperty, language);
    format.clearProperty(EmbedBlockProperty);
    format.setHeadingLevel(0);
    format.setLeftMargin(0); format.setRightMargin(0);
    format.setTopMargin(6); format.setBottomMargin(12);
    format.setBackground(QColor(dark_ ? "#1b1b20" : "#f1f1f4"));
    cursor.setBlockFormat(format);
    if (highlighter_) highlighter_->rehighlightBlock(cursor.block());
}
QString RichEditor::codeLanguageAtCursor() const {
    const auto format = textCursor().block().blockFormat();
    return format.property(BlockStyleProperty).toString() == "code" ? format.property(CodeLanguageProperty).toString() : QString();
}
void RichEditor::setCodeLanguage(const QString &language) {
    auto block = textCursor().block();
    if (isReadOnly() || block.blockFormat().property(BlockStyleProperty).toString() != "code") return;
    QTextCursor cursor(block);
    styleCodeBlock(cursor, LumenSyntax::normalize(language));
    updateCodeBar(); updateImageBar();
}
void RichEditor::updateCodeBar() {
    const auto block = textCursor().block();
    const bool inCode = block.isValid() && isVisible() && block.blockFormat().property(BlockStyleProperty).toString() == "code";
    if (!inCode) { if (codeBar_) codeBar_->hide(); return; }
    if (!codeBar_) {
        auto *bar = new QFrame(viewport());
        codeBar_ = bar;
        bar->setObjectName("editorLayer");
        bar->setAccessibleName("Opciones del bloque de código");
        auto *layout = new QHBoxLayout(bar);
        layout->setContentsMargins(3, 2, 3, 2); layout->setSpacing(2);
        auto *language = new QToolButton(bar);
        language->setObjectName("codeLanguage");
        language->setToolTip("Lenguaje del bloque");
        language->setAccessibleName("Lenguaje del bloque de código");
        language->setPopupMode(QToolButton::InstantPopup);
        language->setFocusPolicy(Qt::NoFocus);
        language->setToolButtonStyle(Qt::ToolButtonTextOnly);
        language->setAutoRaise(true);
        auto *menu = new QMenu(language);
        for (const auto &entry : LumenSyntax::languages()) {
            auto *action = menu->addAction(entry.label);
            connect(action, &QAction::triggered, this, [this, id = entry.id] { setCodeLanguage(id); setFocus(); });
        }
        menu->addSeparator();
        connect(menu->addAction("Texto sin resaltado"), &QAction::triggered, this, [this] { setCodeLanguage({}); setFocus(); });
        language->setMenu(menu);
        auto *copy = tool("Copiar código", "copy", bar);
        copy->setObjectName("codeCopy");
        copy->setFocusPolicy(Qt::NoFocus);
        connect(copy, &QToolButton::clicked, this, [this, copy] {
            QString text = textCursor().block().text();
            text.replace(QChar(QChar::LineSeparator), '\n');
            QGuiApplication::clipboard()->setText(text);
            copy->setToolTip("Copiado");
            QToolTip::showText(QCursor::pos(), "Código copiado", copy);
        });
        layout->addWidget(language); layout->addWidget(copy);
        styleLayer(bar);
    }
    if (auto *language = codeBar_->findChild<QToolButton *>("codeLanguage")) {
        language->setText(LumenSyntax::label(block.blockFormat().property(CodeLanguageProperty).toString()));
        language->setEnabled(!isReadOnly());
    }
    codeBar_->adjustSize();
    const QRectF rect = document()->documentLayout()->blockBoundingRect(block);
    const int right = qMin(int(rect.right()) - horizontalScrollBar()->value(), viewport()->width());
    const int top = int(rect.top()) - verticalScrollBar()->value();
    const int bottom = int(rect.bottom()) - verticalScrollBar()->value();
    if (bottom < codeBar_->height() + 8 || top > viewport()->height()) { codeBar_->hide(); return; }
    codeBar_->move(qMax(4, right - codeBar_->width() - 8), qMax(top + 4, 4));
    codeBar_->show();
    codeBar_->raise();
}
bool RichEditor::handleCodeKey(QKeyEvent *event) {
    if (isReadOnly()) return false;
    auto cursor = textCursor();
    const auto block = cursor.block();
    const QString style = block.blockFormat().property(BlockStyleProperty).toString();
    const bool plainModifiers = !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
    const QChar lineBreak(QChar::LineSeparator);
    if (style != "code") {
        // Markdown fence: ```cpp + Enter (or Space after a language name) opens a code block.
        if (!plainModifiers || !(enter || event->key() == Qt::Key_Space) || cursor.hasSelection()) return false;
        static const QRegularExpression fence("^```([A-Za-z0-9+#_-]*)$");
        const auto match = fence.match(block.text());
        if (!match.hasMatch() || cursor.positionInBlock() != block.text().size()) return false;
        const QString raw = match.captured(1), language = LumenSyntax::normalize(raw);
        if (!enter && raw.isEmpty()) return false;
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        setTextCursor(cursor);
        applyBlockStyle(language.isEmpty() ? QString("code") : "code:" + language);
        return true;
    }
    const QString text = block.text();
    const int position = cursor.positionInBlock();
    // Ctrl+Enter / Alt+Enter salen del bloque sin tocar el código. Enter y
    // Shift+Enter siguen siendo texto dentro del bloque.
    if (enter && (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        cursor.beginEditBlock();
        QTextCursor after(block);
        after.movePosition(QTextCursor::EndOfBlock);
        const QTextBlock next = block.next();
        if (next.isValid() && next.blockFormat().property(BlockStyleProperty).toString() != "code") {
            after.setPosition(next.position());
        } else {
            after.insertBlock(QTextBlockFormat(), QTextCharFormat());
            after.setBlockCharFormat(QTextCharFormat());
            after.setCharFormat(QTextCharFormat());
        }
        cursor.endEditBlock();
        setTextCursor(after);
        ensureBlockIds();
        return true;
    }
    if (enter && !(event->modifiers() & Qt::ControlModifier)) {
        if (text.isEmpty()) { applyBlockStyle("paragraph"); return true; }
        const bool atEnd = !cursor.hasSelection() && position == text.size();
        if (atEnd && text.endsWith(lineBreak) && !(event->modifiers() & Qt::ShiftModifier)) {
            // Enter on an empty last line leaves the block.
            cursor.beginEditBlock();
            cursor.deletePreviousChar();
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
            cursor.endEditBlock();
            setTextCursor(cursor);
            ensureBlockIds();
            return true;
        }
        if (cursor.hasSelection()) cursor.removeSelectedText();
        const int at = cursor.positionInBlock();
        const int lineStart = at == 0 ? 0 : text.lastIndexOf(lineBreak, at - 1) + 1;
        const QString before = text.mid(lineStart, qMin(at, text.size()) - lineStart);
        QString indent;
        for (const QChar c : before) { if (c == ' ' || c == '\t') indent += c; else break; }
        const QString trimmed = before.trimmed();
        if (trimmed.endsWith('{') || trimmed.endsWith('(') || trimmed.endsWith('[')) indent += "    ";
        cursor.insertText(QString(lineBreak) + indent);
        setTextCursor(cursor);
        ensureCursorVisible();
        return true;
    }
    if (event->key() == Qt::Key_Backspace && plainModifiers && !cursor.hasSelection() && text.isEmpty()) {
        applyBlockStyle("paragraph");
        return true;
    }
    if (plainModifiers && (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab)) {
        const bool outdent = event->key() == Qt::Key_Backtab || (event->modifiers() & Qt::ShiftModifier);
        const int base = block.position();
        int from = cursor.selectionStart() - base, to = cursor.selectionEnd() - base;
        if (from < 0 || to > text.size()) return false;
        if (!cursor.hasSelection() && !outdent) { cursor.insertText("    "); setTextCursor(cursor); return true; }
        QList<int> starts;
        const int first = from == 0 ? 0 : text.lastIndexOf(lineBreak, from - 1) + 1;
        starts << first;
        for (int i = text.indexOf(lineBreak, first); i >= 0 && i < to; i = text.indexOf(lineBreak, i + 1)) starts << i + 1;
        cursor.beginEditBlock();
        for (auto it = starts.crbegin(); it != starts.crend(); ++it) {
            QTextCursor edit(document());
            edit.setPosition(base + *it);
            if (!outdent) { edit.insertText("    "); continue; }
            int remove = 0;
            while (remove < 4 && *it + remove < text.size() && text[*it + remove] == ' ') ++remove;
            if (!remove && *it < text.size() && text[*it] == '\t') remove = 1;
            edit.setPosition(base + *it + remove, QTextCursor::KeepAnchor);
            edit.removeSelectedText();
        }
        cursor.endEditBlock();
        return true;
    }
    if (event->matches(QKeySequence::Find)) return false;
    // Inside a code block, forward typing directly to standard text editing
    // so markdown shortcuts, todo triggers ("() "), pickers, or inline formulas do not interfere.
    QTextEdit::keyPressEvent(event);
    return true;
}

// ------------------------------------------------------------------- formulas
QTextImageFormat RichEditor::mathFormat(const QString &latex, bool display) const {
    const int pixel = QFontInfo(font()).pixelSize() > 0 ? QFontInfo(font()).pixelSize() : 16;
    const QColor color(dark_ ? "#e4e3e2" : "#303034");
    const auto result = LumenMath::render(latex, pixel, color, display, qMax<qreal>(2, devicePixelRatioF() * 2));
    const auto key = QCryptographicHash::hash(QString(latex + (display ? "|d|" : "|i|") + QString::number(pixel)).toUtf8(),
                                              QCryptographicHash::Sha1).toHex();
    const QUrl resource("lumen-math://m/" + QString::fromLatin1(key));
    document()->addResource(QTextDocument::ImageResource, resource, result.image);
    QTextImageFormat format;
    format.setName(resource.toString());
    format.setWidth(result.width);
    format.setHeight(result.height);
    format.setVerticalAlignment(display ? QTextCharFormat::AlignNormal : QTextCharFormat::AlignMiddle);
    format.setProperty(MathLatexProperty, latex);
    format.setProperty(MathDisplayProperty, display);
    format.setToolTip(latex.isEmpty() ? QString("Fórmula vacía") : "$" + latex + "$\nClic para editar");
    return format;
}
void RichEditor::insertMathAt(QTextCursor cursor, const QString &latex, bool display) {
    cursor.insertImage(mathFormat(latex, display));
}
void RichEditor::refreshMath() {
    for (auto block = document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto format = it.fragment().charFormat();
            if (format.isImageFormat() && format.hasProperty(MathLatexProperty))
                mathFormat(format.property(MathLatexProperty).toString(), format.property(MathDisplayProperty).toBool());
        }
}
void RichEditor::insertMath(const QString &latex, bool display) {
    if (isReadOnly()) return;
    if (display) { insertMathBlock(); return; }
    auto cursor = textCursor();
    QTextCharFormat base = cursor.charFormat();
    if (base.isImageFormat() || base.isAnchor()) base = QTextCharFormat();
    const int start = cursor.position();
    cursor.beginEditBlock();
    insertMathAt(cursor, latex, false);
    cursor.endEditBlock();
    setTextCursor(cursor);
    setCurrentCharFormat(base);
    ensureBlockIds();
    if (latex.trimmed().isEmpty()) {
        QTextCursor image(document());
        image.setPosition(start); image.setPosition(start + 1, QTextCursor::KeepAnchor);
        editMathAt(image);
    }
}
void RichEditor::insertMathBlock() {
    if (isReadOnly()) return;
    auto cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.currentList()) cursor.currentList()->remove(cursor.block());
    const bool reusable = cursor.block().text().isEmpty() && cursor.block().blockFormat().property(BlockStyleProperty).toString() != "code";
    if (!reusable) { cursor.movePosition(QTextCursor::EndOfBlock); cursor.insertBlock(QTextBlockFormat(), QTextCharFormat()); }
    QTextBlockFormat format;
    format.setProperty(BlockStyleProperty, "math");
    format.setAlignment(Qt::AlignCenter);
    format.setTopMargin(8); format.setBottomMargin(12);
    cursor.setBlockFormat(format);
    const int start = cursor.position();
    cursor.insertImage(mathFormat({}, true));
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());   // keep writing below the formula
    cursor.endEditBlock();
    setTextCursor(cursor);
    ensureBlockIds();
    QTextCursor image(document());
    image.setPosition(start); image.setPosition(start + 1, QTextCursor::KeepAnchor);
    editMathAt(image);
}
QTextCursor RichEditor::mathCursorAt(const QPoint &point) const {
    const QPointF documentPoint(point.x() + horizontalScrollBar()->value(), point.y() + verticalScrollBar()->value());
    const int hit = document()->documentLayout()->hitTest(documentPoint, Qt::ExactHit);
    if (hit < 0) return {};
    for (const int candidate : {hit, hit - 1}) {
        if (candidate < 0 || candidate >= document()->characterCount() - 1) continue;
        if (document()->characterAt(candidate) != QChar::ObjectReplacementCharacter) continue;
        QTextCursor probe(document());
        probe.setPosition(candidate + 1);
        if (!probe.charFormat().hasProperty(MathLatexProperty)) continue;
        QTextCursor result(document());
        result.setPosition(candidate);
        result.setPosition(candidate + 1, QTextCursor::KeepAnchor);
        return result;
    }
    return {};
}
void RichEditor::editMathAt(QTextCursor target) {
    if (isReadOnly() || mathPopup_ || !target.hasSelection()) return;
    const int start = target.selectionStart();
    QTextCursor probe(document());
    probe.setPosition(start + 1);
    const auto current = probe.charFormat();
    if (!current.hasProperty(MathLatexProperty)) return;
    const bool display = current.property(MathDisplayProperty).toBool();
    auto *popup = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
    mathPopup_ = popup;
    popup->setObjectName("editorLayer");
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setAccessibleName("Editar fórmula LaTeX");
    popup->setFixedWidth(display ? 460 : 400);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(10, 10, 10, 8); layout->setSpacing(7);
    auto *input = new QLineEdit(current.property(MathLatexProperty).toString(), popup);
    input->setObjectName("mathInput");
    input->setPlaceholderText("LaTeX, por ejemplo \\frac{a}{b} + \\sqrt{x^2}");
    input->setAccessibleName("Código LaTeX de la fórmula");
    input->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto *preview = new QLabel(popup);
    preview->setObjectName("mathPreview");
    preview->setAlignment(Qt::AlignCenter);
    preview->setMinimumHeight(54);
    auto *hint = new QLabel("Enter aplica · Esc cancela · vacío elimina la fórmula", popup);
    hint->setObjectName("inlineMenuHint");
    hint->setWordWrap(true);
    layout->addWidget(input); layout->addWidget(preview); layout->addWidget(hint);
    const int pixel = QFontInfo(font()).pixelSize() > 0 ? QFontInfo(font()).pixelSize() : 16;
    const QColor color(dark_ ? "#e4e3e2" : "#303034");
    auto refresh = [=, this] {
        const auto result = LumenMath::render(input->text(), pixel, color, display, qMax<qreal>(2, devicePixelRatioF() * 2));
        preview->setPixmap(QPixmap::fromImage(result.image));
        hint->setText(result.ok ? QString("Enter aplica · Esc cancela · vacío elimina la fórmula") : "Revisa la fórmula: " + result.error);
    };
    connect(input, &QLineEdit::textChanged, popup, refresh);
    refresh();
    connect(input, &QLineEdit::returnPressed, popup, [this, popup, input, start, display] {
        QTextCursor image(document());
        image.setPosition(start); image.setPosition(start + 1, QTextCursor::KeepAnchor);
        if (document()->characterAt(start) == QChar::ObjectReplacementCharacter) {
            const QString latex = input->text().trimmed();
            image.beginEditBlock();
            image.removeSelectedText();
            if (!latex.isEmpty()) image.insertImage(mathFormat(latex, display));
            else if (display) { QTextCursor block(document()->findBlock(start)); block.setBlockFormat(QTextBlockFormat()); }
            image.endEditBlock();
            QTextCursor after(document());
            after.setPosition(qMin(start + (latex.isEmpty() ? 0 : 1), document()->characterCount() - 1));
            setTextCursor(after);
        }
        popup->close();
        setFocus();
    });
    // An abandoned empty placeholder must not leave an empty formula behind.
    connect(popup, &QObject::destroyed, this, [this, start, display] {
        if (document()->characterAt(start) != QChar::ObjectReplacementCharacter) return;
        QTextCursor probe(document());
        probe.setPosition(start + 1);
        const auto format = probe.charFormat();
        if (!format.hasProperty(MathLatexProperty) || !format.property(MathLatexProperty).toString().isEmpty()) return;
        QTextCursor image(document());
        image.setPosition(start); image.setPosition(start + 1, QTextCursor::KeepAnchor);
        image.removeSelectedText();
        if (display) { QTextCursor block(document()->findBlock(start)); block.setBlockFormat(QTextBlockFormat()); }
    });
    styleLayer(popup);
    popup->adjustSize();
    positionPopup(popup, target);
    popup->show();
    input->setFocus();
    input->selectAll();
}
bool RichEditor::handleMarkupShortcut(QKeyEvent *event) {
    if (isReadOnly()) return false;
    auto cursor = textCursor();
    if (cursor.block().blockFormat().property(BlockStyleProperty).toString() == "code") return false;
    if (event->key() == Qt::Key_M && event->modifiers() == Qt::ControlModifier) {
        QString selected = cursor.selectedText();
        selected.replace(QChar(QChar::LineSeparator), ' ').replace(QChar(QChar::ParagraphSeparator), ' ');
        if (cursor.hasSelection()) { cursor.removeSelectedText(); setTextCursor(cursor); }
        insertMath(selected.trimmed(), false);
        return true;
    }
    if (event->text() != "$" || cursor.hasSelection()) return false;
    const QString prefix = cursor.block().text().left(cursor.positionInBlock());
    if (prefix == "$") {                       // "$$" at the start of a paragraph opens a formula block
        cursor.deletePreviousChar();
        setTextCursor(cursor);
        insertMathBlock();
        return true;
    }
    const int open = prefix.lastIndexOf('$');
    if (open < 0 || (open > 0 && prefix[open - 1] == '\\')) return false;
    const QString inner = prefix.mid(open + 1);
    if (inner.isEmpty() || inner.front().isSpace() || inner.back().isSpace() || inner.contains(QChar::ObjectReplacementCharacter)) return false;
    cursor.beginEditBlock();
    cursor.setPosition(cursor.position() - inner.size() - 1);
    cursor.setPosition(cursor.position() + inner.size() + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor);
    insertMath(inner, false);
    return true;
}

QString RichEditor::exportText() const {
    QStringList lines;
    const QChar lineBreak(QChar::LineSeparator);
    for (auto block = document()->begin(); block.isValid(); block = block.next()) {
        const auto format = block.blockFormat();
        if (format.property(BlockStyleProperty).toString() == "divider") {
            lines << "---";
            continue;
        }
        if (format.property(BlockStyleProperty).toString() == "code") {
            QString body = block.text();
            body.replace(lineBreak, '\n');
            lines << "```" + format.property(CodeLanguageProperty).toString() + "\n" + body + "\n```";
            continue;
        }
        QString text;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            const auto f = fragment.charFormat();
            if (f.isImageFormat() && f.hasProperty(MathLatexProperty)) {
                const QString mark = f.property(MathDisplayProperty).toBool() ? "$$" : "$";
                text += mark + f.property(MathLatexProperty).toString() + mark;
            } else text += fragment.text();
        }
        text.replace(lineBreak, '\n');
        lines << text;
    }
    return lines.join('\n');
}
