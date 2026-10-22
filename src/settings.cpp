#include "settings.h"
#include "paths.h"
#include "tunnel.h"

#include <QRandomGenerator>
#include <QSettings>
#include <QUrl>
#include <QVersionNumber>
#include <algorithm>

namespace {

QSettings store()
{
    return QSettings(Paths::data() + "/settings.ini", QSettings::IniFormat);
}

}

namespace Settings {

QList<GameServer> servers()
{
    QSettings settings = store();
    QList<GameServer> list;
    int count = settings.beginReadArray("servers");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        const QString address = settings.value("address").toString().trimmed();
        if (Tunnel::isAddress(address)) {
            list.append({settings.value("name").toString(), Tunnel::address(Tunnel::code(address)),
                settings.value("title").toString(), settings.value("thumbnail").toByteArray(), settings.value("clients").toStringList()});
            continue;
        }
        QUrl url(address.contains("://") ? address : "http://" + address);
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")
            || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || url.port() == 0
            || (!url.path().isEmpty() && url.path() != "/")) continue;
        if (!address.contains("://") && url.port() == -1) url.setPort(8080);
        if (url.path() == "/") url.setPath(QString());
        list.append({settings.value("name").toString(), url.toString(QUrl::StripTrailingSlash),
            settings.value("title").toString(), settings.value("thumbnail").toByteArray(), settings.value("clients").toStringList()});
    }
    settings.endArray();
    return list;
}

void setServers(const QList<GameServer> &servers)
{
    QSettings settings = store();
    settings.remove("servers");
    settings.beginWriteArray("servers", servers.size());
    for (int i = 0; i < servers.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", servers[i].name);
        settings.setValue("address", servers[i].address);
        settings.setValue("title", servers[i].title);
        settings.setValue("thumbnail", servers[i].thumbnail);
        settings.setValue("clients", servers[i].clients);
    }
    settings.endArray();
}

QString publicList()
{
    return store().value("launcher/serverList", "https://list.openkogama.org").toString().trimmed();
}

bool publicView()
{
    return store().value("launcher/publicServers", true).toBool();
}

void setPublicView(bool shown)
{
    store().setValue("launcher/publicServers", shown);
}

QString currentServer()
{
    return store().value("launcher/server").toString();
}

void setCurrentServer(const QString &address)
{
    store().setValue("launcher/server", address);
}

QString lastInstance()
{
    return store().value("launcher/lastInstance").toString();
}

void setLastInstance(const QString &path)
{
    store().setValue("launcher/lastInstance", path);
}

QString nickname()
{
    return store().value("player/nickname").toString();
}

void setNickname(const QString &name)
{
    store().setValue("player/nickname", name);
}

QString accountsUrl()
{
    QString url = store().value("account/url", "https://accounts.openkogama.org").toString().trimmed();
    while (url.endsWith('/')) url.chop(1);
    return url;
}

QString accountsSite()
{
    return store().value("account/site", "https://openkogama.org/account/").toString().trimmed();
}

QString accountVerifier()
{
    return store().value("account/verifier").toString();
}

void setAccountVerifier(const QString &verifier)
{
    if (verifier.isEmpty()) store().remove("account/verifier");
    else store().setValue("account/verifier", verifier);
}

QString accountName()
{
    return store().value("account/name").toString();
}

QByteArray accountToken()
{
    return store().value("account/token").toByteArray();
}

void setAccount(const QString &name, const QByteArray &token)
{
    QSettings settings = store();
    settings.setValue("account/name", name);
    settings.setValue("account/token", token);
}

void forgetAccount()
{
    QSettings settings = store();
    settings.remove("account/name");
    settings.remove("account/token");
}

QString rendezvous()
{
    return store().value("p2p/rendezvous").toString();
}

QString hostSecret()
{
    QSettings settings = store();
    QString secret = settings.value("p2p/secret").toString();
    if (secret.size() >= 32) return secret;
    const QString letters = "abcdefghijklmnopqrstuvwxyz0123456789";
    secret.clear();
    for (int i = 0; i < 32; ++i)
        secret += letters[QRandomGenerator::system()->bounded(int(letters.size()))];
    settings.setValue("p2p/secret", secret);
    return secret;
}

int hostWorld()
{
    return store().value("p2p/world").toInt();
}

void setHostWorld(int world)
{
    store().setValue("p2p/world", world);
}

bool consoleOnLaunch()
{
    return store().value("console/showOnLaunch", false).toBool();
}

bool consoleOnCrash()
{
    return store().value("console/showOnCrash", true).toBool();
}

void setConsoleOnLaunch(bool enabled)
{
    store().setValue("console/showOnLaunch", enabled);
}

void setConsoleOnCrash(bool enabled)
{
    store().setValue("console/showOnCrash", enabled);
}

bool discordPresence()
{
    return store().value("discord/presence", true).toBool();
}

void setDiscordPresence(bool enabled)
{
    store().setValue("discord/presence", enabled);
}

bool clientPatch(const QString &id)
{
    return store().value("clientPatches/" + id, true).toBool();
}

void setClientPatch(const QString &id, bool enabled)
{
    store().setValue("clientPatches/" + id, enabled);
}

QString language()
{
    return store().value("general/language").toString();
}

void setLanguage(const QString &code)
{
    store().setValue("general/language", code);
}

}

QString GameServer::addressOf(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (Tunnel::isAddress(trimmed)) return Tunnel::address(Tunnel::code(trimmed));
    const QUrl url(trimmed);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() || url.port() == 0
        || (!url.path().isEmpty() && url.path() != "/"))
        return {};
    return url.toString(QUrl::RemovePath | QUrl::StripTrailingSlash);
}

bool GameServer::accepts(const QString &version) const
{
    if (clients.isEmpty()) return true;
    const QVersionNumber number = QVersionNumber::fromString(version);
    for (const QString &entry : clients) {
        const QStringList range = entry.split('-');
        const QVersionNumber low = QVersionNumber::fromString(range.value(0).trimmed());
        const QVersionNumber high = QVersionNumber::fromString(range.value(1).trimmed());
        if (range.size() == 2 && !low.isNull() && !high.isNull()) {
            if (!number.isNull() && low <= number && number <= high) return true;
        } else if (entry.trimmed() == version) {
            return true;
        }
    }
    return false;
}
