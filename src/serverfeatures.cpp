#include "serverfeatures.h"
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

static constexpr int FetchTimeout = 3000;

static QHash<QString, int> &udpPorts() {
    static QHash<QString, int> ports;
    return ports;
}

std::optional<QStringList> ServerFeatures::fetch(const QString &serverUrl) {
    QNetworkRequest request(QUrl(serverUrl + "/api/server"));
    request.setTransferTimeout(FetchTimeout);
    QNetworkAccessManager net;
    QNetworkReply *reply = net.get(request);
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return std::nullopt;

    const QJsonObject server = QJsonDocument::fromJson(reply->readAll()).object();
    if (const int port = server.value("udp").toInt(); port > 0) udpPorts().insert(serverUrl, port);
    const QJsonValue listed = server.value("features");
    if (!listed.isArray()) return std::nullopt;
    QStringList features;
    for (const QJsonValue &feature : listed.toArray())
        if (feature.isString()) features << feature.toString();
    return features;
}

int ServerFeatures::udpPort(const QString &serverUrl) {
    return udpPorts().value(serverUrl);
}

bool ServerFeatures::allows(const std::optional<QStringList> &features, const QString &feature) {
    return !features || features->contains(feature);
}
