#pragma once

#include "lumen_api.hpp"
#include "lumen_widgets.hpp"

#include "lumen_calendar.hpp"
#include "lumen_editor.hpp"
#include "lumen_graph.hpp"
#include "lumen_icons.hpp"
#include "lumen_image_dialog.hpp"
#include "lumen_media.hpp"
#include "lumen_properties.hpp"
#include "lumen_tasks.hpp"
#include "lumen_views.hpp"

#include <QPdfDocument>
#include <QPdfView>
#include <QSvgRenderer>
#include <QtNetwork>
#include <QtWidgets>
#include <cmath>
#include <functional>

class MainWindow : public QMainWindow {
    Api api;
    QJsonArray objects, types;
    QJsonObject current;
    QString route = "journal", typeFilter;
    QDate day = QDate::currentDate();
    quint64 navigationGeneration = 0;
    bool navigationPending = false;
    bool loading = false, dirty = false, saving = false, dark = true, compact = true, gallery = true,
         reduceMotion = false;
    QString themePreset = "Grafito";
    int fontSize = 16;
    QString findQuery_;
    bool findMatchCase_ = false, findWholeWord_ = false, findReplaceVisible_ = false;
    QTimer saveTimer, syncTimer;
    bool polling = false;
    bool derivedRefreshPending = false;
    int pollTick = 0;
    QJsonArray linked;
    std::function<void()> pendingNavigation;
    QWidget *sidebar = nullptr, *page = nullptr, *inspector = nullptr;
    QVBoxLayout *nav = nullptr, *body = nullptr, *inspect = nullptr;
    QHBoxLayout *shell = nullptr;
    QLabel *crumb = nullptr, *saveLabel = nullptr, *connection = nullptr, *notice = nullptr;
    DocumentTitle *title = nullptr;
    RichEditor *editor = nullptr;
    QWidget *linkedSection = nullptr;
    QPointer<QWidget> documentPane;
    QPointer<QScrollArea> documentScroll;
    QPointer<LumenViews> activeViews;
    QPointer<LumenMiniCalendar> miniCalendar;
    QPointer<LumenCalendarOverview> calendarOverview;
    QPointer<LumenGraph> graphCanvas;
    QPointer<LumenTasks> activeTasks;
    QTabBar *tabBar = nullptr;
    QPushButton *backButton = nullptr, *forwardButton = nullptr;
    struct NavState {
        QString route = "journal", id, type, mode = "day";
        // Dashboard section of a type page ("all" | built-in keys | "query:<id>").
        QString section = "all";
        QDate date = QDate::currentDate();
        QJsonObject view;
        int scroll = 0;
    };
    QVector<QVector<NavState>> histories{{NavState{}}};
    QVector<int> historyIndices{0};
    int activeTab = 0;
    bool replaying = false;
    QString calendarMode = "day";
    QHash<QString, QJsonObject> collectionSpecs;
    QHash<QString, int> scrollPositions;
    QSet<QString> knownObjectIds;
    QSet<QString> hiddenTypeCollections;
    QStringList recentObjectIds;
    QString typeSection = "all";
    QHash<QString, QStringList> noBacklinkIds;
    QSet<QString> noBacklinkFetched, noBacklinkPending;
    QHash<QString, QPixmap> imageMemoryCache_;
    QHash<QString, QList<QPointer<RichEditor>>> assetImageWaiters_;
    QJsonArray templates;
    QSet<QString> templatesFetched, templatesPending;

  public:
    QString failure;
    bool ready = false;
    bool viewReady() const;
    QString currentRoute() const { return route; }
    MainWindow();
    void shortcut(const QString &key, std::function<void()> fn);
    QJsonObject typeFor(const QString &id) const;
    void error(const QString &s);
    void refresh();
    void connectDialog();
    void rebuildSidebar();
    void rememberRecent(const QString &id);
    // Recently-opened persistence (D4): the session list stays in RAM; a
    // capped snapshot lives in QSettings so the "Recent" dashboard section
    // survives restarts. Object ids only, never note content.
    QString recentSettingsKey() const;
    void loadRecentIds();
    void saveRecentIds() const;
    void rememberObject(const QJsonObject &object);
    void refreshDerivedViews();
    QJsonObject objectFor(const QString &id) const;
    QJsonArray allLinks(const QJsonObject &object) const;
    NavState snapshot() const;
    void rememberPosition();
    void updateHistoryButtons();
    void recordNavigation();
    void restoreState(const NavState &s);
    void moveHistory(int delta);
    void switchTab(int index);
    void newTab(QString objectId = {});
    void closeTab(int index);
    void setInfoVisible(bool visible);
    void pollWorkspace();
    void resetBody();
    bool canLeave(std::function<void()> next = {});
    quint64 beginNavigation();
    bool finishNavigation(quint64 generation);
    void showJournal();
    void navigateJournal(QDate target);
    void openObject(const QString &id);
    void wireEditor(RichEditor *edit, const QString &origin, bool preview = false);
    void ensureImageLoaded(RichEditor *edit, const QString &objectId);
    void ensureAssetImageLoaded(RichEditor *edit, const QString &assetId);
    void loadEditor(RichEditor *edit, const QJsonObject &object);
    QWidget *calendarToolbar(const QString &selected);
    void renderEditor(bool journal);
    QJsonArray attachedAssetIds(const QJsonObject &obj) const;
    QJsonArray documentReferences() const;
    void reconcileReferences();
    void unlinkObject(const QString &id);
    void renderLinkedObjects();
    // Fila de etiquetas bajo el título: asignadas (propiedad tags), mencionadas
    // en el contenido (saltan al bloque con focusBlock) y acceso al selector.
    // `object`/`apply` la conectan con current (página) o PreviewSession (preview).
    void refreshTagChips(QWidget *row, RichEditor *edit,
                         const std::function<QJsonObject()> &object,
                         const std::function<void(const QJsonObject &)> &apply,
                         const std::function<void()> &dirty);
    void renderJournalTasks(QVBoxLayout *layout, QDate targetDay);

    void renderObjectAssociatedTasks(QVBoxLayout *layout, const QString &objectId);

    void renderTaskItemRow(QVBoxLayout *layout, const QJsonObject &t);
    QString workspaceSyncState() const;
    void updateStatusLabel();
    QString saveState(const QJsonObject &o) const;
    void markDirty();
    void saveCurrent(std::function<void()> after = {});
    void renderInspector(bool journal);
    void configureViews(LumenViews *views);
    void saveQuery(QString name, QJsonObject spec);
    struct PreviewSession {
        QJsonObject object;
        QPointer<RichEditor> edit;
        QPointer<DocumentTitle> heading;
        QPointer<QLabel> status;
        QPointer<QTimer> timer;
        bool dirty = false, saving = false, closing = false;
        std::function<void()> afterClose;
    };
    QString referenceBlock(const QJsonObject &source, const QString &target) const;
    QString referenceSnippet(const QJsonObject &source, const QString &target, const QString &targetTitle = {}) const;
    void savePreview(QPointer<GuardedDialog> dialog, std::shared_ptr<PreviewSession> state);
    void setCoverAsset(const QString &assetId);
    void removeCoverAsset();
    void chooseCoverFile(std::function<void(QString)> onDone = {});
    void pasteCoverFromClipboard(std::function<void(QString)> onDone = {});
    void openAddImageModal(std::function<void(QString)> applyNewCover, QWidget *parentWin = nullptr);
    void showCoverMenu(QWidget *anchor, std::shared_ptr<PreviewSession> previewSession = nullptr,                        QPointer<QLabel> previewCover = nullptr, QPointer<QPushButton> previewBtn = nullptr);
    void showObjectOptionsMenu(QWidget *anchor, std::shared_ptr<PreviewSession> previewSession = nullptr,                                QPointer<QLabel> previewCover = nullptr, QPointer<QPushButton> previewBtn = nullptr,                                QWidget *canvasToResize = nullptr);
    void openPreview(QString id, QString blockId = {});
    void propertyDialog(QString typeId);
    struct CollectionImport {
        QStringList paths;
        QString type;
        int index = 0;
        int imported = 0;
        QStringList errors;
    };
    void importCollectionFiles(QString type);
    void importCollectionNext(std::shared_ptr<CollectionImport> batch);
    void importMedia(QString type);
    void format(QString what);
    void slashMenu();
    void linkObject();
    void insertReference(const QJsonObject &object);
    void createdObject(const QJsonObject &object, const QString &originId, const QString &originRoute,                        quint64 generation);
    void newObject(QString defaultType = "");
    void newType();
    void commandPalette();
    void showCollection(QString tid, QJsonObject spec = {}, QString queryId = {}, QString section = "all");
    // Curated collections (objects of the default type `collection`): chips on
    // the type page, a dedicated collection page, and the item mutation flows.
    QJsonArray collectionsForType(const QString &typeId) const;
    static QStringList collectionItemIds(const QJsonObject &collection);
    static QString collectionCountText(const QJsonObject &collection);
    void rememberActiveViewSpec();
    QWidget *buildCollectionChips(const QString &typeId);
    void fillCollectionChips(QWidget *bar, QHBoxLayout *row, const QString &typeId);
    void updateCollectionChips(const QString &typeId);
    void showCollectionPage(const QString &collectionId);
    void applyCollectionScope(LumenViews *views, const QJsonObject &collection);
    void collectionUpdated(const QJsonObject &collection);
    void newCollectionDialog(const QString &typeId, const QString &objectId = {});
    void createCollection(const QString &typeId, const QString &name, const QString &objectId = {});
    void addToCollection(const QString &collectionId, const QStringList &objectIds);
    void removeFromCollection(const QString &collectionId, const QString &objectId);
    void moveCollectionItem(const QString &collectionId, const QString &objectId, int delta);
    void reorderCollection(const QString &collectionId, const QStringList &itemIds);
    void deleteCollection(const QString &collectionId);
    void pickObjectsForCollection(const QString &collectionId);
    QMenu *buildObjectMenu(const QString &objectId, const QString &collectionId);
    void showObjectMenu(const QString &objectId, const QPoint &globalPos);
    void fillObjectCollections(QWidget *panel, const QJsonArray &collections, const QString &objectId);
    void renderObjectCollections(QWidget *panel, const QString &objectId);
    static QStringList dashboardSectionKeys();
    static QString dashboardSectionLabel(const QString &section);
    QStringList visibleTypeSections(const QString &typeId) const;
    void setVisibleTypeSections(const QString &typeId, const QStringList &sections);
    static bool objectHasTags(const QJsonObject &object);
    QStringList recentTypeIds(const QString &typeId) const;
    QStringList untaggedTypeIds(const QString &typeId) const;
    QJsonArray queriesForType(const QString &typeId) const;
    void requestNoBacklinkIds(const QString &typeId);
    int dashboardSectionCount(const QString &typeId, const QString &section) const;
    QWidget *buildTypeDashboard(const QString &typeId);
    void fillTypeDashboard(QWidget *bar, QHBoxLayout *row, const QString &typeId);
    void updateTypeDashboard(const QString &typeId);
    void refreshTypeDashboard(const QString &typeId);
    void applyTypeSection(LumenViews *views, const QString &typeId, const QString &section,
                          QJsonObject baseSpec = {});
    void showTypeSection(const QString &typeId, const QString &section);
    void toggleTypeSection(const QString &typeId, const QString &section, bool visible);
    static bool objectIsTemplate(const QJsonObject &object);
    QJsonObject templateById(const QString &id) const;
    QJsonArray templatesForType(const QString &typeId) const;
    QJsonArray objectsWithTemplates() const;
    void requestTemplates(const QString &typeId);
    void refreshTemplatePickers();
    void fillTemplateCombo(QComboBox *combo, const QString &typeId, const QString &purpose,
                           const QString &selectedId = {});
    void setObjectTemplateFlag(const QString &objectId, bool flagged);
    void createFromTemplate(const QString &templateId, const QString &title, const QString &originId,
                            const QString &originRoute, quint64 generation);
    bool journalTypeHasDefaultTemplate() const;
    bool templatesSectionConfigured(const QString &typeId) const;
    bool templatesSectionVisible(const QString &typeId) const;
    QMenu *buildTemplateMenu(const QString &objectId);
    void showTemplateMenu(const QString &objectId, const QPoint &globalPos);
    void showAliasEditor(const QString &objectId, std::shared_ptr<PreviewSession> previewSession = {});
    void showTasks(const QString &tab = "inbox");
    void showGraph();
    void showTimeline(int days);
    void captureUrl();
    void attach();
    void previewAsset(QString id, QJsonObject asset, QWidget *owner=nullptr);
    void preferences();
    void applyTheme();
    void seedDemo(std::function<void()> done);
    void screenshot(const QString &path);

  protected:
    void closeEvent(QCloseEvent *e) override;
};
