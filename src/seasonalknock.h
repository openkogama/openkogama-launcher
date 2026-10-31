#ifndef SEASONALKNOCK_H
#define SEASONALKNOCK_H

#include <QDate>
#include <QSettings>

class QProcess;

namespace SeasonalKnock {
inline constexpr qint64 AfterMs = 30 * 60 * 1000;
inline constexpr auto SeenKey = "seasonal/knockPlayed";

inline bool halloween(const QDate &date)
{
    return (date.month() == 10 && date.day() >= 24) || (date.month() == 11 && date.day() <= 2);
}

inline bool advance(qint64 &played, qint64 elapsed, bool active, const QDate &date)
{
    if (!halloween(date)) { played = 0; return false; }
    if (active) played += qBound(qint64(0), elapsed, qint64(2000));
    return active && played >= AfterMs;
}

inline bool claim(QSettings &settings)
{
    settings.sync();
    if (settings.value(SeenKey, false).toBool()) return false;
    settings.setValue(SeenKey, true);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

void watch(QProcess *client);
}

#endif
