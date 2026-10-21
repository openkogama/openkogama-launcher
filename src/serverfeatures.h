#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace ServerFeatures {
std::optional<QStringList> fetch(const QString &serverUrl);
bool allows(const std::optional<QStringList> &features, const QString &feature);
}
