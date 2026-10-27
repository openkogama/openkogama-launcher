#include "clientpatches.h"
#include "paths.h"
#include "serverfeatures.h"
#include "settings.h"
#include "singleinstance.h"
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostInfo>
#include <QUdpSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrlQuery>
#include <QtEndian>

static constexpr unsigned long CreateNoWindow = 0x08000000;
static constexpr int ContentTimeout = 3000;
static constexpr int PatchTimeout = 60000;
static constexpr int NothingToChange = 3;
static constexpr quint32 CompressionMask = 0x3F;
static constexpr int MaxCacheName = 100;
static constexpr int ProbeAttempts = 3;
static constexpr int ProbeWaitMs = 700;
static constexpr quint16 PeMachine64 = 0x8664;

namespace {

const QString SeveralWindows = "several-windows";
const QString SoundEmitterList = "sound-emitter-list";
const QString ServerThemes = "server-themes";
const QString FreshBundles = "fresh-bundles";
const QString ColossusLaser = "colossus-laser";
const QString WorldSwitch = "world-switch";
const QString Tcp = "tcp";
const QString ServerTeleport = "teleport";
const QString Mods = "mods";
const QString ModLoader = "okmod.dll";
const QByteArray UdpProbe = "OKUDP?";
const QByteArray Applied = "client fix: applied ";
const QByteArray ThemesUnchanged = "themes unchanged";

QString unityCache(const QString &instancePath) {
    const QDir instance(instancePath);
    for (const QString &folder : instance.entryList({"*_Data"}, QDir::Dirs)) {
        QFile info(instance.filePath(folder + "/app.info"));
        if (!info.open(QIODevice::ReadOnly)) continue;
        const QStringList lines = QString::fromUtf8(info.readAll()).split('\n');
        if (lines.size() < 2 || lines[0].trimmed().isEmpty() || lines[1].trimmed().isEmpty()) continue;
        return QDir::homePath() + "/AppData/LocalLow/Unity/" + lines[0].trimmed() + "_" + lines[1].trimmed();
    }
    return {};
}

bool cacheName(const QString &name) {
    return !name.isEmpty() && name.size() <= MaxCacheName && !name.startsWith('.')
        && !name.contains('/') && !name.contains('\\') && !name.contains(':');
}

QStringList names(const QJsonArray &values) {
    QStringList list;
    for (const QJsonValue &value : values)
        if (cacheName(value.toString())) list << value.toString();
    return list;
}

int dropReplaced(const QString &instancePath, const QJsonArray &replaced) {
    const QString cache = unityCache(instancePath);
    if (cache.isEmpty()) return 0;

    QFile record(QDir(Paths::data()).filePath("replaced-bundles.json"));
    QStringList dropped = names(replaced);
    if (record.open(QIODevice::ReadOnly)) {
        dropped << names(QJsonDocument::fromJson(record.readAll()).array());
        record.close();
    }
    dropped.removeDuplicates();
    int removed = 0;
    for (const QString &name : dropped) {
        QDir folder(cache + "/" + name);
        if (folder.exists() && folder.removeRecursively()) removed++;
    }

    if (record.open(QIODevice::WriteOnly | QIODevice::Truncate))
        record.write(QJsonDocument(QJsonArray::fromStringList(names(replaced))).toJson(QJsonDocument::Compact));
    return removed;
}

QString dataFile(const QString &instancePath) {
    const QDir instance(instancePath);
    for (const QString &folder : instance.entryList({"*_Data"}, QDir::Dirs)) {
        const QString file = instance.filePath(folder + "/data.unity3d");
        if (QFile::exists(file)) return file;
    }
    return {};
}

bool patched(const QString &file) {
    QFile archive(file);
    if (!archive.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = archive.read(128);
    int at = head.indexOf('\0') + 1 + 4;
    at = head.indexOf('\0', at) + 1;
    at = head.indexOf('\0', at) + 1;
    if (at <= 0 || at + 20 > head.size()) return false;
    const quint32 flags = qFromBigEndian<quint32>(head.constData() + at + 16);
    return (flags & CompressionMask) == 0;
}

bool replace(const QString &from, const QString &to) {
    const QString temporary = to + ".tmp";
    QFile::remove(temporary);
    if (!QFile::copy(from, temporary)) return false;
    if (QFile::exists(to) && !QFile::remove(to)) {
        QFile::remove(temporary);
        return false;
    }
    return QFile::rename(temporary, to);
}

QJsonObject content(const QString &serverUrl, const QString &version) {
    QUrl url(serverUrl + "/api/content");
    QUrlQuery query;
    query.addQueryItem("client", version);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(ContentTimeout);

    QNetworkAccessManager net;
    QNetworkReply *reply = net.get(request);
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) return {};
    return QJsonDocument::fromJson(reply->readAll()).object();
}

int serverTool(const QStringList &arguments, const QByteArray &input = {}, QByteArray *output = nullptr) {
    QProcess process;
    process.setProgram(QDir(Paths::server()).filePath("openkogama-server.exe"));
    process.setArguments(arguments);
    process.setWorkingDirectory(Paths::serverData());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    process.start();
    process.write(input);
    process.closeWriteChannel();
    const int code = process.waitForFinished(PatchTimeout) && process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    const QByteArray printed = process.readAll();
    if (output) *output = printed;
    if (code != 0 && code != NothingToChange) qWarning("Client %s failed: %s", qPrintable(arguments.value(1)), printed.constData());
    return code;
}

QString sharedAssets(const QString &instancePath) {
    const QDir instance(instancePath);
    for (const QString &folder : instance.entryList({"*_Data"}, QDir::Dirs)) {
        const QString file = instance.filePath(folder + "/sharedassets0.assets");
        if (QFile::exists(file)) return file;
    }
    return {};
}

bool differs(const QString &file, const QString &original) {
    return QFile::exists(original) && QFileInfo(original).size() != QFileInfo(file).size();
}

QStringList gameData(const QString &instancePath, const QJsonArray &sounds, const QJsonArray &themes, bool *themed) {
    *themed = false;
    const QString data = dataFile(instancePath);
    const QString file = data.isEmpty() ? sharedAssets(instancePath) : data;
    if (file.isEmpty()) return {};
    const QString name = QFileInfo(file).fileName();
    const QString original = file + ".original";
    const QJsonArray soundList = data.isEmpty() ? QJsonArray() : sounds;

    auto restore = [&]() -> QStringList {
        if (differs(file, original) && replace(original, file))
            return {QCoreApplication::translate("ClientPatches", "put the original %1 back").arg(name)};
        return {};
    };
    if (soundList.isEmpty() && themes.isEmpty()) return restore();

    if (!data.isEmpty() && !patched(data) && differs(data, original)) QFile::remove(original);
    if (!QFile::exists(original) && !QFile::copy(file, original)) return {};

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QByteArray output;
    const QJsonObject content{{"sounds", soundList}, {"themes", themes}};
    const int result = serverTool({"client", "patch", original, "-", file}, QJsonDocument(content).toJson(QJsonDocument::Compact), &output);
    QGuiApplication::restoreOverrideCursor();
    if (result == NothingToChange) return restore();
    if (result != 0) {
        restore();
        return {QCoreApplication::translate("ClientPatches", "could not add the server's sounds and themes to %1, the game keeps its own").arg(name)};
    }

    QStringList notes;
    if (!soundList.isEmpty())
        notes << QCoreApplication::translate("ClientPatches", "added %n server sound(s) to the Sound Emitter list in data.unity3d", nullptr, int(soundList.size()));
    *themed = !themes.isEmpty() && !output.contains(ThemesUnchanged);
    if (*themed) notes << QCoreApplication::translate("ClientPatches", "added the server's themes to the theme list in %1").arg(name);
    return notes;
}

QString managedAssembly(const QString &instancePath) {
    const QDir instance(instancePath);
    for (const QString &folder : instance.entryList({"*_Data"}, QDir::Dirs)) {
        const QString file = instance.filePath(folder + "/Managed/Assembly-CSharp.dll");
        if (QFile::exists(file)) return file;
    }
    return {};
}

QString gameAssembly(const QString &instancePath) {
    const QString file = QDir(instancePath).filePath("GameAssembly.dll");
    return QFile::exists(file) ? file : QString();
}

bool is64Bit(const QString &file) {
    QFile binary(file);
    if (!binary.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = binary.read(4096);
    if (head.size() < 0x40) return false;
    const qint32 pe = qFromLittleEndian<qint32>(head.constData() + 0x3C);
    return pe > 0 && pe + 6 <= head.size() && qFromLittleEndian<quint16>(head.constData() + pe + 4) == PeMachine64;
}

bool sameBytes(const QString &first, const QString &second) {
    QFile a(first), b(second);
    if (!a.open(QIODevice::ReadOnly) || !b.open(QIODevice::ReadOnly) || a.size() != b.size()) return false;
    return a.readAll() == b.readAll();
}

bool copyIfChanged(const QString &from, const QString &to) {
    return sameBytes(from, to) || replace(from, to);
}

bool stageLoader(const QString &instancePath) {
    const QString loader = QDir(Paths::server()).filePath(ModLoader);
    return QFile::exists(loader) && copyIfChanged(loader, QDir(instancePath).filePath(ModLoader));
}

QString stageMods(const QString &instancePath, const QString &gameAssembly, bool on) {
    const QDir instance(instancePath);
    QDir staged(instance.filePath("mods"));
    if (!on) {
        if (!QFile::exists(instance.filePath(ModLoader)) && !staged.exists()) return {};
        QFile binary(gameAssembly);
        if (binary.open(QIODevice::ReadOnly) && binary.readAll().contains(ModLoader.toLatin1() + '\0')) return {};
        QFile::remove(instance.filePath(ModLoader));
        staged.removeRecursively();
        return QCoreApplication::translate("ClientPatches", "took okmod.dll and the mods out of the game");
    }

    const QDir source(Paths::mods());
    const QStringList wanted = source.entryList({"*.dll"}, QDir::Files);
    staged.mkpath(".");
    for (const QString &old : staged.entryList({"*.dll"}, QDir::Files))
        if (!wanted.contains(old, Qt::CaseInsensitive)) staged.remove(old);
    int loaded = 0;
    for (const QString &mod : wanted)
        if (copyIfChanged(source.filePath(mod), staged.filePath(mod))) loaded++;
    return QCoreApplication::translate("ClientPatches", "the game loads okmod.dll with %n mod(s) from the launcher's mods folder", nullptr, loaded);
}

QString fixCode(const QString &file, const QStringList &fixes, QStringList &applied) {
    const QString name = QFileInfo(file).fileName();
    const QString original = file + ".original";
    if (fixes.isEmpty()) {
        if (!QFile::exists(original) || !replace(original, file)) return {};
        QFile::remove(original);
        return QCoreApplication::translate("ClientPatches", "put the original %1 back").arg(name);
    }

    const bool fresh = !QFile::exists(original);
    if (fresh && !QFile::copy(file, original)) return {};
    QByteArray output;
    const int result = serverTool(QStringList{"client", "fix", original, file} + fixes, {}, &output);
    if (result == 0) {
        for (const QByteArray &line : output.split('\n'))
            if (line.trimmed().startsWith(Applied)) applied << QString::fromUtf8(line.trimmed().mid(Applied.size()));
        return {};
    }
    if (result == NothingToChange) {
        if (fresh || replace(original, file)) QFile::remove(original);
        return {};
    }
    return QCoreApplication::translate("ClientPatches", "could not patch %1, the game keeps its own").arg(name);
}

bool udpReaches(const QString &serverUrl, int port) {
    const QHostInfo host = QHostInfo::fromName(QUrl(serverUrl).host());
    if (port <= 0 || host.addresses().isEmpty()) return true;
    QUdpSocket socket;
    for (int attempt = 0; attempt < ProbeAttempts; ++attempt) {
        socket.writeDatagram(UdpProbe, host.addresses().first(), quint16(port));
        if (!socket.waitForReadyRead(ProbeWaitMs)) continue;
        while (socket.hasPendingDatagrams()) {
            QByteArray reply(int(socket.pendingDatagramSize()), Qt::Uninitialized);
            socket.readDatagram(reply.data(), reply.size());
            if (reply == UdpProbe) return true;
        }
    }
    return false;
}

QString describe(const QString &fix) {
    if (fix == ColossusLaser) return QCoreApplication::translate("ClientPatches", "wrapped GodzillaLaser.OnUnequip in Assembly-CSharp.dll in a try block");
    if (fix == WorldSwitch) return QCoreApplication::translate("ClientPatches", "the game can switch worlds without restarting");
    if (fix == Tcp) return QCoreApplication::translate("ClientPatches", "the game connects over TCP, because its UDP traffic does not reach this server");
    if (fix == ServerTeleport) return QCoreApplication::translate("ClientPatches", "the server can move your avatar at once");
    return fix;
}

}

const QList<ClientPatch> &ClientPatches::all() {
    static const QList<ClientPatch> patches = {
        {SeveralWindows, QCoreApplication::translate("ClientPatches", "Several game windows"),
         QCoreApplication::translate("ClientPatches", "Closes the lock that keeps a game version to one window, so another window can start.")},
        {SoundEmitterList, QCoreApplication::translate("ClientPatches", "Server sounds in the Sound Emitter list"),
         QCoreApplication::translate("ClientPatches",
             "Adds the server's own sounds to the Sound Emitter list of 3.5 by rewriting kogama_Data/data.unity3d. "
             "The original stays as data.unity3d.original and comes back when the server has none or this is off.")},
        {ServerThemes, QCoreApplication::translate("ClientPatches", "Server themes"),
         QCoreApplication::translate("ClientPatches",
             "Adds the server's own sky themes to the theme menu and gives the game's themes the pictures the server replaced, "
             "by rewriting the theme list in kogama_Data/sharedassets0.assets, or in kogama_Data/data.unity3d on 3.5. "
             "The original stays as .original next to it and comes back when the server has none or this is off.")},
        {FreshBundles, QCoreApplication::translate("ClientPatches", "Fresh copies of replaced bundles"),
         QCoreApplication::translate("ClientPatches", "Removes the game's cached copies of bundles the server replaces, so the game downloads the server's version.")},
        {ColossusLaser, QCoreApplication::translate("ClientPatches", "Colossus laser fix"),
         QCoreApplication::translate("ClientPatches",
             "Wraps GodzillaLaser.OnUnequip in kogama_Data/Managed/Assembly-CSharp.dll in a try block, so 1.47 to 1.92 stop breaking "
             "when someone leaves the Colossus. The original stays as Assembly-CSharp.dll.original and comes back when this is off.")},
        {WorldSwitch, QCoreApplication::translate("ClientPatches", "World switching"),
         QCoreApplication::translate("ClientPatches",
             "Lets a server send you to another world without closing the game: when the server asks the game to sign in again, it cleans up "
             "and loads itself into the new world. Rewrites MVGameControllerBase.TryReauth and turns off HackingToolDetector in "
             "kogama_Data/Managed/Assembly-CSharp.dll, or in GameAssembly.dll on IL2CPP versions. "
             "The original stays as .original next to it and comes back when this is off.")},
        {Tcp, QCoreApplication::translate("ClientPatches", "TCP when UDP is blocked"),
         QCoreApplication::translate("ClientPatches",
             "Before joining a server that offers it, the launcher checks whether the game's UDP traffic gets through. When your network or VPN "
             "blocks it, the game connects over TCP for that launch instead, like a website. Rewrites how MVNetworkGame opens its connection "
             "in kogama_Data/Managed/Assembly-CSharp.dll, or in GameAssembly.dll on IL2CPP versions. "
             "The original stays as .original next to it and comes back on the next launch over UDP.")},
        {ServerTeleport, QCoreApplication::translate("ClientPatches", "Server teleport"),
         QCoreApplication::translate("ClientPatches",
             "Lets the server move your avatar at once, without the hidden vehicles and teleporters it uses otherwise: when the server sends "
             "your own avatar a teleport, the game puts it there. Adds a check after MVNetworkGame.OnUpdateWorldObjectEvent reads a "
             "transform in kogama_Data/Managed/Assembly-CSharp.dll, or in GameAssembly.dll on IL2CPP versions, where the check goes "
             "in place of an OnDrawGizmos the game never calls. The original stays as .original next to it and comes back when this is off.")},
        {Mods, QCoreApplication::translate("ClientPatches", "Mods"),
         QCoreApplication::translate("ClientPatches",
             "Loads the mods from the mods folder in the launcher's data folder into 3.5. Adds okmod.dll to the imports of GameAssembly.dll "
             "with hooks the mods use, and puts okmod.dll and the mods next to the game. Antivirus programs may not like DLLs next to a game, "
             "so this stays off until you turn it on. The original stays as GameAssembly.dll.original and comes back when this is off."),
         false},
    };
    return patches;
}

QStringList ClientPatches::apply(const QString &instancePath, const QString &serverUrl, const QString &version, const std::optional<QStringList> &features,
    QStringList *active, QString *themes) {
    QStringList notes;
    if (Settings::clientPatch(SeveralWindows))
        if (const int released = SingleInstance::release(Paths::instances()); released > 0)
            notes << QCoreApplication::translate("ClientPatches", "closed the one window lock in %n running game(s)", nullptr, released);

    const bool sounds = Settings::clientPatch(SoundEmitterList);
    const bool skies = Settings::clientPatch(ServerThemes);
    const bool fresh = Settings::clientPatch(FreshBundles);
    const QJsonObject served = (sounds || skies || fresh) && ServerFeatures::allows(features, "content") ? content(serverUrl, version) : QJsonObject();
    if (fresh)
        if (const int removed = dropReplaced(instancePath, served.value("replaced").toArray()); removed > 0)
            notes << QCoreApplication::translate("ClientPatches", "removed %n cached bundle(s) the server replaces", nullptr, removed);
    bool themed = false;
    notes << gameData(instancePath, sounds ? served.value("sounds").toArray() : QJsonArray(), skies ? served.value("themes").toArray() : QJsonArray(), &themed);
    QStringList fixes;
    if (Settings::clientPatch(ColossusLaser)) fixes << ColossusLaser;
    if (Settings::clientPatch(WorldSwitch)) fixes << WorldSwitch;
    if (Settings::clientPatch(ServerTeleport)) fixes << ServerTeleport;
    if (Settings::clientPatch(Tcp) && features && features->contains(Tcp) && !udpReaches(serverUrl, ServerFeatures::udpPort(serverUrl)))
        fixes << Tcp;
    QStringList applied;
    if (const QString assembly = managedAssembly(instancePath); !assembly.isEmpty())
        if (const QString note = fixCode(assembly, fixes, applied); !note.isEmpty()) notes << note;
    if (const QString native = gameAssembly(instancePath); !native.isEmpty()) {
        QStringList nativeFixes;
        for (const QString &fix : {WorldSwitch, Tcp, ServerTeleport})
            if (fixes.contains(fix)) nativeFixes << fix;
        if (Settings::clientPatch(Mods, false) && is64Bit(native) && stageLoader(instancePath)) nativeFixes << Mods;
        if (const QString note = fixCode(native, nativeFixes, applied); !note.isEmpty()) notes << note;
        if (const QString note = stageMods(instancePath, native, applied.contains(Mods)); !note.isEmpty()) notes << note;
    }
    for (const QString &fix : applied)
        if (fix != Mods) notes << describe(fix);
    if (themed) applied << "themes";
    if (active) *active = applied;
    if (themes) *themes = themed ? served.value("themesVersion").toString() : QString();
    return notes;
}
