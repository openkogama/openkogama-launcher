#include "languages.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {
const QString Prefix = "launcher_";
const QString Suffix = ".qm";
}

namespace Languages {

QStringList available()
{
    QStringList codes;
    for (const QString &file : QDir(":/i18n").entryList({Prefix + "*" + Suffix}, QDir::Files, QDir::Name))
        codes << file.mid(Prefix.size(), file.size() - Prefix.size() - Suffix.size());
    return codes;
}

QString nameOf(const QString &code)
{
    const QString name = QLocale(code).nativeLanguageName();
    return name.isEmpty() ? code : name.left(1).toUpper() + name.mid(1);
}

void install(QCoreApplication &app)
{
    const QString chosen = Settings::language();
    const QLocale locale = chosen.isEmpty() ? QLocale::system() : QLocale(chosen);
    auto *launcher = new QTranslator(&app);
    if (launcher->load(locale, "launcher", "_", ":/i18n")) app.installTranslator(launcher);
    auto *qt = new QTranslator(&app);
    if (qt->load(locale, "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) app.installTranslator(qt);
}

}
