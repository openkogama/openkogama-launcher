#include "languages.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QMap>
#include <QTranslator>

namespace {
const QString Prefix = "launcher_";
const QString Suffix = ".qm";

QString usualName(const QLocale &locale)
{
    QMap<QString, int> counts;
    for (const QLocale &variant : QLocale::matchingLocales(locale.language(), locale.script(), QLocale::AnyTerritory))
        ++counts[variant.nativeLanguageName()];
    QString name = locale.nativeLanguageName();
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > counts.value(name)) name = it.key();
    return name;
}

QLocale chosenLocale()
{
    const QString chosen = Settings::language();
    return chosen.isEmpty() ? QLocale::system() : QLocale(chosen);
}
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
    const QLocale locale(code);
    const QString name = code.contains('_') ? locale.nativeLanguageName() : usualName(locale);
    return name.isEmpty() ? code : name.left(1).toUpper() + name.mid(1);
}

QString current()
{
    return chosenLocale().bcp47Name();
}

void install(QCoreApplication &app)
{
    const QLocale locale = chosenLocale();
    auto *launcher = new QTranslator(&app);
    if (launcher->load(locale, "launcher", "_", ":/i18n")) app.installTranslator(launcher);
    auto *qt = new QTranslator(&app);
    if (qt->load(locale, "qt", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) app.installTranslator(qt);
}

}
