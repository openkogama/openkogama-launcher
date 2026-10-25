#include "discordpresence.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QUrl>
#include <QtEndian>

namespace {
const QString ApplicationId = "1560605777717108816";
const QString WebsiteUrl = "https://openkogama.org";
constexpr int PipeCount = 10;
constexpr int ConnectTimeoutMs = 200;
constexpr int HeaderSize = 8;
constexpr int TextLimit = 128;
constexpr int ButtonLimit = 32;

enum Opcode : quint32 { Handshake, Frame, Close, Ping, Pong };

struct Game
{
    QPointer<QProcess> client;
    QString details;
    QString state;
    qint64 start;
    QString join;
};

QList<Game> games;
QLocalSocket *socket = nullptr;
QByteArray received;
bool ready = false;
int nonce = 0;

void send(Opcode opcode, const QJsonObject &payload)
{
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray header(HeaderSize, Qt::Uninitialized);
    qToLittleEndian<quint32>(opcode, header.data());
    qToLittleEndian<quint32>(quint32(body.size()), header.data() + 4);
    socket->write(header + body);
}

void sendActivity()
{
    if (!socket || !ready || games.isEmpty()) return;
    const Game &game = games.last();
    const QJsonObject button = game.join.isEmpty()
        ? QJsonObject{{"label", QStringLiteral("Play OpenKogama").left(ButtonLimit)}, {"url", WebsiteUrl}}
        : QJsonObject{{"label", QStringLiteral("Join this server").left(ButtonLimit)},
            {"url", WebsiteUrl + "/join/?server=" + QString::fromLatin1(QUrl::toPercentEncoding(game.join))}};
    const QJsonObject activity{
        {"details", game.details.left(TextLimit)},
        {"state", game.state.left(TextLimit)},
        {"timestamps", QJsonObject{{"start", game.start}}},
        {"assets", QJsonObject{{"large_image", "logo"}, {"large_text", "OpenKogama"}}},
        {"buttons", QJsonArray{button}},
    };
    send(Frame, {
        {"cmd", "SET_ACTIVITY"},
        {"args", QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"activity", activity}}},
        {"nonce", QString::number(++nonce)},
    });
}

void closePipe()
{
    ready = false;
    received.clear();
    if (!socket) return;
    QLocalSocket *pipe = socket;
    socket = nullptr;
    pipe->disconnect();
    pipe->abort();
    pipe->deleteLater();
}

void receive()
{
    received += socket->readAll();
    while (received.size() >= HeaderSize) {
        const quint32 opcode = qFromLittleEndian<quint32>(received.constData());
        const qsizetype length = qFromLittleEndian<quint32>(received.constData() + 4);
        if (received.size() < HeaderSize + length) return;
        const QJsonObject message = QJsonDocument::fromJson(received.mid(HeaderSize, length)).object();
        received.remove(0, HeaderSize + length);
        if (opcode == Ping) {
            send(Pong, message);
        } else if (opcode == Close) {
            closePipe();
            return;
        } else if (opcode == Frame && message.value("evt").toString() == "READY") {
            ready = true;
            sendActivity();
        }
    }
}

void openPipe()
{
    auto *pipe = new QLocalSocket(qApp);
    for (int index = 0; index < PipeCount; ++index) {
        pipe->connectToServer(QString("discord-ipc-%1").arg(index));
        if (pipe->waitForConnected(ConnectTimeoutMs)) break;
        pipe->abort();
    }
    if (pipe->state() != QLocalSocket::ConnectedState) {
        pipe->deleteLater();
        return;
    }
    socket = pipe;
    QObject::connect(socket, &QLocalSocket::readyRead, socket, receive);
    QObject::connect(socket, &QLocalSocket::disconnected, socket, closePipe);
    send(Handshake, {{"v", 1}, {"client_id", ApplicationId}});
}
}

void DiscordPresence::show(QProcess *client, const QString &details, const QString &state)
{
    games.append({client, details, state, QDateTime::currentSecsSinceEpoch(), {}});
    QObject::connect(client, &QProcess::finished, client, refresh);
    refresh();
}

void DiscordPresence::join(QProcess *client, const QString &address, const QString &title)
{
    for (Game &game : games) {
        if (game.client != client) continue;
        game.join = address;
        if (!title.isEmpty()) game.details = QStringLiteral("Playing %1").arg(title);
    }
    refresh();
}

void DiscordPresence::refresh()
{
    games.removeIf([](const Game &game) { return !game.client || game.client->state() == QProcess::NotRunning; });
    if (games.isEmpty() || !Settings::discordPresence()) {
        closePipe();
        return;
    }
    if (socket) sendActivity();
    else openPipe();
}
