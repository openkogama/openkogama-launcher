#include "seasonalknock.h"
#include "paths.h"
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTimer>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
bool focused(QProcess *client)
{
#ifdef Q_OS_WIN
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == static_cast<DWORD>(client->processId());
#else
    Q_UNUSED(client);
    return false;
#endif
}

bool playKnock()
{
#ifdef Q_OS_WIN
    static const QByteArray wave = [] {
        QFile file(":/halloween-knock.wav");
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }();
    return !wave.isEmpty() && PlaySoundW(reinterpret_cast<LPCWSTR>(wave.constData()), nullptr,
        SND_MEMORY | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP);
#else
    return false;
#endif
}
}

void SeasonalKnock::watch(QProcess *client)
{
    const QString settingsPath = Paths::data() + "/settings.ini";
    if (QSettings(settingsPath, QSettings::IniFormat).value(SeenKey, false).toBool()) return;
    auto *timer = new QTimer(client);
    timer->setInterval(1000);
    QElapsedTimer clock;
    clock.start();
    QObject::connect(timer, &QTimer::timeout, timer, [client, timer, settingsPath, clock, played = qint64(0)]() mutable {
        const qint64 elapsed = clock.restart();
        if (client->state() != QProcess::Running) { timer->stop(); return; }
        if (!advance(played, elapsed, focused(client), QDate::currentDate())) return;
        QSettings settings(settingsPath, QSettings::IniFormat);
        if (!claim(settings)) { timer->stop(); return; }
        if (!playKnock()) {
            settings.remove(SeenKey);
            settings.sync();
            return;
        }
        timer->stop();
    });
    QObject::connect(client, &QProcess::finished, timer, &QTimer::stop);
    timer->start();
}
