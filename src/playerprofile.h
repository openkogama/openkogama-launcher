#ifndef PLAYERPROFILE_H
#define PLAYERPROFILE_H

#include <QString>
#include <QStringList>
#include <optional>

class QObject;

namespace PlayerProfile {

QString send(const QString &url, const QString &key, bool everything, const std::optional<QStringList> &features);
void keep(const QString &url, const QString &key, bool local, QObject *owner);
void refresh(QObject *owner);

}

#endif // PLAYERPROFILE_H
