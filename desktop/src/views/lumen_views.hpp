#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QWidget>
#include <functional>
#include <memory>
#include <limits>

// One query definition and one ordered set of IDs for every native presentation.
// This evaluator also serves inline related-object/query blocks in the editor.
namespace LumenQuery {
QJsonArray evaluate(const QJsonArray &objects, const QJsonArray &types, const QJsonObject &spec);
QString plainText(const QJsonObject &object);
QJsonValue fieldValue(const QJsonObject &object, const QString &field, const QJsonArray &universe);
QString displayValue(const QJsonValue &value);
struct EditResult {
    bool valid = false;
    QJsonObject patch;
    QString error;
};
EditResult validateEdit(const QJsonObject &object, const QJsonArray &types, const QString &field,
                        const QString &text, const QJsonArray &universe);
}

class LumenViews final : public QWidget {
  public:
    explicit LumenViews(QWidget *parent = nullptr);
    ~LumenViews() override;
    void setData(QJsonArray objects, QJsonArray types);
    void setScope(QString typeId, QJsonObject viewSpec = {});
    void setTheme(bool dark, bool reducedMotion = false);
    QJsonObject viewSpec() const;
    QStringList visibleIds() const;
    void setPresentation(const QString &name);
    void setCollectionMode(bool overview);
    // Leaves collection navigation entirely (no overview/all internal tabs):
    // used by the type dashboard, whose tab bar replaces the internal toggle.
    void clearCollectionMode();
    bool collectionMode() const;
    void setRecentIds(QStringList ids);
    bool applyKanbanMove(const QString &id, const QString &value);
    void undoLastMove();

    std::function<void(QString)> onOpen;
    std::function<void(QString)> onCreate;
    std::function<void()> onImport;
    std::function<void(QString, QJsonObject)> onPatch;
    std::function<void(QString, QJsonObject)> onSaveView;
    std::function<void(QString, std::function<void(QByteArray, QString)>)> onAssetRequest;
    // Right-click on a card or table row: (objectId, global position). The host
    // builds the menu (add to collection, reorder, remove...).
    std::function<void(QString, QPoint)> onObjectMenu;

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void rebuild();
    void editFilters();
    void editProperties();
    // D7 (view calculations): -1 adds a new {property, op} to the spec;
    // a valid index edits (or removes) that existing calculation.
    void editCalculation(int index);
    void saveView();
    bool patchField(const QString &id, const QString &field, const QString &value, bool remember);
};
