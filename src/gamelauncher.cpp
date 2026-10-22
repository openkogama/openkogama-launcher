#include "gamelauncher.h"
#include "account.h"
#include "clientpatches.h"
#include "serverfeatures.h"
#include "launchdialog.h"
#include "paths.h"
#include "playerprofile.h"
#include "webplayerruntime.h"
#include "consolewindow.h"
#include "settings.h"
#include "discordpresence.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWidget>
#include <utility>

namespace {

constexpr int NameTimeout = 3000;
constexpr int MaxNickname = 20;
constexpr qint64 PendingLifeMs = 10 * 60 * 1000;
std::function<void()> pendingJoin;
qint64 pendingSince = 0;

bool nameClaimed(const QString &url, const QString &name)
{
    if (name.isEmpty()) return false;
    QNetworkAccessManager net;
    QNetworkRequest request(QUrl(url + "/api/name?name=" + QString::fromLatin1(QUrl::toPercentEncoding(name))));
    request.setTransferTimeout(NameTimeout);
    QNetworkReply *reply = net.get(request);
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    reply->deleteLater();
    return reply->error() == QNetworkReply::NoError && QJsonDocument::fromJson(reply->readAll()).object().value("claimed").toBool();
}

void signInThenJoin(const std::function<void()> &retry)
{
    pendingJoin = retry;
    pendingSince = QDateTime::currentMSecsSinceEpoch();
    QDesktopServices::openUrl(Account::signInPage());
}

QPushButton *primary(QMessageBox &box, const QString &text)
{
    QPushButton *button = box.addButton(text, QMessageBox::AcceptRole);
    button->setProperty("primary", true);
    button->style()->polish(button);
    box.setDefaultButton(button);
    return button;
}

void askToSignIn(QWidget *parent, const std::function<void()> &retry)
{
    QMessageBox box(QMessageBox::Information, QCoreApplication::translate("GameLauncher", "Sign in to play"),
        QCoreApplication::translate("GameLauncher",
            "This server only lets in players with an OpenKogama account.\n\n"
            "Sign in or create a free account in your browser. The game starts by itself after that."),
        QMessageBox::Cancel, parent);
    QPushButton *signIn = primary(box, QCoreApplication::translate("GameLauncher", "Sign in"));
    box.exec();
    if (box.clickedButton() == signIn) signInThenJoin(retry);
}

void askAboutName(QWidget *parent, const QString &name, const std::function<void()> &retry)
{
    QMessageBox box(QMessageBox::Information, QCoreApplication::translate("GameLauncher", "Name taken"),
        QCoreApplication::translate("GameLauncher",
            "The name \"%1\" belongs to an OpenKogama account on this server.\n\n"
            "If it is yours, sign in. Otherwise pick another nickname.").arg(name),
        QMessageBox::Cancel, parent);
    QPushButton *signIn = primary(box, QCoreApplication::translate("GameLauncher", "Sign in"));
    QPushButton *rename = box.addButton(QCoreApplication::translate("GameLauncher", "Change nickname"), QMessageBox::ActionRole);
    box.exec();
    if (box.clickedButton() == signIn) {
        signInThenJoin(retry);
        return;
    }
    if (box.clickedButton() != rename) return;
    bool ok = false;
    const QString picked = QInputDialog::getText(parent, QCoreApplication::translate("GameLauncher", "Change nickname"),
        QCoreApplication::translate("GameLauncher", "Pick a nickname that other players will see:"), QLineEdit::Normal, name, &ok).trimmed();
    if (!ok || picked.isEmpty()) return;
    Settings::setNickname(picked.left(MaxNickname));
    if (retry) QTimer::singleShot(0, parent, retry);
}

}

static QString instanceTitle(const QString &path)
{
    QFile file(QDir(path).filePath("instance.json"));
    QJsonObject info;
    if (file.open(QIODevice::ReadOnly))
        info = QJsonDocument::fromJson(file.readAll()).object();
    for (const char *key : {"name", "version"}) {
        QString value = info.value(key).toString().trimmed();
        if (!value.isEmpty())
            return QCoreApplication::translate("GameLauncher", "KoGaMa %1").arg(value);
    }
    return "KoGaMa";
}

static QString presenceDetails(const QString &url, const QString &mode)
{
    if (mode == "avatar")
        return QCoreApplication::translate("GameLauncher", "Editing an avatar");
    if (mode != "play")
        return QCoreApplication::translate("GameLauncher", "Building a world");
    if (url == LocalServerUrl)
        return QCoreApplication::translate("GameLauncher", "Playing a local world");
    const QList<GameServer> servers = Settings::servers();
    for (const GameServer &server : servers)
        if (server.address == url && !server.title.isEmpty())
            return QCoreApplication::translate("GameLauncher", "Playing %1").arg(server.title);
    return QCoreApplication::translate("GameLauncher", "Playing online");
}

bool GameLauncher::resumeJoin()
{
    if (!pendingJoin) return false;
    const std::function<void()> join = std::exchange(pendingJoin, nullptr);
    if (QDateTime::currentMSecsSinceEpoch() - pendingSince > PendingLifeMs) return false;
    QTimer::singleShot(0, QCoreApplication::instance(), join);
    return true;
}

QProcess *GameLauncher::start(const QString &path, const QString &exe, const QString &url, const QString &mode, int world, QWidget *owner, QWidget *parent, bool viaCode, const std::function<void()> &retry) {
    const QString title = instanceTitle(path);
    const bool local = url == LocalServerUrl;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (local) {
        env.insert("http_proxy", "http://127.0.0.1:8081");
        env.insert("no_proxy", "127.0.0.1,localhost");
    }

    QString session = url + "/session?mode=" + mode + (world ? "&world=" + QString::number(world) : QString());
    const QString nickname = Settings::nickname();
    if (!nickname.isEmpty())
        session += "&name=" + QString::fromLatin1(QUrl::toPercentEncoding(nickname));
    if (viaCode)
        session += "&local=" + QString::number(LocalHttpPort);
    const QString settingsKey = QUuid::createUuid().toString(QUuid::Id128);
    session += "&key=" + settingsKey;

    const std::optional<QStringList> features = ServerFeatures::fetch(url);
    QStringList notes;
    if (!local && !viaCode && features && features->contains("accounts")) {
        const bool required = features->contains("accounts-required");
        bool signedIn = false;
        if (!Account::name().isEmpty()) {
            const Account::Reply ticket = Account::ticket(url);
            if (ticket.ok()) {
                session += "&ticket=" + QString::fromLatin1(QUrl::toPercentEncoding(ticket.body.value("ticket").toString()));
                notes << QCoreApplication::translate("GameLauncher", "signed in as %1").arg(ticket.body.value("name").toString());
                signedIn = true;
            } else if (!Account::name().isEmpty()) {
                if (required || nameClaimed(url, nickname)) {
                    QMessageBox::warning(parent, QCoreApplication::translate("GameLauncher", "Sign in"),
                        QCoreApplication::translate("GameLauncher", "Could not sign in to this server. %1").arg(ticket.error()));
                    return nullptr;
                }
                notes << QCoreApplication::translate("GameLauncher", "could not sign in here, playing as a guest: %1").arg(ticket.error());
            }
        }
        if (!signedIn && Account::name().isEmpty()) {
            if (required) {
                askToSignIn(parent, retry);
                return nullptr;
            }
            if (nameClaimed(url, nickname)) {
                askAboutName(parent, nickname, retry);
                return nullptr;
            }
        }
    }

    auto *client = new QProcess(owner);
    QDir logs(QDir(path).filePath("logs"));
    logs.mkpath(".");
    QString log = logs.filePath("client-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".log");

    if (WebPlayerRuntime::isWebPlayerFile(exe)) {
        QFile meta(QDir(path).filePath("instance.json"));
        QJsonObject info;
        if (meta.open(QIODevice::ReadOnly)) info = QJsonDocument::fromJson(meta.readAll()).object();
        QString version = info.value("version").toString();
        QString file = QDir::toNativeSeparators(QDir(path).filePath(exe));
        client->setProgram(Paths::player());
        if (info.value("unityVersion").toString().startsWith("3.4")) {
            if (mode == "avatar") {
                client->deleteLater();
                QMessageBox::information(parent, QCoreApplication::translate("GameLauncher", "Worlds"),
                    QCoreApplication::translate("GameLauncher", "This version has no avatar editor"));
                return nullptr;
            }
            const QString username = viaCode && !nickname.isEmpty() ? QString::fromLatin1(QUrl::toPercentEncoding(nickname)) : "1";
            QString query = "?Username=" + username + "&Password=&PlanetName=" + QString::number(world) + "&EditMode=" + (mode == "play" ? "false" : "true");
            client->setArguments({file + query, "--serve-as", url + "/kogama2012/WebPlayer.unity3d",
                "--title", title, "--log", QDir::toNativeSeparators(log)});
        } else {
            client->setArguments({file, "--version", version,
                "--reply", "sendPlayerParams=" + session + "&client=" + version, "--title", title, "--log", QDir::toNativeSeparators(log)});
        }
    } else {
        client->setProgram(QDir(path).filePath(exe));
        QFile meta(QDir(path).filePath("instance.json"));
        QJsonObject info;
        if (meta.open(QIODevice::ReadOnly)) info = QJsonDocument::fromJson(meta.readAll()).object();
        QString version = info.value("version").toString();
        if (!version.isEmpty())
            session += "&client=" + version;
        QString unity = info.value("unityVersion").toString();
        if (!unity.isEmpty())
            session += "&unity=" + unity;
        client->setArguments({"kogamaPackage:" + QString::fromLatin1(session.toUtf8().toBase64()), "-logFile", QDir::toNativeSeparators(log)});
        notes += ClientPatches::apply(path, url, version, features);
    }
    client->setWorkingDirectory(path);
    client->setProcessEnvironment(env);

    auto *console = new ConsoleWindow(QCoreApplication::translate("GameLauncher", "Console - %1").arg(title));
    console->setAttribute(Qt::WA_DeleteOnClose);
    console->setAttribute(Qt::WA_QuitOnClose, false);
    console->follow(QCoreApplication::translate("GameLauncher", "Game"), log);
    for (const QString &note : notes)
        console->note(note);
    if (WebPlayerRuntime::isWebPlayerFile(exe))
        console->followNewest("Unity", QDir::tempPath() + "/UnityWebPlayer/log");
    if (local)
        console->followNewest(QCoreApplication::translate("GameLauncher", "Server"), Paths::serverData() + "/logs", true);
    QObject::connect(console, &ConsoleWindow::crashed, console, [console]() {
        if (!Settings::consoleOnCrash())
            return;
        console->show();
        console->raise();
    });
    QObject::connect(client, &QProcess::finished, console, [console](int exitCode, QProcess::ExitStatus status) {
        console->gameFinished(exitCode, status);
        if (!console->isVisible())
            console->deleteLater();
    });
    QObject::connect(client, &QProcess::finished, client, [url, settingsKey, local, owner]() { PlayerProfile::keep(url, settingsKey, local, owner); });
    QObject::connect(client, &QProcess::finished, client, &QObject::deleteLater);
    if (const QString sent = PlayerProfile::send(url, settingsKey, !local, features); !sent.isEmpty())
        console->note(sent);
    client->start();

    if (!client->waitForStarted()) {
        client->deleteLater();
        console->deleteLater();
        QMessageBox::warning(parent, QCoreApplication::translate("GameLauncher", "Worlds"),
            QCoreApplication::translate("GameLauncher", "Could not start %1").arg(exe));
        return nullptr;
    }
    DiscordPresence::show(client, presenceDetails(url, mode), title);
    if (Settings::consoleOnLaunch())
        console->show();
    return client;
}
