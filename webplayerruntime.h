#ifndef WEBPLAYERRUNTIME_H
#define WEBPLAYERRUNTIME_H

#include <QString>

class QWidget;

namespace WebPlayerRuntime {

bool isWebPlayerFile(const QString &name);
QString bundledDir(const QString &name);
QString playerPath();
bool ensureInstalled(QWidget *parent);

}

#endif // WEBPLAYERRUNTIME_H
