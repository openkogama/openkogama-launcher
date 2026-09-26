#include "settings.h"

#include <QSettings>
#include <QStandardPaths>

namespace {

QSettings store()
{
    return QSettings(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/settings.ini", QSettings::IniFormat);
}

}

namespace Settings {

bool consoleOnLaunch()
{
    return store().value("console/showOnLaunch", false).toBool();
}

bool consoleOnCrash()
{
    return store().value("console/showOnCrash", true).toBool();
}

void setConsoleOnLaunch(bool enabled)
{
    store().setValue("console/showOnLaunch", enabled);
}

void setConsoleOnCrash(bool enabled)
{
    store().setValue("console/showOnCrash", enabled);
}

}
