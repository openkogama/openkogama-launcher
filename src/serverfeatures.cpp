#include "serverfeatures.h"
#include <QEventLoop>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

constexpr int MaxRules = 10;
constexpr int MaxRuleLength = 200;

static QHash<QString, int> &udpPorts() {
    static QHash<QString, int> ports;
    return ports;
}

static QHash<QString, QString> &addresses() {
    static QHash<QString, QString> given;
    return given;
}

static std::optional<QJsonObject> get(const QString &url, int timeout) {
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(timeout);
    QNetworkAccessManager net;
    QNetworkReply *reply = net.get(request);
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return std::nullopt;
    return QJsonDocument::fromJson(reply->readAll()).object();
}

std::optional<QStringList> ServerFeatures::fetch(const QString &serverUrl, int timeout) {
    const std::optional<QJsonObject> server = get(serverUrl + "/api/server", timeout);
    if (!server) return std::nullopt;

    if (const int port = server->value("udp").toInt(); port > 0) udpPorts().insert(serverUrl, port);
    addresses().insert(serverUrl, server->value("url").toString());
    const QJsonValue listed = server->value("features");
    if (!listed.isArray()) return std::nullopt;
    QStringList features;
    for (const QJsonValue &feature : listed.toArray())
        if (feature.isString()) features << feature.toString();
    return features;
}

QStringList ServerFeatures::rules(const QString &serverUrl, int timeout) {
    QStringList rules;
    const QJsonArray listed = get(serverUrl + "/api/server/rules", timeout).value_or(QJsonObject()).value("rules").toArray();
    for (const QJsonValue &rule : listed) {
        const QString text = rule.toString().simplified().left(MaxRuleLength);
        if (!text.isEmpty()) rules << text;
        if (rules.size() == MaxRules) break;
    }
    return rules;
}

QString ServerFeatures::address(const QString &serverUrl) {
    return addresses().value(serverUrl);
}

int ServerFeatures::udpPort(const QString &serverUrl) {
    return udpPorts().value(serverUrl);
}

bool ServerFeatures::allows(const std::optional<QStringList> &features, const QString &feature) {
    return !features || features->contains(feature);
}
