#include "clientpatches.h"
#include "paths.h"
#include "serverfeatures.h"
#include "settings.h"
#include "singleinstance.h"
#include <QCursor>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
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
static constexpr quint32 CompressionMask = 0x3F;
static constexpr int MaxCacheName = 100;

namespace {

const QString SeveralWindows = "several-windows";
const QString SoundEmitterList = "sound-emitter-list";
const QString FreshBundles = "fresh-bundles";

QString counted(qsizetype count, const QString &thing) {
    return QString("%1 %2%3").arg(count).arg(thing, count == 1 ? "" : "s");
}

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

bool patch(const QString &original, const QJsonArray &list, const QString &data) {
    QProcess process;
    process.setProgram(QDir(Paths::server()).filePath("openkogama-server.exe"));
    process.setArguments({"client", "patch", original, "-", data});
    process.setWorkingDirectory(Paths::serverData());
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    process.start();
    process.write(QJsonDocument(QJsonObject{{"sounds", list}}).toJson(QJsonDocument::Compact));
    process.closeWriteChannel();
    const bool done = process.waitForFinished(PatchTimeout) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!done) qWarning("Client patch failed: %s", process.readAll().constData());
    return done;
}

QString soundEmitterList(const QString &instancePath, const QJsonArray &list) {
    const QString data = dataFile(instancePath);
    if (data.isEmpty()) return {};
    const QString original = data + ".original";

    if (list.isEmpty()) {
        if (patched(data) && QFile::exists(original) && replace(original, data)) return "put the original data.unity3d back";
        return {};
    }

    if (!patched(data) && QFileInfo(original).size() != QFileInfo(data).size()) {
        QFile::remove(original);
        if (!QFile::copy(data, original)) return {};
    }
    if (!QFile::exists(original)) return {};

    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    const bool done = patch(original, list, data);
    if (!done && patched(data)) replace(original, data);
    QGuiApplication::restoreOverrideCursor();
    return done ? "added " + counted(list.size(), "server sound") + " to the Sound Emitter list in data.unity3d"
                : "could not add the server sounds to the Sound Emitter list, the game keeps its own";
}

}

const QList<ClientPatch> &ClientPatches::all() {
    static const QList<ClientPatch> patches = {
        {SeveralWindows, "Several game windows",
         "Closes the lock that keeps a game version to one window, so another window can start."},
        {SoundEmitterList, "Server sounds in the Sound Emitter list",
         "Adds the server's own sounds to the Sound Emitter list of 3.5 by rewriting kogama_Data/data.unity3d. "
         "The original stays as data.unity3d.original and comes back when the server has none or this is off."},
        {FreshBundles, "Fresh copies of replaced bundles",
         "Removes the game's cached copies of bundles the server replaces, so the game downloads the server's version."},
    };
    return patches;
}

QStringList ClientPatches::apply(const QString &instancePath, const QString &serverUrl, const QString &version, const std::optional<QStringList> &features) {
    QStringList notes;
    if (Settings::clientPatch(SeveralWindows))
        if (const int released = SingleInstance::release(Paths::instances()); released > 0)
            notes << "closed the one window lock in " + counted(released, "running game");

    const bool sounds = Settings::clientPatch(SoundEmitterList);
    const bool fresh = Settings::clientPatch(FreshBundles);
    const QJsonObject served = (sounds || fresh) && ServerFeatures::allows(features, "content") ? content(serverUrl, version) : QJsonObject();
    if (fresh)
        if (const int removed = dropReplaced(instancePath, served.value("replaced").toArray()); removed > 0)
            notes << "removed " + counted(removed, "cached bundle") + " the server replaces";
    if (const QString note = soundEmitterList(instancePath, sounds ? served.value("sounds").toArray() : QJsonArray()); !note.isEmpty())
        notes << note;
    return notes;
}
