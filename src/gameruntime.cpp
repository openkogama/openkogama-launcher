#include "gameruntime.h"
#include "paths.h"
#include "settings.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>
#include <QVersionNumber>
#include <algorithm>

namespace {

const QString ServerName = "openkogama-server";

#ifdef Q_OS_WIN
constexpr unsigned long CreateNoWindow = 0x08000000;
#else
constexpr int RegistryReadMs = 30000;
constexpr int RegistryWriteMs = 30000;
constexpr int PrefixInitMs = 120000;
const QUrl ProtonGeLatestApi(QStringLiteral("https://api.github.com/repos/GloriousEggroll/proton-ge-custom/releases/latest"));

QString compatDataPath()
{
    const QString configured = qEnvironmentVariable("STEAM_COMPAT_DATA_PATH").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir::cleanPath(Paths::data() + "/proton");
}

QString prefixPath()
{
    return compatDataPath() + "/pfx";
}

QString managedRuntimesDir()
{
    return QDir::cleanPath(Paths::data() + "/proton/runtimes");
}

QString resolveProtonScript(const QString &candidate)
{
    if (candidate.trimmed().isEmpty()) return {};
    const QFileInfo info(candidate.trimmed());
    if (info.isDir()) {
        const QFileInfo script(QDir(info.absoluteFilePath()).filePath("proton"));
        if (script.isFile() && script.isExecutable()) return script.absoluteFilePath();
        return {};
    }
    if (info.isFile() && info.isExecutable()) return info.absoluteFilePath();
    return {};
}

QVersionNumber protonVersionKey(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^GE-Proton(\\d+)(?:[-.](\\d+))?(?:[-.](\\d+))?"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name.trimmed());
    if (!m.hasMatch()) return {};
    QList<int> nums;
    for (int i = 1; i <= 3; ++i) {
        if (!m.captured(i).isEmpty()) nums.append(m.captured(i).toInt());
    }
    return QVersionNumber(nums);
}

bool isManagedProton(const QString &script)
{
    if (script.isEmpty()) return false;
    const QString root = QDir::cleanPath(managedRuntimesDir()) + "/";
    return QDir::cleanPath(script).startsWith(root);
}

QList<GameRuntime::ProtonBuild> scanProtonDirs()
{
    QList<GameRuntime::ProtonBuild> builds;
    QSet<QString> seenPaths;
    QSet<QString> seenNames;

    QDir dir(managedRuntimesDir());
    if (dir.exists()) {
        for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (!entry.startsWith(QStringLiteral("GE-Proton"), Qt::CaseInsensitive)) continue;
            const QString script = resolveProtonScript(dir.filePath(entry));
            if (script.isEmpty()) continue;
            const QString canon = QFileInfo(script).canonicalFilePath();
            const QString lowerName = entry.toLower();
            if (seenPaths.contains(canon) || seenNames.contains(lowerName)) continue;
            seenPaths.insert(canon);
            seenNames.insert(lowerName);
            builds.append({entry, script});
        }
    }

    std::sort(builds.begin(), builds.end(), [](const GameRuntime::ProtonBuild &a, const GameRuntime::ProtonBuild &b) {
        const QVersionNumber va = protonVersionKey(a.name);
        const QVersionNumber vb = protonVersionKey(b.name);
        if (va != vb) return va > vb;
        return a.name > b.name;
    });
    return builds;
}

QString steamClientInstallPath()
{
    const QString configured = qEnvironmentVariable("STEAM_COMPAT_CLIENT_INSTALL_PATH").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return compatDataPath();
}

QProcessEnvironment protonEnvironment(QProcessEnvironment env)
{
    if (env.isEmpty()) env = QProcessEnvironment::systemEnvironment();
    const QString compat = compatDataPath();
    QDir().mkpath(compat);
    env.insert("STEAM_COMPAT_DATA_PATH", compat);
    env.insert("STEAM_COMPAT_CLIENT_INSTALL_PATH", steamClientInstallPath());
    if (!env.contains("STEAM_COMPAT_APP_ID")) env.insert("STEAM_COMPAT_APP_ID", "0");
    if (!env.contains("SteamAppId")) env.insert("SteamAppId", "0");
    if (!env.contains("WINEDEBUG")) env.insert("WINEDEBUG", "-all");

    // Strip AppImage runtime library overrides so host Python and Proton binaries run cleanly
    const QString appDir = env.value("APPDIR");
    if (!appDir.isEmpty() && env.contains("LD_LIBRARY_PATH")) {
        const QStringList parts = env.value("LD_LIBRARY_PATH").split(':', Qt::SkipEmptyParts);
        QStringList clean;
        for (const QString &part : parts) {
            if (!part.startsWith(appDir)) clean.append(part);
        }
        if (clean.isEmpty()) env.remove("LD_LIBRARY_PATH");
        else env.insert("LD_LIBRARY_PATH", clean.join(':'));
    }
    env.remove("PYTHONHOME");
    env.remove("PYTHONPATH");
    env.remove("QT_PLUGIN_PATH");
    return env;
}

bool ensurePrefixInitialized(const QString &proton)
{
    if (QFileInfo::exists(prefixPath() + "/system.reg")) return true;
    QProcess init;
    init.setProcessEnvironment(protonEnvironment({}));
    init.start(proton, {QStringLiteral("run"), QStringLiteral("cmd"), QStringLiteral("/c"), QStringLiteral("exit"), QStringLiteral("0")});
    if (!init.waitForFinished(PrefixInitMs) || init.exitStatus() != QProcess::NormalExit) return false;
    return QFileInfo::exists(prefixPath() + "/system.reg");
}

int runInPrefix(const QStringList &arguments, int timeout, QByteArray *output = nullptr)
{
    const QString proton = GameRuntime::activeProton();
    if (proton.isEmpty() || !ensurePrefixInitialized(proton)) return -1;
    QProcess process;
    process.setProcessEnvironment(protonEnvironment({}));
    process.start(proton, QStringList{QStringLiteral("runinprefix")} + arguments);
    if (!process.waitForFinished(timeout) || process.exitStatus() != QProcess::NormalExit) return -1;
    if (output) *output = process.readAllStandardOutput();
    return process.exitCode();
}

QNetworkRequest githubRequest(const QUrl &url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenKogamaLauncher/1.9.7"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return req;
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

QList<ProtonBuild> installedProtons()
{
#ifdef Q_OS_WIN
    return {};
#else
    return scanProtonDirs();
#endif
}

QString activeProton()
{
#ifdef Q_OS_WIN
    return {};
#else
    if (const QString env = resolveProtonScript(qEnvironmentVariable("OPENKOGAMA_PROTON")); !env.isEmpty())
        return env;
    if (const QString configured = resolveProtonScript(Settings::protonPath()); isManagedProton(configured))
        return configured;
    const QList<ProtonBuild> found = scanProtonDirs();
    return found.isEmpty() ? QString() : found.first().path;
#endif
}

bool wineAvailable()
{
#ifdef Q_OS_WIN
    return true;
#else
    return !activeProton().isEmpty();
#endif
}

bool ensureAvailable(QWidget *parent)
{
#ifdef Q_OS_WIN
    Q_UNUSED(parent);
    return true;
#else
    if (!activeProton().isEmpty()) return true;
    const auto answer = QMessageBox::question(parent,
        QCoreApplication::translate("GameRuntime", "GE-Proton is required"),
        QCoreApplication::translate("GameRuntime",
            "GE-Proton is needed to run KoGaMa on Linux.\n\n"
            "Download and install the latest GE-Proton now? (~600 MB)"));
    if (answer != QMessageBox::Yes) return false;
    return installProton(parent);
#endif
}

bool installProton(QWidget *parent, QString *installedPath)
{
#ifdef Q_OS_WIN
    Q_UNUSED(parent);
    Q_UNUSED(installedPath);
    return true;
#else
    QProgressDialog progress(
        QCoreApplication::translate("GameRuntime", "Checking for the latest GE-Proton release..."),
        QCoreApplication::translate("GameRuntime", "Cancel"), 0, 0, parent);
    progress.setWindowTitle(QCoreApplication::translate("GameRuntime", "Install GE-Proton"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();

    QNetworkAccessManager nam;
    bool cancelled = false;
    QObject::connect(&progress, &QProgressDialog::canceled, [&]() { cancelled = true; });

    // 1. Fetch latest release metadata
    QNetworkReply *metaReply = nam.get(githubRequest(ProtonGeLatestApi));
    {
        QEventLoop loop;
        QObject::connect(metaReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&progress, &QProgressDialog::canceled, metaReply, &QNetworkReply::abort);
        loop.exec();
    }
    const QByteArray metaData = metaReply->readAll();
    const QNetworkReply::NetworkError metaErr = metaReply->error();
    const QString metaErrStr = metaReply->errorString();
    metaReply->deleteLater();

    if (cancelled) return false;
    if (metaErr != QNetworkReply::NoError) {
        progress.close();
        QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
            QCoreApplication::translate("GameRuntime", "Could not check for GE-Proton releases: %1").arg(metaErrStr));
        return false;
    }

    const QJsonObject rel = QJsonDocument::fromJson(metaData).object();
    const QString tag = rel.value("tag_name").toString().trimmed();
    const QVersionNumber latestVer = protonVersionKey(tag);

    // Avoid re-downloading if this or a newer GE-Proton is already installed (e.g. via Steam or launcher)
    for (const ProtonBuild &existing : scanProtonDirs()) {
        const QVersionNumber existingVer = protonVersionKey(existing.name);
        const bool sameOrNewer = (!latestVer.isNull() && !existingVer.isNull() && existingVer >= latestVer)
            || (!tag.isEmpty() && existing.name.startsWith(tag, Qt::CaseInsensitive));
        if (sameOrNewer) {
            progress.close();
            if (installedPath) *installedPath = existing.path;
            QMessageBox::information(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
                QCoreApplication::translate("GameRuntime", "The latest GE-Proton (%1) is already installed.").arg(existing.name));
            return false;
        }
    }

    const bool isArm = QSysInfo::currentCpuArchitecture().contains("arm", Qt::CaseInsensitive)
        || QSysInfo::currentCpuArchitecture().contains("aarch64", Qt::CaseInsensitive);
    const QString archSuffix = isArm ? QStringLiteral("aarch64") : QStringLiteral("x86_64");

    QUrl tarballUrl;
    QUrl shaUrl;
    QString tarballName;
    qint64 expectedSize = 0;

    for (const QJsonValue &val : rel.value("assets").toArray()) {
        const QJsonObject asset = val.toObject();
        const QString name = asset.value("name").toString();
        const QUrl url(asset.value("browser_download_url").toString());
        if (!url.isValid() || url.scheme() != "https") continue;
        if (name.endsWith(".tar.gz", Qt::CaseInsensitive)) {
            if (name.contains(archSuffix, Qt::CaseInsensitive)) {
                tarballUrl = url;
                tarballName = name;
                expectedSize = asset.value("size").toVariant().toLongLong();
            } else if (tarballUrl.isEmpty() && !isArm && !name.contains("aarch64", Qt::CaseInsensitive) && !name.contains("arm64", Qt::CaseInsensitive)) {
                tarballUrl = url;
                tarballName = name;
                expectedSize = asset.value("size").toVariant().toLongLong();
            }
        } else if (name.endsWith(".sha512sum", Qt::CaseInsensitive) && name.contains(archSuffix, Qt::CaseInsensitive)) {
            shaUrl = url;
        }
    }

    if (tarballUrl.isEmpty()) {
        progress.close();
        QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
            QCoreApplication::translate("GameRuntime", "Could not find a compatible GE-Proton package for this architecture."));
        return false;
    }

    // 2. Fetch optional SHA-512 checksum
    QString expectedSha512;
    if (!shaUrl.isEmpty()) {
        QNetworkReply *shaReply = nam.get(githubRequest(shaUrl));
        QEventLoop loop;
        QObject::connect(shaReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&progress, &QProgressDialog::canceled, shaReply, &QNetworkReply::abort);
        loop.exec();
        if (!cancelled && shaReply->error() == QNetworkReply::NoError) {
            const QString line = QString::fromUtf8(shaReply->readAll()).trimmed();
            expectedSha512 = line.split(QRegularExpression("\\s+")).value(0).toLower();
        }
        shaReply->deleteLater();
        if (cancelled) return false;
    }

    // 3. Download tarball with live progress & SHA-512 verification
    const QString runtimesDir = managedRuntimesDir();
    QDir().mkpath(runtimesDir);
    const QString archivePath = runtimesDir + "/.download-" + tarballName;
    QFile outFile(archivePath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        progress.close();
        QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
            QCoreApplication::translate("GameRuntime", "Could not write to %1").arg(archivePath));
        return false;
    }

    const QString displayName = tag.isEmpty() ? tarballName : tag;
    progress.setRange(0, 100);
    progress.setValue(0);
    progress.setLabelText(QCoreApplication::translate("GameRuntime", "Downloading %1...").arg(displayName));

    QCryptographicHash hasher(QCryptographicHash::Sha512);
    qint64 receivedBytes = 0;
    QNetworkReply *dlReply = nam.get(githubRequest(tarballUrl));
    {
        QEventLoop loop;
        QObject::connect(dlReply, &QNetworkReply::readyRead, [&]() {
            const QByteArray chunk = dlReply->readAll();
            if (!chunk.isEmpty()) {
                outFile.write(chunk);
                hasher.addData(chunk);
                receivedBytes += chunk.size();
            }
        });
        QObject::connect(dlReply, &QNetworkReply::downloadProgress, [&](qint64 done, qint64 total) {
            const qint64 denom = total > 0 ? total : expectedSize;
            if (denom > 0) {
                progress.setValue(qBound(0, int(done * 100 / denom), 100));
                progress.setLabelText(QCoreApplication::translate("GameRuntime", "Downloading %1 (%2 / %3)...")
                    .arg(displayName, QLocale().formattedDataSize(done), QLocale().formattedDataSize(denom)));
            }
        });
        QObject::connect(dlReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&progress, &QProgressDialog::canceled, dlReply, &QNetworkReply::abort);
        loop.exec();
    }
    if (dlReply->bytesAvailable() > 0) {
        const QByteArray rest = dlReply->readAll();
        outFile.write(rest);
        hasher.addData(rest);
        receivedBytes += rest.size();
    }
    outFile.close();

    const QNetworkReply::NetworkError dlErr = dlReply->error();
    const QString dlErrStr = dlReply->errorString();
    dlReply->deleteLater();

    if (cancelled) {
        QFile::remove(archivePath);
        return false;
    }
    if (dlErr != QNetworkReply::NoError || receivedBytes == 0) {
        QFile::remove(archivePath);
        progress.close();
        QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
            QCoreApplication::translate("GameRuntime", "Could not download %1: %2").arg(displayName, dlErrStr));
        return false;
    }
    if (!expectedSha512.isEmpty()) {
        const QString actualSha512 = QString::fromLatin1(hasher.result().toHex()).toLower();
        if (actualSha512 != expectedSha512) {
            QFile::remove(archivePath);
            progress.close();
            QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
                QCoreApplication::translate("GameRuntime", "Downloaded %1 failed checksum verification. Please try again.").arg(displayName));
            return false;
        }
    }

    // 4. Extract archive into staging dir, then move into runtimesDir
    const QString stagingDir = runtimesDir + "/.extract-tmp";
    QDir(stagingDir).removeRecursively();
    QDir().mkpath(stagingDir);

    progress.setRange(0, 0);
    progress.setLabelText(QCoreApplication::translate("GameRuntime", "Extracting %1...").arg(displayName));

    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("-xzf"), archivePath, QStringLiteral("-C"), stagingDir});
    {
        QEventLoop loop;
        QObject::connect(&tar, &QProcess::finished, &loop, &QEventLoop::quit);
        QObject::connect(&tar, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
        QObject::connect(&progress, &QProgressDialog::canceled, &tar, &QProcess::kill);
        loop.exec();
    }
    QFile::remove(archivePath);

    if (cancelled || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        QDir(stagingDir).removeRecursively();
        progress.close();
        if (!cancelled) {
            QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
                QCoreApplication::translate("GameRuntime", "Could not extract %1.").arg(displayName));
        }
        return false;
    }

    QString extractedName;
    for (const QString &entry : QDir(stagingDir).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!resolveProtonScript(QDir(stagingDir).filePath(entry)).isEmpty()) {
            extractedName = entry;
            break;
        }
    }

    if (extractedName.isEmpty()) {
        QDir(stagingDir).removeRecursively();
        progress.close();
        QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
            QCoreApplication::translate("GameRuntime", "The downloaded archive did not contain a valid GE-Proton runtime."));
        return false;
    }

    const QString finalDir = QDir(runtimesDir).filePath(extractedName);
    if (QDir(finalDir).exists())
        QDir(finalDir).removeRecursively();
    if (!QDir().rename(QDir(stagingDir).filePath(extractedName), finalDir)) {
        QDir(stagingDir).removeRecursively();
        progress.close();
        QMessageBox::warning(parent, QCoreApplication::translate("GameRuntime", "Install GE-Proton"),
            QCoreApplication::translate("GameRuntime", "Could not move %1 into place.").arg(extractedName));
        return false;
    }
    QDir(stagingDir).removeRecursively();

    const QString script = resolveProtonScript(finalDir);
    if (installedPath) *installedPath = script;
    Settings::setProtonPath(script);
    progress.close();
    return !script.isEmpty();
#endif
}

QString gameHome()
{
#ifdef Q_OS_WIN
    return QDir::homePath();
#else
    const QString configured = qEnvironmentVariable("OPENKOGAMA_GAME_HOME").trimmed();
    if (!configured.isEmpty()) return QDir::cleanPath(configured);
    return QDir::cleanPath(prefixPath() + "/drive_c/users/steamuser");
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
    const QString drive = QDir::cleanPath(prefixPath() + "/drive_c");
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
    const QString proton = activeProton();
    if (proton.isEmpty()) return;
    process->setArguments(QStringList{QStringLiteral("run"), program} + process->arguments());
    process->setProgram(proton);
    process->setProcessEnvironment(protonEnvironment(process->processEnvironment()));
#endif
}

QString registryValue(const QString &key, const QString &name)
{
#ifdef Q_OS_WIN
    return QSettings(key, QSettings::NativeFormat).value(name).toString();
#else
    if (!wineAvailable()) return {};
    QByteArray output;
    if (runInPrefix({"reg", "query", key, "/v", name}, RegistryReadMs, &output) != 0) return {};
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
    return runInPrefix({"reg", "add", key, "/v", name, "/t", "REG_SZ", "/d", value, "/f"}, RegistryWriteMs) == 0;
#endif
}

}
