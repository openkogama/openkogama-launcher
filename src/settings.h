#pragma once

#include <QList>
#include <QByteArray>
#include <QString>
#include <QStringList>

struct GameServer
{
    QString name;
    QString address;
    QString title;
    QByteArray thumbnail;
    QStringList clients;

    bool accepts(const QString &version) const;
    static QString addressOf(const QString &text);
};

namespace Settings {

QList<GameServer> servers();
void setServers(const QList<GameServer> &servers);
QString publicList();
bool publicView();
void setPublicView(bool shown);
QString currentServer();
void setCurrentServer(const QString &address);

QString lastInstance();
void setLastInstance(const QString &path);

QString nickname();
void setNickname(const QString &name);

QString accountsUrl();
QString accountsSite();
QString accountVerifier();
void setAccountVerifier(const QString &verifier);
QString accountName();
QByteArray accountToken();
void setAccount(const QString &name, const QByteArray &token);
void forgetAccount();

QString rendezvous();
QString hostSecret();
int hostWorld();
void setHostWorld(int world);

bool consoleOnLaunch();
bool consoleOnCrash();
void setConsoleOnLaunch(bool enabled);
void setConsoleOnCrash(bool enabled);

bool discordPresence();
void setDiscordPresence(bool enabled);

bool clientPatch(const QString &id);
void setClientPatch(const QString &id, bool enabled);

QString language();
void setLanguage(const QString &code);

}
