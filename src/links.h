#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QObject;

namespace Links {
QString find(const QStringList &arguments);
QString serverOf(const QString &link);
QString signInCodeOf(const QString &link);
bool forward(const QString &link);
void listen(QObject *owner, std::function<void(const QString &)> open);
void registerScheme();
}
