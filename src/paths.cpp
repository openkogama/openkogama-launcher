#include "paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace {

QString bundled(const QString &name) {
    QString path = QCoreApplication::applicationDirPath() + "/" + name;
    return QDir(path).exists() ? path : Paths::data() + "/" + name;
}

}

QString Paths::data() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString Paths::instances() {
    return data() + "/instances";
}

QString Paths::mods() {
    return data() + "/mods";
}

QString Paths::player() {
    return bundled("player") + "/openkogama-player.exe";
}

QString Paths::server() {
    return bundled("server");
}

QString Paths::serverData() {
    return data() + "/server";
}
