#pragma once

#include <QString>
#include <functional>

class QProcess;
class QWidget;

namespace GameLauncher {
QProcess *start(const QString &path, const QString &exe, const QString &url,
    const QString &mode, int world, QWidget *owner, QWidget *parent, bool viaCode = false, const std::function<void()> &retry = {});
bool resumeJoin();
}
