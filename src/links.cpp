#include "links.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>

namespace {
const QString Scheme = "openkogama";
constexpr int LinkLimit = 16384;
constexpr int ConnectTimeoutMs = 500;
constexpr int WriteTimeoutMs = 1000;
const QStringList ArchiveHosts = {"openkogama.org", "archive.org"};

QString pipeName()
{
#ifdef Q_OS_WIN
    return "openkogama-launcher-" + qEnvironmentVariable("USERNAME", "user");
#else
    return "openkogama-launcher-" + qEnvironmentVariable("USER", "user");
#endif
}

#ifndef Q_OS_WIN
QString desktopQuoted(const QString &path)
{
    QString quoted = path;
    quoted.replace('\\', "\\\\\\\\").replace('"', "\\\\\"").replace('`', "\\\\`").replace('$', "\\\\$").replace('%', "%%");
    return "\"" + quoted + "\"";
}
#endif
}

namespace Links {

QString find(const QStringList &arguments)
{
    for (const QString &argument : arguments.mid(1))
        if (argument.startsWith(Scheme + ":", Qt::CaseInsensitive) && argument.size() <= LinkLimit) return argument;
    return {};
}

QString serverOf(const QString &link)
{
    const QUrl url(link);
    if (url.scheme().compare(Scheme, Qt::CaseInsensitive) != 0 || url.host().compare("join", Qt::CaseInsensitive) != 0) return {};
    return GameServer::addressOf(QUrlQuery(url).queryItemValue("server", QUrl::FullyDecoded));
}

QString signInCodeOf(const QString &link)
{
    const QUrl url(link);
    if (url.scheme().compare(Scheme, Qt::CaseInsensitive) != 0 || url.host().compare("signed-in", Qt::CaseInsensitive) != 0) return {};
    return QUrlQuery(url).queryItemValue("code", QUrl::FullyDecoded).trimmed();
}

bool fromArchive(const QUrl &url)
{
    if (url.scheme() != "https") return false;
    const QString host = url.host();
    for (const QString &allowed : ArchiveHosts)
        if (host == allowed || host.endsWith("." + allowed)) return true;
    return false;
}

std::optional<ImportRequest> importOf(const QString &link)
{
    const QUrl url(link);
    if (url.scheme().compare(Scheme, Qt::CaseInsensitive) != 0 || url.host().compare("import", Qt::CaseInsensitive) != 0) return std::nullopt;

    const QUrlQuery query(url);
    ImportRequest request;
    request.kind = query.queryItemValue("kind", QUrl::FullyDecoded).trimmed().toLower();
    if (request.kind != "map" && request.kind != "model" && request.kind != "avatar") return std::nullopt;
    request.name = query.queryItemValue("name", QUrl::FullyDecoded).trimmed();
    request.sha256 = query.queryItemValue("sha256", QUrl::FullyDecoded).trimmed().toLower();
    request.scale = query.queryItemValue("scale", QUrl::FullyDecoded).trimmed();

    for (const auto &[key, value] : query.queryItems(QUrl::FullyDecoded)) {
        if (key != "url") continue;
        const QUrl source(value);
        if (source.isValid() && fromArchive(source)) request.urls.append(source);
    }
    if (request.urls.isEmpty()) return std::nullopt;

    bool ok = false;
    const qint64 offset = query.queryItemValue("offset", QUrl::FullyDecoded).toLongLong(&ok);
    if (ok && offset >= 0) request.offset = offset;
    const qint64 length = query.queryItemValue("length", QUrl::FullyDecoded).toLongLong(&ok);
    if (ok && length > 0) request.length = length;
    return request;
}

bool forward(const QString &link)
{
    QLocalSocket socket;
    socket.connectToServer(pipeName());
    if (!socket.waitForConnected(ConnectTimeoutMs)) return false;
    socket.write(link.toUtf8() + '\n');
    const bool sent = socket.waitForBytesWritten(WriteTimeoutMs);
    socket.disconnectFromServer();
    return sent;
}

void listen(QObject *owner, std::function<void(const QString &)> open)
{
    auto *server = new QLocalServer(owner);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(pipeName())) {
        server->deleteLater();
        return;
    }
    QObject::connect(server, &QLocalServer::newConnection, server, [server, open]() {
        while (QLocalSocket *socket = server->nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, open]() {
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() > LinkLimit) socket->abort();
                    return;
                }
                const QString link = QString::fromUtf8(socket->readLine(LinkLimit + 1)).trimmed();
                socket->disconnectFromServer();
                if (!link.isEmpty()) open(link);
            });
        }
    });
}

void registerScheme()
{
#ifdef Q_OS_WIN
    if (QDir(QCoreApplication::applicationDirPath()).dirName() != "current") return;
    QSettings classes("HKEY_CURRENT_USER\\Software\\Classes\\" + Scheme, QSettings::NativeFormat);
    const QString command = "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\" \"%1\"";
    if (classes.value("shell/open/command/Default").toString() == command) return;
    classes.setValue("Default", "URL:OpenKogama");
    classes.setValue("URL Protocol", "");
    classes.setValue("shell/open/command/Default", command);
#else
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString applications = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (appImage.isEmpty() || applications.isEmpty() || !QDir().mkpath(applications)) return;
    const QString icons = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/icons/hicolor/192x192/apps";
    if (!QFile::exists(icons + "/openkogama-launcher.png") && QDir().mkpath(icons))
        QFile::copy(":/logo.png", icons + "/openkogama-launcher.png");
    const QString entry = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=OpenKogama Launcher\n"
        "Comment=Play classic KoGaMa versions, join servers and manage your worlds\n"
        "Exec=%1 %u\n"
        "Icon=openkogama-launcher\n"
        "Terminal=false\n"
        "Categories=Game;\n"
        "MimeType=x-scheme-handler/%2;\n"
        "StartupWMClass=openkogama-launcher\n").arg(desktopQuoted(appImage), Scheme);
    QFile desktop(applications + "/openkogama-launcher.desktop");
    if (desktop.open(QIODevice::ReadOnly) && QString::fromUtf8(desktop.readAll()) == entry) return;
    desktop.close();
    if (!desktop.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    desktop.write(entry.toUtf8());
    desktop.close();
    QProcess::execute("xdg-mime", {"default", "openkogama-launcher.desktop", "x-scheme-handler/" + Scheme});
    QProcess::execute("update-desktop-database", {applications});
#endif
}

}
