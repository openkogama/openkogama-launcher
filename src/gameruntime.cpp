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

#ifdef Q_OS_WIN
constexpr unsigned long CreateNoWindow = 0x08000000;
#else
constexpr int RegistryReadMs = 20000;
constexpr int RegistryWriteMs = 30000;

QString wineBinary()
{
    static const QString chosen = []() {
        const QString configured = qEnvironmentVariable("OPENKOGAMA_WINE").trimmed();
        if (!configured.isEmpty()) return configured;
        for (const QString &name : {QStringLiteral("wine"), QStringLiteral("wine64")}) {
            const QString found = QStandardPaths::findExecutable(name);
            if (!found.isEmpty()) return found;
        }
        return QStringLiteral("wine");
    }();
    return chosen;
}

QString winePrefix()
{
    const QString configured = qEnvironmentVariable("WINEPREFIX").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir::homePath() + "/.wine";
}

QProcessEnvironment wineEnvironment(QProcessEnvironment env)
{
    if (env.isEmpty()) env = QProcessEnvironment::systemEnvironment();
    env.insert("WINEPREFIX", winePrefix());
    if (!env.contains("WINEDEBUG")) env.insert("WINEDEBUG", "-all");
    return env;
}

int runWine(const QStringList &arguments, int timeout, QByteArray *output = nullptr)
{
    QProcess process;
    process.setProcessEnvironment(wineEnvironment({}));
    process.start(wineBinary(), arguments);
    if (!process.waitForFinished(timeout) || process.exitStatus() != QProcess::NormalExit) return -1;
    if (output) *output = process.readAllStandardOutput();
    return process.exitCode();
}
#endif

}

namespace GameRuntime {

bool needsWine(const QString &program)
{
#ifdef Q_OS_WIN
    Q_UNUSED(program);
    return false;
#else
    QFile file(program);
    if (!file.open(QIODevice::ReadOnly)) return program.endsWith(".exe", Qt::CaseInsensitive);
    return file.read(2) == "MZ";
#endif
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

QString gameHome()
{
#ifdef Q_OS_WIN
    return QDir::homePath();
#else
    const QString configured = qEnvironmentVariable("OPENKOGAMA_GAME_HOME").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    const QString user = qEnvironmentVariable("USER", QFileInfo(QDir::homePath()).fileName());
    return QDir::cleanPath(winePrefix() + "/drive_c/users/" + user);
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
    if (!clean.startsWith('/')) return QString(clean).replace('/', '\\');
    const QString drive = QDir::cleanPath(winePrefix() + "/drive_c");
    if (clean == drive || clean.startsWith(drive + "/")) {
        const QString rest = clean.mid(drive.size());
        return "C:" + (rest.isEmpty() ? QStringLiteral("\\") : QString(rest).replace('/', '\\'));
    }
    return "Z:" + QString(clean).replace('/', '\\');
#endif
}

QString serverExecutable()
{
#ifdef Q_OS_WIN
    return ServerName + ".exe";
#else
    return ServerName;
#endif
}

QString serverProgram()
{
    return QDir(Paths::server()).filePath(serverExecutable());
}

bool hasServer()
{
    return QFileInfo(serverProgram()).isFile();
}

bool isServerProgram(const QString &program)
{
    return QFileInfo(program).fileName() == serverExecutable();
}

void prepare(QProcess *process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
#else
    const QString program = process->program();
    if (!needsWine(program)) return;
    process->setArguments(QStringList{program} + process->arguments());
    process->setProgram(wineBinary());
    process->setProcessEnvironment(wineEnvironment(process->processEnvironment()));
#endif
}

QString registryValue(const QString &key, const QString &name)
{
#ifdef Q_OS_WIN
    return QSettings(key, QSettings::NativeFormat).value(name).toString();
#else
    if (!wineAvailable()) return {};
    QByteArray output;
    if (runWine({"reg", "query", key, "/v", name}, RegistryReadMs, &output) != 0) return {};
    static const QRegularExpression spaces("\\s{2,}");
    for (const QString &line : QString::fromLocal8Bit(output).split('\n')) {
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
    settings.sync();
    return settings.status() == QSettings::NoError;
#else
    if (!wineAvailable()) return false;
    return runWine({"reg", "add", key, "/v", name, "/t", "REG_SZ", "/d", value, "/f"}, RegistryWriteMs) == 0;
#endif
}

}
