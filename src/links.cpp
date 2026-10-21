#include "links.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QUrl>
#include <QUrlQuery>

namespace {
const QString Scheme = "openkogama";
constexpr int LinkLimit = 2048;
constexpr int ConnectTimeoutMs = 500;
constexpr int WriteTimeoutMs = 1000;

QString pipeName()
{
    return "openkogama-launcher-" + qEnvironmentVariable("USERNAME", "user");
}
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
#endif
}

}
