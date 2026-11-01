#pragma once

// Minimal loopback HTTP server for the collections UI test. It implements the
// slice of the Lumen API that MainWindow touches and mirrors the validation
// semantics of the backend collections stage
// (docs/implementation/backend/collections.md + contracts/SYNC.md "Collections").
// No external server and no real secrets: it listens on an ephemeral loopback
// port inside the test process.

#include <QDateTime>
#include <QDate>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

class FakeApi final : public QObject {
  public:
    QTcpServer server;
    QJsonArray types;
    QJsonArray objects;
    struct Request {
        QString method, path;
        QJsonObject body;
        int status = 0;
    };
    QList<Request> log;
    int nextId = 1;
    QHash<QTcpSocket *, QByteArray> buffers;
    QJsonValue lastValue;

    explicit FakeApi(QObject *parent = nullptr) : QObject(parent) {
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (auto *socket = server.nextPendingConnection()) {
                // QPointer guards the deferred re-read: the socket is
                // deleteLater()d on disconnect, and a queued readyRead/read
                // timer must not touch the freed object (ASan found this in
                // the find/replace test, which issues more requests).
                connect(socket, &QTcpSocket::readyRead, this,
                        [this, guard = QPointer<QTcpSocket>(socket)] {
                            if (guard) read(guard.data());
                        });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
                    buffers.remove(socket);
                    socket->disconnect(this);
                    socket->deleteLater();
                });
            }
        });
    }
    ~FakeApi() override {
        // The server's child sockets are deleted after the members (including
        // `buffers`) are gone; their disconnected handler would then touch a
        // destroyed hash. Detach them while everything is still alive.
        for (auto *socket : server.findChildren<QTcpSocket *>())
            socket->disconnect(this);
    }
    bool start() { return server.listen(QHostAddress::LocalHost, 0); }
    QUrl baseUrl() const { return QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())); }
    QString newId() { return QString("obj-%1").arg(nextId++); }
    static QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

    QJsonObject findObject(const QString &id) const {
        for (auto v : objects)
            if (v.toObject().value("id").toString() == id)
                return v.toObject();
        return {};
    }
    QJsonObject findType(const QString &id) const {
        for (auto v : types)
            if (v.toObject().value("id").toString() == id)
                return v.toObject();
        return {};
    }
    bool replaceObject(const QJsonObject &object) {
        for (int i = 0; i < objects.size(); ++i)
            if (objects.at(i).toObject().value("id").toString() == object.value("id").toString()) {
                objects[i] = object;
                return true;
            }
        return false;
    }
    static QString itemTypeId(const QJsonObject &collection) {
        return collection.value("properties").toObject().value("item_type_id").toString();
    }
    static QStringList itemIds(const QJsonObject &collection) {
        QStringList ids;
        for (auto v : collection.value("properties").toObject().value("items").toArray())
            ids << v.toString();
        return ids;
    }
    // Backend invariants: every item exists, matches item_type_id, and an
    // object appears at most once per collection.
    QString validateCollectionItems(const QString &typeId, const QJsonArray &items) const {
        QStringList seen;
        for (auto v : items) {
            const QString id = v.toString();
            const auto object = findObject(id);
            if (object.isEmpty())
                return "Item does not exist: " + id;
            if (object.value("type_id").toString() != typeId)
                return "Item is not of the collection type: " + id;
            if (seen.contains(id))
                return "Item is already in this collection: " + id;
            seen << id;
        }
        return {};
    }
    // property_links is derived (a set), like on the server.
    QJsonObject withDerivedLinks(const QJsonObject &object) const {        QJsonObject result = object;
        QJsonArray links;
        for (auto id : object.value("links").toArray())
            if (!links.contains(id))
                links.append(id);
        for (auto id : object.value("properties").toObject().value("items").toArray())
            if (!links.contains(id))
                links.append(id);
        result["property_links"] = links;
        return result;
    }
    QJsonObject saveCollection(QJsonObject collection, const QStringList &items) {
        auto properties = collection.value("properties").toObject();
        QJsonArray array;
        for (const auto &id : items) array.append(id);
        properties["items"] = array;
        collection["properties"] = properties;
        collection["revision"] = collection.value("revision").toInt() + 1;
        collection["updated_at"] = now();
        collection = withDerivedLinks(collection);
        replaceObject(collection);
        return collection;
    }

    // Aliases contract slice (docs/implementation/backend/aliases.md +
    // contracts/SYNC.md "Aliases"), mirroring the server exactly:
    // normalization (trim, drop empty, case-insensitive dedupe keeping the
    // first spelling, empty removes the key), limits (50 aliases x 200
    // characters) enforced with the server's 400 messages, and the search
    // matcher — title and content hits carry no matched_alias, an alias
    // hit carries the stored alias.
    static QStringList objectAliases(const QJsonObject &object) {
        QStringList aliases;
        for (auto v : object.value("properties").toObject().value("aliases").toArray()) {
            const QString alias = v.toString().trimmed();
            if (!alias.isEmpty())
                aliases << alias;
        }
        return aliases;
    }
    static QString normalizeAliases(QJsonObject &properties) {
        if (!properties.contains("aliases"))
            return {};
        const QJsonValue raw = properties.value("aliases");
        if (!raw.isArray())
            return "Property aliases must be an array of strings";
        QStringList out;
        for (auto v : raw.toArray()) {
            if (!v.isString())
                return "Property aliases must be an array of strings";
            const QString alias = v.toString().trimmed();
            if (alias.isEmpty())
                continue;
            if (alias.size() > 200)
                return "Each alias supports up to 200 characters";
            bool duplicate = false;
            for (const auto &kept : out)
                if (kept.compare(alias, Qt::CaseInsensitive) == 0) {
                    duplicate = true;
                    break;
                }
            if (!duplicate)
                out << alias;
        }
        if (out.size() > 50)
            return "An object supports up to 50 aliases";
        if (out.isEmpty()) {
            properties.remove("aliases");
            return {};
        }
        QJsonArray array;
        for (const auto &alias : out) array.append(alias);
        properties["aliases"] = array;
        return {};
    }
    static QString firstMatchingAlias(const QJsonObject &object, const QString &query) {
        for (const auto &alias : objectAliases(object))
            if (alias.contains(query, Qt::CaseInsensitive))
                return alias;
        return {};
    }
    static bool objectMatchesQuery(const QJsonObject &object, const QString &query) {
        if (object.value("title").toString().contains(query, Qt::CaseInsensitive) ||
            object.value("content").toString().contains(query, Qt::CaseInsensitive))
            return true;
        return !firstMatchingAlias(object, query).isEmpty();
    }
    int search(const QUrlQuery &query) {
        const QString q = query.queryItemValue("q");
        const QString typeId = query.queryItemValue("type_id");
        QJsonArray results;
        for (auto v : objects) {
            auto object = v.toObject();
            if (object.value("properties").toObject().value("is_template").toBool())
                continue;
            if (!typeId.isEmpty() && object.value("type_id").toString() != typeId)
                continue;
            if (!q.isEmpty()) {
                const bool textHit = object.value("title").toString().contains(q, Qt::CaseInsensitive) ||
                                     object.value("content").toString().contains(q, Qt::CaseInsensitive);
                const QString alias = textHit ? QString() : firstMatchingAlias(object, q);
                if (!textHit && alias.isEmpty())
                    continue;
                if (!alias.isEmpty())
                    object["matched_alias"] = alias;
            }
            results.append(object);
        }
        lastValue = results;
        lastStatus = 200;
        return 200;
    }

  private:
    void read(QTcpSocket *socket) {
        if (!socket) return;
        auto &buffer = buffers[socket];
        buffer.append(socket->readAll());
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        int contentLength = 0;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        for (int i = 1; i < lines.size(); ++i) {
            QByteArray line = lines[i];
            line.replace('\r', QByteArray());
            if (line.startsWith("Content-Length:"))
                contentLength = line.mid(15).trimmed().toInt();
        }
        if (buffer.size() < headerEnd + 4 + contentLength)
            return;
        const QList<QByteArray> requestLine = lines.first().split(' ');
        const QByteArray bodyBytes = buffer.mid(headerEnd + 4, contentLength);
        buffer.remove(0, headerEnd + 4 + contentLength);
        const QString method = QString::fromUtf8(requestLine.value(0));
        const QUrl url(QString::fromUtf8(requestLine.value(1)));
        const QJsonObject body = QJsonDocument::fromJson(bodyBytes).object();
        const int status = route(method, url, body);
        const QString loggedPath = url.path() + (url.hasQuery() ? "?" + url.query() : QString());
        log.append(Request{method, loggedPath, body, status});
        static const QHash<int, QString> reasons{{200, "OK"},    {201, "Created"}, {400, "Bad Request"},
                                                 {404, "Not Found"}, {409, "Conflict"}};
        respond(socket, status, reasons.value(status, "Error"));
        if (!buffer.isEmpty())
            QTimer::singleShot(0, this, [this, guard = QPointer<QTcpSocket>(socket)] {
                if (guard && buffers.contains(guard.data()))
                    read(guard.data());
            });
    }
    void respond(QTcpSocket *socket, int status, const QString &reason) {
        const QByteArray body = lastValue.isArray()
                                    ? QJsonDocument(lastValue.toArray()).toJson(QJsonDocument::Compact)
                                    : QJsonDocument(lastValue.toObject()).toJson(QJsonDocument::Compact);
        QByteArray text = "HTTP/1.1 " + QByteArray::number(status) + " " + reason.toUtf8() + "\r\n";
        text += "Content-Type: application/json\r\n";
        text += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        text += "Connection: close\r\n\r\n";
        text += body;
        socket->write(text);
        socket->flush();
        socket->disconnectFromHost();
    }
    void fail(const QString &message, int status = 400) {
        lastValue = QJsonObject{{"error", message}};
        lastStatus = status;
    }
    int lastStatus = 200;

    // GET /v1/objects with the query-filter slice the desktop uses: type_id
    // and backlinked_count (server semantics: the deduped union of objects
    // whose links or property_links point at the candidate). Templates
    // (D5 contract) are excluded unless include_templates=true — the same
    // default the server applies to every listing.
    QJsonArray listObjects(const QUrlQuery &query) {
        const QString typeId = query.queryItemValue("type_id");
        const QString backlinked = query.queryItemValue("backlinked_count");
        const QString searchQuery = query.queryItemValue("q");
        const bool includeTemplates = query.queryItemValue("include_templates") == "true";
        int min = -1, max = -1;
        const bool hasBacklinked = !backlinked.isEmpty();
        if (hasBacklinked) {
            // "N" is exact; "min,max", "min," and ",max" are open intervals.
            const QStringList parts = backlinked.split(',');
            if (parts.size() == 1)
                min = max = parts.value(0).toInt();
            else {
                if (!parts.value(0).isEmpty())
                    min = parts.value(0).toInt();
                if (!parts.value(1).isEmpty())
                    max = parts.value(1).toInt();
            }
        }
        QJsonArray listed;
        for (auto v : objects) {
            const auto object = v.toObject();
            if (!typeId.isEmpty() && object.value("type_id").toString() != typeId)
                continue;
            if (!includeTemplates &&
                object.value("properties").toObject().value("is_template").toBool())
                continue;
            // Aliases slice: GET /v1/objects?q= uses the same matcher as
            // GET /v1/search (title, content, aliases) but never carries
            // matched_alias.
            if (!searchQuery.isEmpty() && !objectMatchesQuery(object, searchQuery))
                continue;
            if (hasBacklinked) {
                QSet<QString> sources;
                for (auto other : objects) {
                    const auto linker = withDerivedLinks(other.toObject());
                    if (allLinks(linker).contains(object.value("id")))
                        sources.insert(other.toObject().value("id").toString());
                }
                const int count = sources.size();
                if ((min >= 0 && count < min) || (max >= 0 && count > max))
                    continue;
            }
            listed.append(object);
        }
        return listed;
    }

    int route(const QString &method, const QUrl &url, const QJsonObject &body) {
        const QUrlQuery query(url);
        const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
        lastValue = QJsonObject{{"error", "Not found"}};
        lastStatus = 404;

        if (method == "GET" && url.path() == "/v1/types") {
            lastValue = QJsonObject{{"types", types}};
            lastStatus = 200;
            return 200;
        }
        if (url.path() == "/v1/objects" && method == "GET") {
            lastValue = listObjects(query);
            lastStatus = 200;
            return 200;
        }
        // Aliases slice: GET /v1/search matches title, content and aliases
        // and reports matched_alias for alias-only hits.
        if (url.path() == "/v1/search" && method == "GET")
            return search(query);
        if (url.path() == "/v1/objects" && method == "POST")
            return createObject(body);
        // D5: POST /v1/objects/from-template — deep copy of the template with
        // fresh block ids and {date:...} resolution (the documented slice).
        if (url.path() == "/v1/objects/from-template" && method == "POST")
            return fromTemplate(body);
        // /v1/objects/{id}[/backlinks] and /v1/collections/{id}/items[/{oid}]
        if (parts.value(0) == "v1" && parts.value(1) == "objects" && parts.size() >= 3) {
            const QString id = parts.value(2);
            if (parts.size() == 3) {
                if (method == "GET")
                    return getObject(id);
                if (method == "PATCH")
                    return patchObject(id, body);
                if (method == "DELETE")
                    return deleteObject(id);
            }
            if (parts.size() == 4 && parts.value(3) == "backlinks" && method == "GET")
                return backlinks(id, query.queryItemValue("type_id"));
        }
        // D5: PATCH /v1/types/{id} with default_template_id validation.
        if (parts.value(0) == "v1" && parts.value(1) == "types" && parts.size() == 3 &&
            method == "PATCH")
            return patchType(parts.value(2), body);
        if (parts.value(0) == "v1" && parts.value(1) == "journal" && parts.size() == 3 && method == "GET")
            return journal(parts.value(2), query.queryItemValue("apply_default_template") == "true");
        if (parts.value(0) == "v1" && parts.value(1) == "collections" && parts.size() >= 4) {
            const QString id = parts.value(2);
            if (parts.value(3) != "items")
                return lastStatus;
            if (parts.size() == 4) {
                if (method == "GET")
                    return collectionItems(id);
                if (method == "POST")
                    return addItem(id, body);
                if (method == "PUT")
                    return putItems(id, body);
            }
            if (parts.size() == 5 && method == "DELETE")
                return removeItem(id, parts.value(4));
        }
        return lastStatus;
    }

    int createObject(const QJsonObject &body) {
        const QString typeId = body.value("type_id").toString();
        if (findType(typeId).isEmpty()) {
            fail("Unknown type: " + typeId);
            return 400;
        }
        QJsonObject object;
        object["id"] = newId();
        object["type_id"] = typeId;
        object["title"] = body.value("title").toString();
        object["content"] = body.value("content").toString();
        object["links"] = body.value("links").toArray();
        QJsonObject properties = body.value("properties").toObject();
        if (typeId == "collection") {
            const QString itemTypeId = properties.value("item_type_id").toString();
            if (itemTypeId.isEmpty() || findType(itemTypeId).isEmpty()) {
                fail("item_type_id must reference an existing type");
                return 400;
            }
            if (itemTypeId == "collection") {
                fail("A collection cannot hold collections");
                return 400;
            }
            const QString problem = validateCollectionItems(itemTypeId, properties.value("items").toArray());
            if (!problem.isEmpty()) {
                fail(problem);
                return 400;
            }
        } else {
            properties.remove("item_type_id");
            properties.remove("items");
        }
        // Aliases slice: the server normalizes and validates on every write.
        if (const QString problem = normalizeAliases(properties); !problem.isEmpty()) {
            fail(problem);
            return 400;
        }
        object["properties"] = properties;
        object["revision"] = 1;
        object["created_at"] = now();
        object["updated_at"] = now();
        object["sync_status"] = "synced";
        object = withDerivedLinks(object);
        objects.append(object);
        lastValue = object;
        lastStatus = 201;
        return 201;
    }

    int getObject(const QString &id) {
        const auto object = findObject(id);
        if (object.isEmpty()) {
            fail("Object not found", 404);
            return 404;
        }
        lastValue = object;
        lastStatus = 200;
        return 200;
    }

    int patchObject(const QString &id, const QJsonObject &body) {
        auto object = findObject(id);
        if (object.isEmpty()) {
            fail("Object not found", 404);
            return 404;
        }
        const QString typeId = object.value("type_id").toString();
        const QJsonObject properties = body.value("properties").toObject();
        if (typeId == "collection" && body.contains("properties")) {
            if (properties.contains("item_type_id") &&
                properties.value("item_type_id").toString() != itemTypeId(object)) {
                fail("item_type_id is immutable after creation");
                return 400;
            }
            const QString problem =
                validateCollectionItems(itemTypeId(object), properties.value("items").toArray());
            if (!problem.isEmpty()) {
                fail(problem);
                return 400;
            }
        }
        if (body.contains("title"))
            object["title"] = body.value("title");
        if (body.contains("content"))
            object["content"] = body.value("content");
        if (body.contains("properties")) {
            // Aliases slice: full replacement document, normalized and
            // validated exactly like the server (400 on violations).
            auto incoming = body.value("properties").toObject();
            if (const QString problem = normalizeAliases(incoming); !problem.isEmpty()) {
                fail(problem);
                return 400;
            }
            object["properties"] = incoming;
        }
        QJsonArray links = body.value("links").toArray(object.value("links").toArray());
        for (auto v : body.value("add_links").toArray())
            if (!links.contains(v))
                links.append(v);
        object["links"] = links;
        object["revision"] = object.value("revision").toInt() + 1;
        object["updated_at"] = now();
        object = withDerivedLinks(object);
        replaceObject(object);
        lastValue = object;
        lastStatus = 200;
        return 200;
    }

    int deleteObject(const QString &id) {
        const auto object = findObject(id);
        if (object.isEmpty()) {
            fail("Object not found", 404);
            return 404;
        }
        for (int i = 0; i < objects.size(); ++i)
            if (objects.at(i).toObject().value("id").toString() == id) {
                objects.removeAt(i);
                break;
            }
        // Deleting an object removes it from every collection (order of the
        // remaining items is preserved).
        for (int i = 0; i < objects.size(); ++i) {
            auto candidate = objects.at(i).toObject();
            if (candidate.value("type_id").toString() != "collection")
                continue;
            const QStringList items = itemIds(candidate);
            if (!items.contains(id))
                continue;
            QStringList kept;
            for (const auto &item : items)
                if (item != id)
                    kept << item;
            candidate = saveCollection(candidate, kept);
            objects[i] = candidate;
        }
        lastValue = QJsonObject{{"deleted", true}, {"id", id}};
        lastStatus = 200;
        return 200;
    }

    int backlinks(const QString &id, const QString &typeFilter) {
        if (findObject(id).isEmpty()) {
            fail("Object not found", 404);
            return 404;
        }
        QJsonArray found;
        for (auto v : objects) {
            auto o = withDerivedLinks(v.toObject());
            if (!allLinks(o).contains(id))
                continue;
            if (!typeFilter.isEmpty() && o.value("type_id").toString() != typeFilter)
                continue;
            found.append(o);
        }
        lastValue = found;
        lastStatus = 200;
        return 200;
    }
    static QJsonArray allLinks(const QJsonObject &object) {
        QJsonArray ids = object.value("links").toArray();
        for (auto id : object.value("property_links").toArray())
            if (!ids.contains(id))
                ids.append(id);
        return ids;
    }

    // D5: {date}/{date:iso|long|short|weekday} resolution (the documented
    // formats; unknown formats stay verbatim). Month/weekday names are
    // English, like the server's resolver.
    static QString resolveDateVariables(const QString &text, const QString &date) {
        const QLocale english(QLocale::English);
        const QDate day = QDate::fromString(date, Qt::ISODate);
        QString result = text;
        result.replace("{date:iso}", date);
        result.replace("{date:long}", english.toString(day, "MMMM d, yyyy"));
        result.replace("{date:short}", english.toString(day, "M/d/yy"));
        result.replace("{date:weekday}", english.dayName(day.dayOfWeek()));
        result.replace("{date}", date);
        return result;
    }
    // Every copied block receives a fresh id (POST /v1/objects semantics).
    QJsonObject copyForTemplate(const QJsonObject &source, const QString &date, const QString &title) {
        QJsonObject object;
        object["id"] = newId();
        object["type_id"] = source.value("type_id");
        object["title"] = resolveDateVariables(title, date);
        object["content"] = source.value("content");
        QJsonObject properties = source.value("properties").toObject();
        properties.remove("is_template");
        QJsonArray blocks;
        for (auto v : properties.value("blocks").toArray()) {
            auto block = v.toObject();
            block["id"] = newId();
            blocks.append(block);
        }
        if (properties.contains("blocks"))
            properties["blocks"] = blocks;
        object["properties"] = properties;
        object["links"] = source.value("links").toArray();
        object["revision"] = 1;
        object["created_at"] = now();
        object["updated_at"] = now();
        object["sync_status"] = "synced";
        return withDerivedLinks(object);
    }

    int fromTemplate(const QJsonObject &body) {
        const QString templateId = body.value("template_id").toString();
        const auto source = findObject(templateId);
        if (source.isEmpty()) {
            fail("Template not found", 404);
            return 404;
        }
        if (!source.value("properties").toObject().value("is_template").toBool()) {
            fail("Object is not a template");
            return 400;
        }
        QString date = body.value("date").toString();
        if (date.isEmpty())
            date = QDate::currentDate().toString(Qt::ISODate);
        if (!QDate::fromString(date, Qt::ISODate).isValid()) {
            fail("Invalid date");
            return 400;
        }
        const QString title =
            body.contains("title") ? body.value("title").toString() : source.value("title").toString();
        auto object = copyForTemplate(source, date, title);
        objects.append(object);
        lastValue = object;
        lastStatus = 201;
        return 201;
    }

    int patchType(const QString &id, const QJsonObject &body) {
        auto type = findType(id);
        if (type.isEmpty()) {
            fail("Object type not found", 404);
            return 404;
        }
        if (body.contains("name"))
            type["name"] = body.value("name");
        if (body.contains("color"))
            type["color"] = body.value("color");
        if (body.contains("properties"))
            type["properties"] = body.value("properties");
        QString defaultId = type.value("default_template_id").toString();
        if (body.contains("default_template_id"))
            defaultId = body.value("default_template_id").toString();
        if (!defaultId.isEmpty()) {
            const auto source = findObject(defaultId);
            if (source.isEmpty()) {
                fail("Default template does not exist");
                return 400;
            }
            if (source.value("type_id").toString() != id) {
                fail("Default template must belong to this type");
                return 400;
            }
            if (!source.value("properties").toObject().value("is_template").toBool()) {
                fail("Default template must be a template");
                return 400;
            }
        }
        type["default_template_id"] = defaultId;
        for (int i = 0; i < types.size(); ++i)
            if (types.at(i).toObject().value("id").toString() == id)
                types[i] = type;
        lastValue = type;
        lastStatus = 200;
        return 200;
    }

    int journal(const QString &date, bool applyDefault) {
        if (!QDate::fromString(date, Qt::ISODate).isValid()) {
            fail("Invalid date");
            return 400;
        }
        for (auto v : objects) {
            auto o = v.toObject();
            if (o.value("type_id").toString() == "journal" &&
                o.value("properties").toObject().value("date").toString() == date) {
                lastValue = o;
                lastStatus = 200;
                return 200;
            }
        }
        QJsonObject object;
        object["id"] = "journal-" + date;
        object["type_id"] = "journal";
        object["title"] = date;
        object["content"] = "";
        object["links"] = QJsonArray{};
        object["properties"] = QJsonObject{{"date", date}};
        object["revision"] = 1;
        object["created_at"] = now();
        object["updated_at"] = now();
        object["sync_status"] = "synced";
        // D5: lazy creation from the journal type's default template. The
        // note keeps its canonical date title and properties.date; only
        // content, properties and links come from the template.
        if (applyDefault) {
            const auto journalType = findType("journal");
            const auto source = findObject(journalType.value("default_template_id").toString());
            if (!source.isEmpty() && source.value("type_id").toString() == "journal" &&
                source.value("properties").toObject().value("is_template").toBool()) {
                auto built = copyForTemplate(source, date, date);
                object["content"] = built.value("content");
                object["properties"] = built.value("properties").toObject();
                object["links"] = built.value("links").toArray();
                auto properties = object.value("properties").toObject();
                properties["date"] = date;
                object["properties"] = properties;
            }
        }
        objects.append(object);
        lastValue = object;
        lastStatus = 200;
        return 200;
    }

    int collectionItems(const QString &id) {
        const auto collection = findObject(id);
        if (collection.isEmpty()) {
            fail("Collection not found", 404);
            return 404;
        }
        QJsonArray items;
        for (const auto &itemId : itemIds(collection)) {
            const auto object = findObject(itemId);
            if (!object.isEmpty() && object.value("sync_status").toString() != "deleted")
                items.append(object);
        }
        lastValue = QJsonObject{{"collection_id", id},
                                {"item_type_id", itemTypeId(collection)},
                                {"item_ids", collection.value("properties").toObject().value("items").toArray()},
                                {"items", items}};
        lastStatus = 200;
        return 200;
    }

    int addItem(const QString &id, const QJsonObject &body) {
        auto collection = findObject(id);
        if (collection.isEmpty()) {
            fail("Collection not found", 404);
            return 404;
        }
        if (collection.value("type_id").toString() != "collection") {
            fail("Object is not a collection");
            return 400;
        }
        const QString objectId = body.value("object_id").toString();
        const auto object = findObject(objectId);
        if (object.isEmpty()) {
            fail("Item does not exist: " + objectId);
            return 400;
        }
        if (object.value("type_id").toString() != itemTypeId(collection)) {
            fail("Item is not of the collection type: " + objectId);
            return 400;
        }
        QStringList items = itemIds(collection);
        if (items.contains(objectId)) {
            fail("Item is already in this collection", 409);
            return 409;
        }
        int position = body.contains("position") ? body.value("position").toInt(-1) : items.size();
        if (position < 0 || position > items.size()) {
            fail("Invalid position");
            return 400;
        }
        items.insert(position, objectId);
        collection = saveCollection(collection, items);
        lastValue = collection;
        lastStatus = 200;
        return 200;
    }

    int putItems(const QString &id, const QJsonObject &body) {
        auto collection = findObject(id);
        if (collection.isEmpty()) {
            fail("Collection not found", 404);
            return 404;
        }
        if (!body.contains("items")) {
            fail("items is required");
            return 400;
        }
        const QJsonArray items = body.value("items").toArray();
        const QString problem = validateCollectionItems(itemTypeId(collection), items);
        if (!problem.isEmpty()) {
            fail(problem);
            return 400;
        }
        QStringList ids;
        for (auto v : items) ids << v.toString();
        collection = saveCollection(collection, ids);
        lastValue = collection;
        lastStatus = 200;
        return 200;
    }

    int removeItem(const QString &id, const QString &objectId) {
        auto collection = findObject(id);
        if (collection.isEmpty()) {
            fail("Collection not found", 404);
            return 404;
        }
        QStringList items = itemIds(collection);
        if (!items.contains(objectId)) {
            fail("Item is not in this collection", 404);
            return 404;
        }
        items.removeAll(objectId);
        collection = saveCollection(collection, items);
        lastValue = collection;
        lastStatus = 200;
        return 200;
    }
};
