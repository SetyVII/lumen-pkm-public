#include "lumen_api.hpp"
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>
#include <stdexcept>
namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTcpServer server;
        require(server.listen(QHostAddress::LocalHost, 0), "listen isolated fake API");
        bool compatible = false;
        int posts = 0;
        QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
            while (server.hasPendingConnections()) {
                auto *socket = server.nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n"))
                        return;
                    if (socket->property("served").toBool())
                        return;
                    socket->setProperty("served", true);
                    if (request->startsWith("POST "))
                        ++posts;
                    const QByteArray body = "{\"id\":\"test-object\"}";
                    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
                    if (compatible)
                        response += "X-Lumen-Document-Version: 2\r\n";
                    response += "Content-Length: " + QByteArray::number(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body;
                    socket->write(response);
                    socket->disconnectFromHost();
                });
            }
        });
        Api api;
        api.base = "http://127.0.0.1:" + QString::number(server.serverPort());
        auto run = [&](bool write) {
            QEventLoop loop;
            QJsonValue value;
            QString error;
            bool answered = false;
            auto callback = [&](QJsonValue result, QString err) {
                answered = true;
                value = result;
                error = err;
                loop.quit();
            };
            if (write)
                api.post(
                    "/v1/objects",
                    QJsonObject{{"document", QJsonObject{{"schema_version", 2}, {"blocks", QJsonArray()}}}},
                    callback);
            else
                api.get("/v1/objects/test-object", callback);
            QTimer::singleShot(4000, &loop, &QEventLoop::quit);
            if (!answered)
                loop.exec();
            require(answered, "protocol response received");
            return QPair<QJsonValue, QString>{value, error};
        };
        const auto old = run(false);
        require(!old.second.isEmpty() && old.first.isNull(), "legacy response body never reaches editor");
        const auto blocked = run(true);
        require(!blocked.second.isEmpty() && posts == 0,
                "document write preflight blocks old server before POST");
        compatible = true;
        const auto modern = run(false);
        require(modern.second.isEmpty() && modern.first.toObject().value("id") == "test-object",
                "protocol 2 reads allowed");
        const auto saved = run(true);
        require(saved.second.isEmpty() && posts == 1, "verified backend accepts write");
        qInfo() << "API_PROTOCOL_TEST_OK";
    } catch (const std::exception &e) {
        qCritical() << e.what();
        return 1;
    }
}
