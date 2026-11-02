#pragma once

#include <QByteArray>
#include <QCache>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>

using Callback = std::function<void(QJsonValue, QString)>;
using CallbackEx = std::function<void(int, QJsonValue, QString)>;

class Api : public QObject {
    QNetworkAccessManager manager;
    QString documentProtocolBase_;
    QCache<QString, QByteArray> assetCache{64 * 1024};
    QHash<QString, QList<std::function<void(QByteArray, QString)>>> assetWaiters;

  public:
    QString base = qEnvironmentVariable("LUMEN_API_URL", "http://127.0.0.1:8765");
    QByteArray token = qgetenv("LUMEN_TOKEN");
    explicit Api(QObject *p = nullptr) : QObject(p), manager(this) {}
    bool safe() const {
        QUrl u(base);
        return u.isValid() && !u.host().isEmpty() && u.userInfo().isEmpty() && !u.hasQuery() &&
               !u.hasFragment() &&
               (u.scheme() == "https" ||
                (u.scheme() == "http" &&
                 (u.host() == "127.0.0.1" || u.host() == "localhost" || u.host() == "::1")));
    }
    void requestEx(const QString &method, const QString &path, const QJsonObject &body, CallbackEx cb) {
        if (!safe()) {
            cb(0, {}, "La conexión remota necesita HTTPS.");
            return;
        }
        if (method != "GET" && body.contains("document") && documentProtocolBase_ != base) {
            requestEx("GET", "/v1/types", {}, [this, method, path, body, cb](int, QJsonValue, QString error) {
                if (!error.isEmpty()) { cb(0, {}, error); return; }
                requestEx(method, path, body, cb);
            }); return;
        }
        const auto requestBase = base;
        QNetworkRequest req(QUrl(base + path));
        req.setRawHeader("Authorization", "Bearer " + token);
        req.setRawHeader("X-Lumen-Document-Version", "2");
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        req.setRawHeader("Cache-Control", "no-store");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(18000);
        QNetworkReply *reply = nullptr;
        auto bytes = QJsonDocument(body).toJson(QJsonDocument::Compact);
        if (method == "GET")
            reply = manager.get(req);
        else
            reply = manager.sendCustomRequest(req, method.toUtf8(), bytes);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, cb, method, requestBase] {
            const auto data = reply->readAll();
            const auto doc = QJsonDocument::fromJson(data);
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString err;
            if (method == "GET" && status / 100 == 2) {
                if (reply->rawHeader("X-Lumen-Document-Version").trimmed() != "2") {
                    documentProtocolBase_.clear();
                    cb(status, {}, "Servidor incompatible: actualiza el backend al protocolo de documento 2 antes de abrir o guardar notas.");
                    reply->deleteLater(); return;
                }
                documentProtocolBase_ = requestBase;
            }
            if (status / 100 == 3)
                err = "El servidor respondió con una redirección. Revisa la dirección de la API.";
            else if (reply->error() != QNetworkReply::NoError) {
                err = doc.object().value("error").toString();
                if (err.isEmpty())
                    err = reply->errorString();
            }
            cb(status, doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), err);
            reply->deleteLater();
        });
    }
    void request(const QString &method, const QString &path, const QJsonObject &body, Callback cb) {
        requestEx(method, path, body, [cb](int, QJsonValue value, QString error) { cb(value, error); });
    }
    void get(const QString &p, Callback cb) {
        request("GET", p, {}, cb);
    }
    void post(const QString &p, const QJsonObject &b, Callback cb) {
        request("POST", p, b, cb);
    }
    void patch(const QString &p, const QJsonObject &b, Callback cb) {
        if (b.size() == 1 && b.contains("add_links")) {
            // A commutative relation intent has no stale body snapshot. Obtain
            // its CAS base explicitly; never use this path for document edits.
            get(p, [this, p, b, cb](QJsonValue current, QString error) {
                if (!error.isEmpty()) { cb({}, error); return; }
                auto patch = b; patch["base_revision"] = current.toObject().value("revision");
                request("PATCH", p, patch, cb);
            });
            return;
        }
        request("PATCH", p, b, cb);
    }
    void patchEx(const QString &p, const QJsonObject &b, CallbackEx cb) {
        requestEx("PATCH", p, b, cb);
    }
    void upload(const QString &objectId, const QString &path, Callback cb) {
        if (!safe()) {
            cb({}, "Se requiere HTTPS.");
            return;
        }
        auto *file = new QFile(path);
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            cb({}, "No se pudo abrir el archivo.");
            return;
        }
        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart ref;
        ref.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"object_id\"");
        ref.setBody(objectId.toUtf8());
        multi->append(ref);
        QHttpPart part;
        QString name = QFileInfo(path).fileName();
        name.remove('"');
        name.remove('\r');
        name.remove('\n');
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       "form-data; name=\"file\"; filename=\"" + name + "\"");
        part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(path).name());
        part.setBodyDevice(file);
        file->setParent(multi);
        multi->append(part);
        QNetworkRequest req(QUrl(base + "/v1/assets"));
        req.setRawHeader("Authorization", "Bearer " + token);
        req.setRawHeader("X-Lumen-Document-Version", "2");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(30000);
        auto *reply = manager.post(req, multi);
        multi->setParent(reply);
        QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb] {
            auto d = QJsonDocument::fromJson(reply->readAll());
            QString error;
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() / 100 == 3)
                error = "La API ha redirigido la subida. Revisa la dirección del servidor.";
            else if (reply->error() != QNetworkReply::NoError)
                error = d.object().value("error").toString(reply->errorString());
            cb(d.object(), error);
            reply->deleteLater();
        });
    }
    void uploadBytes(const QString &objectId, const QString &filename, const QByteArray &bytes, const QString &mimeType, Callback cb) {
        if (!safe()) {
            cb({}, "Se requiere HTTPS.");
            return;
        }
        auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
        QHttpPart ref;
        ref.setHeader(QNetworkRequest::ContentDispositionHeader, "form-data; name=\"object_id\"");
        ref.setBody(objectId.toUtf8());
        multi->append(ref);
        QHttpPart part;
        QString name = filename;
        name.remove('"');
        name.remove('\r');
        name.remove('\n');
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       "form-data; name=\"file\"; filename=\"" + name + "\"");
        part.setHeader(QNetworkRequest::ContentTypeHeader, mimeType.isEmpty() ? "application/octet-stream" : mimeType);
        part.setBody(bytes);
        multi->append(part);
        QNetworkRequest req(QUrl(base + "/v1/assets"));
        req.setRawHeader("Authorization", "Bearer " + token);
        req.setRawHeader("X-Lumen-Document-Version", "2");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(30000);
        auto *reply = manager.post(req, multi);
        multi->setParent(reply);
        QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb] {
            auto d = QJsonDocument::fromJson(reply->readAll());
            QString error;
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() / 100 == 3)
                error = "La API ha redirigido la subida. Revisa la dirección del servidor.";
            else if (reply->error() != QNetworkReply::NoError)
                error = d.object().value("error").toString(reply->errorString());
            cb(d.object(), error);
            reply->deleteLater();
        });
    }
    void bytes(const QString &path, std::function<void(QByteArray, QString)> cb) {
        if (!safe()) {
            cb({}, "Se requiere HTTPS");
            return;
        }
        if (auto cached = assetCache.object(path)) {
            const auto data = *cached;
            QTimer::singleShot(0, this, [cb, data] {
                cb(data, {});
            });
            return;
        }
        if (assetWaiters.contains(path)) {
            assetWaiters[path].append(std::move(cb));
            return;
        }
        assetWaiters[path].append(std::move(cb));
        QNetworkRequest req(QUrl(base + path));
        req.setRawHeader("Authorization", "Bearer " + token);
        req.setRawHeader("X-Lumen-Document-Version", "2");
        req.setRawHeader("Cache-Control", "no-store");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(15000);
        auto *reply = manager.get(req);
        QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
            auto b = reply->readAll();
            QString error;
            if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() / 100 == 3)
                error = "La API ha redirigido el archivo. Revisa la dirección del servidor.";
            else if (reply->error() != QNetworkReply::NoError)
                error = reply->errorString();
            if (error.isEmpty())
                assetCache.insert(path, new QByteArray(b), qMax(1, int((b.size() + 1023) / 1024)));
            const auto waiters = assetWaiters.take(path);
            for (const auto &callback : waiters)
                callback(b, error);
            reply->deleteLater();
        });
    }
    void clearAssets() {
        assetCache.clear();
    }
};
