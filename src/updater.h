#ifndef UPDATER_H
#define UPDATER_H

#include <QString>

class QAction;
class QWidget;

namespace Updater {

void startup();
void check(QWidget *window, QAction *action, bool asked);
QString version();

}

#endif // UPDATER_H
