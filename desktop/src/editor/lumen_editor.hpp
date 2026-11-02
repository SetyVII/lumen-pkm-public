#pragma once

#include <QtWidgets>
#include <functional>
#include "lumen_math.hpp"
#include "lumen_syntax.hpp"

class RichEditor;

// Notes never resolve file:, http:, or other external resources while rendering.
class PrivateDocument : public QTextDocument {
  public:
    explicit PrivateDocument(RichEditor *editor = nullptr);
  protected:
    QVariant loadResource(int type, const QUrl &name) override;
  private:
    RichEditor *editor_ = nullptr;
};

class RichEditor : public QTextEdit {
  public:
    using ReferenceCompletion = std::function<void(QJsonObject)>;
    std::function<void()> linkRequested, slashRequested;
    std::function<void(QString)> openReference;
    std::function<void(QString, std::function<void(QJsonArray)>)> blockSearchRequested;
    std::function<void(QString, QString, ReferenceCompletion)> blockResolveRequested;
    std::function<void(QString, QString, ReferenceCompletion)> referenceCreateRequested;
    std::function<void(QString, ReferenceCompletion)> webEmbedRequested;
    std::function<void(QString)> referenceInserted;
    std::function<void(QImage)> imagePasted;
    std::function<void(QString, QString, ReferenceCompletion)> convertBlockRequested;
    using ResourceProvider = std::function<QVariant(int type, const QUrl &name)>;
    ResourceProvider resourceProvider;
    QVariant resolveDefaultResource(int type, const QUrl &name) const;
    explicit RichEditor(QWidget *parent = nullptr);
    ~RichEditor() override;
    void setPlainText(const QString &text);
    void setHtml(const QString &text);
    void clear();
    void setRecentIds(const QStringList &ids);
    void setDocumentObjectId(const QString &identity) { documentObjectId_ = identity; }
    QString documentObjectId() const { return documentObjectId_; }
    void setReferenceData(const QJsonArray &objects, const QJsonArray &types);
    void setAppearance(bool dark, bool reducedMotion = false);
    void showReferencePicker(bool asBlock = false);
    void showBlockReferencePicker();
    void insertBlockReference(const QJsonObject &hit, bool asBlock = false);
    void showBlockMenu();
    void showBlockActionMenu(const QTextBlock &block, const QPoint &globalPos);
    void convertBlockToObject(const QTextBlock &block, const QString &typeId);
    void duplicateBlock(const QTextBlock &block);
    void deleteBlock(const QTextBlock &block);
    void groupSelection();
    // Removes the group box around `block` (or unwraps a legacy group block).
    void ungroupBlock(const QTextBlock &block);
    bool blockInGroup(const QTextBlock &block) const;
    // Copies a compound lumen://block/<id>?object_id=<owner> reference.
    void copyBlockReference(const QTextBlock &block);
    // Per-block style colors; an empty string clears that color.
    void setBlockStyleColors(const QTextBlock &block, const QString &textColor, const QString &backgroundColor);
    void moveBlockUp(const QTextBlock &block);
    void moveBlockDown(const QTextBlock &block);
    void indentBlock(const QTextBlock &block);
    void outdentBlock(const QTextBlock &block);
    void insertTemporalReference(const QJsonObject &attributes);
    void showTemporalPicker(bool withTime = false, const QTextCursor &existing = QTextCursor());
    void insertObjectReference(const QJsonObject &object);
    void insertObjectReferenceBlock(const QJsonObject &object);
    // Etiquetas mencionadas en el documento: id del objeto y bloque que
    // contiene la primera mención. Sin duplicados, en orden de aparición.
    struct TagMention {
        QString id;
        QString blockId;
    };
    QList<TagMention> tagMentions() const;
    void insertImageBlock(const QString &objectId, const QPixmap &pixmap, const QString &title = {});
    bool focusBlock(const QString &id);
    void showFindBar(bool showReplace = false);
    void closeFindBar();
    bool findBarVisible() const;
    // Restores the session (last query/options) when a new object is opened.
    void setFindSession(const QString &query, bool matchCase, bool wholeWord, bool replaceVisible);
    QString findQueryText() const { return findQuery_; }
    bool findMatchCase() const { return findMatchCase_; }
    bool findWholeWord() const { return findWholeWord_; }
    bool findReplaceVisible() const { return findReplaceVisible_; }
    int findMatchCount() const { return int(findMatches_.size()); }
    int findCurrentMatch() const { return findIndex_ >= 0 ? findIndex_ + 1 : 0; }
    int findBarHeight() const { return findBar_ ? findBar_->height() : 0; }
    bool findNextMatch();
    bool findPreviousMatch();
    // Replaces the selected match and advances; false when the match is a
    // read-only reference title (the notice explains why).
    bool replaceFindCurrent(const QString &replacement);
    // Replaces every editable match in one undo step; returns the count.
    int replaceFindAll(const QString &replacement);
    void setFindMatchCase(bool enabled);
    void setFindWholeWord(bool enabled);
    void setFindReplaceVisible(bool visible);
    // Scrolls the embedding pane to a match (viewport coordinates). MainWindow
    // maps it to the document scroll area; standalone editors ignore it.
    std::function<void(const QRect &)> revealFindMatch;
    // Persists the find session across editors (MainWindow).
    std::function<void()> findStateChanged;
    QJsonArray documentReferenceIds() const;
    bool selectCanonicalBlock(const QString &id);
    bool pasteCanonicalDocument(const QJsonObject &document, QString *error = nullptr);
    bool applyCanonicalCommands(const QJsonArray &commands, QString *error = nullptr);
    QJsonObject canonicalDocument() const;
    void setCanonicalDocument(const QJsonObject &document);
    QJsonArray structuredBlocks() const;
    void setStructuredBlocks(const QJsonArray &blocks);
    void insertDivider();
    void insertTableBlock(int rows = 3, int cols = 3);
    void insertColumnsBlock();
    void showIconPicker();
    void showLinkEditor(bool embed = false);
    void applyBlockStyle(const QString &style);   // also "code:<lang>" and "math"
    // Plain-text export used for the object's `content`: code blocks become
    // fenced blocks and formulas become $latex$ / $$latex$$.
    QString exportText() const;
    void insertMath(const QString &latex, bool display = false);
    void editMathAt(QTextCursor imageCursor);
    QString codeLanguageAtCursor() const;
    void setCodeLanguage(const QString &language);
    // Id de bloque (BlockIdProperty); vacío si no lo tiene.
    static QString blockId(const QTextBlock &block);
  protected:
    QMimeData *createMimeDataFromSelection() const override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    enum DropMode { DropNone, DropBefore, DropAfter, DropSideColumn };
    // Traslada un bloque conservando su formato completo (estilo, id, imagen del
    // divisor, listas): reinsertar solo el texto plano destruía el divisor.
    void performBlockMove(const QTextBlock &source, const QTextBlock &target, DropMode mode);
    // Coloca el tirador centrado en la línea visible del bloque activo.
    void positionBlockHandle();
    // Bloque bajo un punto del viewport, considerando la fila completa
    // (contenido + márgenes) para que el hover sobre la fila del divisor
    // mantenga el tirador en esa línea.
    QTextBlock blockAtViewportPoint(const QPoint &point) const;
  private:
    QJsonArray objects_, types_;
    QString documentObjectId_;
    bool pasteStructuredMime(const QMimeData *source);
    bool pasteNormalizedHtml(const QString &html);
    void pastePlainText(QString text);
    bool loadedCanonicalEmpty_ = false;
    QPointer<QTextTable> resizingTable_;
    int resizingColumn_ = -1;
    qreal resizingStartX_ = 0, resizingStartWidth_ = 0;
    QTextCursor resizeTransaction_;
    int tableResizeColumnAt(const QPoint &point, QTextTable **table = nullptr) const;
    QStringList recentIds_;
    bool referenceBlock_ = false;
    bool referenceDataSet_ = false, dark_ = false, reducedMotion_ = false, normalizing_ = false;
    QPointer<QFrame> referencePopup_, blockPopup_, selectionToolbar_, linkPopup_, mathPopup_, codeBar_, imageBar_;
    QPointer<QToolButton> blockHandle_;
    QPointer<QWidget> imageLeftHandle_, imageRightHandle_;
    QTextBlock activeHandleBlock_;
    QTextBlock draggedBlock_;
    QTextBlock dropTargetBlock_;
    DropMode dropMode_ = DropNone;
    QRect dropIndicatorRect_;
    QPoint dragStartPos_;
    bool isDraggingHandle_ = false;
    LumenSyntaxHighlighter *highlighter_ = nullptr;
    QPointer<QListWidget> referenceResults_;
    QPointer<QListWidget> blockResults_;
    QPointer<QLabel> referenceHeading_, referenceHint_;
    QPointer<QToolButton> referenceBack_;
    QPointer<QCalendarWidget> referenceCalendar_;
    QTextCursor referenceCursor_, selectionCursor_, queryCursor_;
    QString referenceTypeId_, inlineQuery_;
    bool updatingPicker_ = false;
    quint64 contentGeneration_ = 0;
    QTimer idTimer_, toolbarTimer_;
    void ensureBlockIds();
    void refreshToggleVisibility();
    bool toggleLayoutPending_ = false, refreshingToggles_ = false;
    bool referenceHasTitle(const QString &text, const QString &type, const QString &id) const;
    void rebuildReferences();
    void rebuildBlockMenu();
    void updateInlineQuery();
    void beginInlineQuery(const QString &trigger);
    QTextCursor consumeInlineQuery(bool keepQuery = false);
    void closeInlineMenu(bool cancel = false);
    void selectBlock();
    bool handleInlineKey(QKeyEvent *event);
    QFrame *makeInlinePanel(const QString &accessibleName, const QString &kind, bool heading);
    void insertReferenceIcon(QTextCursor &cursor, const QTextCharFormat &format, const QString &type);
    void selectReference();
    void insertReferenceAt(QTextCursor cursor, const QJsonObject &object, const QString &linkText = {});
    void insertReferenceBlockAt(QTextCursor cursor, const QJsonObject &object, const QString &linkText = {});
    void updateSelectionToolbar();
    void positionPopup(QWidget *popup, const QTextCursor &cursor, bool above = false);
    void styleLayer(QWidget *layer);
    QIcon typeIcon(const QString &id, int size = 30) const;
    QString typeLabel(const QString &id) const;
    QTextImageFormat mathFormat(const QString &latex, bool display) const;
    QTextCursor mathCursorAt(const QPoint &viewportPoint) const;
    void insertMathAt(QTextCursor cursor, const QString &latex, bool display);
    void insertMathBlock();
    void refreshMath();
    void updateCodeBar();
    void updateImageBar();
    bool handleReferenceDeletion(QKeyEvent *event);
    bool handleReferenceNavigation(QKeyEvent *event);
    bool handleMarkdownPrefix(QKeyEvent *event);
    bool handleInlineMarkdown(QKeyEvent *event);
    bool handleCodeKey(QKeyEvent *event);
    bool handleMarkupShortcut(QKeyEvent *event);
    void styleCodeBlock(QTextCursor cursor, const QString &language);
    qreal baseFontPointSize() const;
    qreal blockStylePointSize(const QString &style) const;
    QTextCharFormat characterFormatForBlockStyle(const QString &style) const;
    // Aplica el formato de carácter del estilo al texto ya existente, no solo
    // al formato de inserción. Sin esto, convertir un párrafo en encabezado
    // cambiaba el nivel del bloque pero dejaba el texto con su tamaño y peso.
    void applyBlockCharacterStyle(QTextCursor cursor, const QString &oldStyle, const QString &style);
    // Shared indent/outdent core used by Tab/Shift+Tab and the handle menu.
    void applyIndentChange(const QTextBlock &block, int delta);
    // Reapplies the box background and margins of every contiguous group run;
    // called after moving a member so first/last members frame the box again.
    void normalizeGroupBoxes();
    // Resolves a "lumen://block/<id>" paste into a reference chip (embedded in
    // an empty block, inline elsewhere). Snapshot rendering; live
    // transclusion is a follow-up.
    void insertBlockReferencePaste(const QString &url);
    QTextBlock findBlockById(const QString &id) const;
    void refreshDividerRule();
    qreal dividerRuleWidth_ = 0;
    struct FindMatch {
        int start = 0;
        int end = 0;
        bool readOnly = false;
        bool table = false;
        bool image = false;
    };
    void ensureFindBar();
    void positionFindBar();
    QWidget *findBarHost();
    void refreshFindMatches(bool selectFirst);
    void selectFindMatch(int index);
    void updateFindUi();
    void rebuildFindSelections();
    void applyExtraSelections();
    void updateFindBarIcons();
    void classifyFindMatch(FindMatch &match) const;
    QPointer<QFrame> findBar_;
    QPointer<QWidget> findBarHost_;
    QPointer<QLineEdit> findQueryEdit_, findReplaceEdit_;
    QPointer<QLabel> findCounter_, findNoticeLabel_;
    QPointer<QWidget> findReplaceRow_;
    QPointer<QToolButton> findPrevButton_, findNextButton_, findOptionsButton_, findReplaceToggle_, findCloseButton_;
    QPointer<QAction> findMatchCaseAction_, findWholeWordAction_;
    QList<FindMatch> findMatches_;
    QList<QTextEdit::ExtraSelection> findSelections_, flashSelections_;
    QTimer flashTimer_;
    int findIndex_ = -1;
    QString findQuery_, findNoticeText_;
    bool findMatchCase_ = false, findWholeWord_ = false, findReplaceVisible_ = false;
    bool findUpdating_ = false;
    QTimer findRefreshTimer_;
};
