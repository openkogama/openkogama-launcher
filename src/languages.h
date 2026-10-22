#pragma once

#include <QString>
#include <QStringList>

class QCoreApplication;

namespace Languages {
QStringList available();
QString nameOf(const QString &code);
void install(QCoreApplication &app);
}
