#include "seasonalknock.h"
#include <QCoreApplication>
#include <QDebug>
#include <QTemporaryDir>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int checks = 0;
    auto check = [&checks](bool pass, const char *message) {
        if (!pass) qFatal("FAIL %s", message);
        ++checks;
        qInfo("PASS %s", message);
    };
    const QDate halloween(2026, 10, 31);
    check(!SeasonalKnock::halloween(QDate(2026, 10, 23)) && SeasonalKnock::halloween(QDate(2026, 10, 24))
        && SeasonalKnock::halloween(QDate(2026, 11, 2)) && !SeasonalKnock::halloween(QDate(2026, 11, 3)), "date boundaries");
    qint64 played = 0;
    for (int i = 0; i < 1799; ++i)
        if (SeasonalKnock::advance(played, 1000, true, halloween)) qFatal("played before thirty minutes");
    check(played == 1799000, "no playback before thirty minutes");
    check(!SeasonalKnock::advance(played, 1000, false, halloween) && played == 1799000, "background time is excluded");
    check(SeasonalKnock::advance(played, 1000, true, halloween), "active game reaches thirty minutes");
    played = 0;
    check(!SeasonalKnock::advance(played, 3600000, true, halloween) && played == 2000, "sleep or blocked event loop does not count as play time");
    check(!SeasonalKnock::advance(played, 1000, true, QDate(2026, 12, 1)) && played == 0, "out of season resets countdown");
    QTemporaryDir folder;
    check(folder.isValid(), "isolated persistent storage");
    const QString path = folder.filePath("settings.ini");
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue("unrelated/setting", 7);
        check(SeasonalKnock::claim(settings), "first playback can be claimed");
        check(!SeasonalKnock::claim(settings), "second playback is refused");
    }
    QSettings reopened(path, QSettings::IniFormat);
    check(!SeasonalKnock::claim(reopened), "reopened settings remember playback");
    check(reopened.value("unrelated/setting").toInt() == 7, "other settings are preserved");
    qInfo("%d knock checks passed", checks);
}
