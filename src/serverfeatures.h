#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace ServerFeatures {
std::optional<QStringList> fetch(const QString &serverUrl, int timeout = 3000);
QStringList rules(const QString &serverUrl, int timeout = 3000);
QString address(const QString &serverUrl);
int udpPort(const QString &serverUrl);
bool allows(const std::optional<QStringList> &features, const QString &feature);
}
