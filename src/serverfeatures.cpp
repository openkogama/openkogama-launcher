#include "serverfeatures.h"
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

static constexpr int FetchTimeout = 3000;

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

    const QJsonValue listed = QJsonDocument::fromJson(reply->readAll()).object().value("features");
    if (!listed.isArray()) return std::nullopt;
    QStringList features;
    for (const QJsonValue &feature : listed.toArray())
        if (feature.isString()) features << feature.toString();
    return features;
}

bool ServerFeatures::allows(const std::optional<QStringList> &features, const QString &feature) {
    return !features || features->contains(feature);
}
