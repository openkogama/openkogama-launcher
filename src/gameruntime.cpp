#include "gameruntime.h"
#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

const QString ServerName = "openkogama-server";
const QString PlayerName = "openkogama-player";

bool startsLikeWindowsExecutable(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return path.endsWith(".exe", Qt::CaseInsensitive);
    return file.read(2) == "MZ";
}

QString userInPrefix()
{
    return qEnvironmentVariable("USER", QFileInfo(QDir::homePath()).fileName());
}

QString existingName(const QDir &dir, const QString &name)
{
    if (dir.exists(name)) return name;
    if (dir.exists(name + ".exe")) return name + ".exe";
    return name;
}

}

namespace GameRuntime {

bool isWindowsExecutable(const QString &path)
{
    return startsLikeWindowsExecutable(path);
}

bool needsWine(const QString &program)
{
#ifdef Q_OS_WIN
    Q_UNUSED(program);
    return false;
#else
    return startsLikeWindowsExecutable(program);
#endif
}

QString wineBinary()
{
    static const QString chosen = []() {
        const QString configured = qEnvironmentVariable("OPENKOGAMA_WINE").trimmed();
        if (!configured.isEmpty()) return configured;
        for (const char *name : {"wine", "wine64"}) {
            const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name));
            if (!found.isEmpty()) return found;
        }
        return QStringLiteral("wine");
    }();
    return chosen;
}

bool wineAvailable()
{
#ifdef Q_OS_WIN
    return true;
#else
    const QString binary = wineBinary();
    if (QFileInfo(binary).isAbsolute()) return QFileInfo(binary).isExecutable();
    return !QStandardPaths::findExecutable(binary).isEmpty();
#endif
}

QString winePrefix()
{
    const QString configured = qEnvironmentVariable("WINEPREFIX").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir::homePath() + "/.wine";
}

QString gameHome()
{
#ifdef Q_OS_WIN
    return QDir::homePath();
#else
    const QString configured = qEnvironmentVariable("OPENKOGAMA_GAME_HOME").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir::cleanPath(winePrefix() + "/drive_c/users/" + userInPrefix());
#endif
}

QString gameTemp()
{
#ifdef Q_OS_WIN
    return QDir::tempPath();
#else
    return gameHome() + "/Temp";
#endif
}

QString windowsPath(const QString &path)
{
#ifdef Q_OS_WIN
    return QDir::toNativeSeparators(path);
#else
    const QString clean = QDir::cleanPath(path);
    if (!clean.startsWith('/'))
        return QString(clean).replace('/', '\\');
    const QString drive = QDir::cleanPath(winePrefix() + "/drive_c");
    if (clean == drive || clean.startsWith(drive + "/")) {
        QString rest = clean.mid(drive.size());
        if (rest.isEmpty()) rest = "/";
        return "C:" + QString(rest).replace('/', '\\');
    }
    return "Z:" + QString(clean).replace('/', '\\');
#endif
}

QString serverExecutable()
{
    return existingName(QDir(Paths::server()), ServerName);
}

QString playerExecutable()
{
    return existingName(QDir(Paths::playerFolder()), PlayerName);
}

QString serverProgram()
{
    return QDir(Paths::server()).filePath(serverExecutable());
}

QString playerProgram()
{
    return QDir(Paths::playerFolder()).filePath(playerExecutable());
}

bool hasServer()
{
    return QFileInfo(serverProgram()).isFile();
}

bool hasPlayer()
{
    return QFileInfo(playerProgram()).isFile();
}

bool isServerProgram(const QString &program)
{
    const QString name = QFileInfo(program).fileName();
    return name == ServerName || name == ServerName + ".exe";
}

void prepare(QProcess *process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW
    });
#else
    const QString program = process->program();
    if (!needsWine(program)) return;
    QStringList arguments = process->arguments();
    arguments.prepend(program);
    process->setProgram(wineBinary());
    process->setArguments(arguments);
    QProcessEnvironment env = process->processEnvironment();
    if (env.isEmpty()) env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX", winePrefix());
    process->setProcessEnvironment(env);
#endif
}

QString registryValue(const QString &key, const QString &name)
{
#ifdef Q_OS_WIN
    QSettings settings(key, QSettings::NativeFormat);
    return settings.value(name).toString();
#else
    if (!wineAvailable()) return {};
    QProcess process;
    process.start(wineBinary(), {"reg", "query", key, "/v", name});
    if (!process.waitForFinished(20000) || process.exitCode() != 0) return {};
    static const QRegularExpression spaces("\\s{2,}");
    const QStringList lines = QString::fromLocal8Bit(process.readAllStandardOutput()).split('\n');
    for (const QString &line : lines) {
        const QStringList parts = line.trimmed().split(spaces);
        if (parts.size() >= 3 && parts.first() == name) return parts.mid(2).join("  ");
    }
    return {};
#endif
}

bool setRegistryValue(const QString &key, const QString &name, const QString &value)
{
#ifdef Q_OS_WIN
    QSettings settings(key, QSettings::NativeFormat);
    settings.setValue(name, value);
    return settings.status() == QSettings::NoError;
#else
    if (!wineAvailable()) return false;
    QProcess process;
    process.start(wineBinary(), {"reg", "add", key, "/v", name, "/t", "REG_SZ", "/d", value, "/f"});
    return process.waitForFinished(30000) && process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
#endif
}

}
