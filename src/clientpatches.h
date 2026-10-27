#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

struct ClientPatch
{
    QString id;
    QString name;
    QString description;
    bool byDefault = true;
};

namespace ClientPatches {
const QList<ClientPatch> &all();
QStringList apply(const QString &instancePath, const QString &serverUrl, const QString &version, const std::optional<QStringList> &features,
    QStringList *active = nullptr, QString *themes = nullptr);
}
