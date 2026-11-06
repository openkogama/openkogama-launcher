#ifndef WEBPLAYERRUNTIME_H
#define WEBPLAYERRUNTIME_H

#include <QString>

class QWidget;

namespace WebPlayerRuntime {

bool isWebPlayerFile(const QString &name);
bool ensureInstalled(QWidget *parent, const QString &file);

}

#endif // WEBPLAYERRUNTIME_H
