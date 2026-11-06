#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

class QObject;

struct ImportRequest {
    QString kind;
    QString name;
    QList<QUrl> urls;
    qint64 offset = -1;
    qint64 length = -1;
    QString sha256;
    QString scale;
};

namespace Links {
QString find(const QStringList &arguments);
QString serverOf(const QString &link);
QString signInCodeOf(const QString &link);
bool fromArchive(const QUrl &url);
std::optional<ImportRequest> importOf(const QString &link);
bool forward(const QString &link);
void listen(QObject *owner, std::function<void(const QString &)> open);
void registerScheme();
}
