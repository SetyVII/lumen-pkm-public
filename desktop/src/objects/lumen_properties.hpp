#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>
#include <memory>

class LumenProperties final : public QWidget {
  public:
    explicit LumenProperties(QWidget *parent=nullptr);
    ~LumenProperties() override;
    void setData(QJsonObject object,QJsonObject type,QJsonArray objects,QJsonArray types);
    void setTheme(bool dark);
    // Selector de relación reutilizable: devuelve false si se canceló. Con
    // aceptar, result trae los ids elegidos (array o string) o Null si se
    // limpió con "Restablecer". excludeId omite el propio objeto. Lo usan las
    // propiedades y la fila de etiquetas.
    static bool chooseRelationValue(QWidget *parent,const QString &title,const QJsonArray &objects,
                                    const QJsonArray &types,const QJsonObject &definition,
                                    const QJsonValue &prior,const QString &excludeId,QJsonValue &result);
    std::function<void(QString,QJsonValue)> onChange;
    std::function<void()> onAddProperty;
    std::function<void()> onEditType;
  private:
    struct Impl;
    std::unique_ptr<Impl> d;
    void rebuild();
    void change(const QString &key,const QJsonValue &value);
    void chooseRelation(QString key,QJsonObject definition);
};
