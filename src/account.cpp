#include "account.h"
#include "settings.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QUrlQuery>
#include <windows.h>
#include <dpapi.h>

namespace {

constexpr int Timeout = 8000;
constexpr int Unauthorized = 401;
constexpr int VerifierBytes = 32;
const auto Base64Url = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;

QByteArray protect(const QByteArray &plain)
{
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    const QByteArray sealed(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return sealed;
}

QByteArray unprotect(const QByteArray &sealed)
{
    if (sealed.isEmpty()) return {};
    DATA_BLOB in{DWORD(sealed.size()), reinterpret_cast<BYTE *>(const_cast<char *>(sealed.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    const QByteArray plain(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return plain;
}

QString token()
{
    return QString::fromUtf8(unprotect(Settings::accountToken()));
}

Account::Reply post(const QString &path, const QJsonObject &body)
{
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QNetworkAccessManager net;
    QNetworkRequest request(QUrl(Settings::accountsUrl() + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(Timeout);
    QNetworkReply *reply = net.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop wait;
    QObject::connect(reply, &QNetworkReply::finished, &wait, &QEventLoop::quit);
    wait.exec(QEventLoop::ExcludeUserInputEvents);
    reply->deleteLater();
    QGuiApplication::restoreOverrideCursor();
    return {reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), QJsonDocument::fromJson(reply->readAll()).object()};
}

void forget()
{
    Settings::forgetAccount();
    emit Account::events()->changed();
}

}

namespace Account {

QString Reply::error() const
{
    if (status == 0) return QCoreApplication::translate("Account", "Could not reach OpenKogama accounts, check your connection.");
    QString message = body.value("error").toString();
    if (message.isEmpty()) return QCoreApplication::translate("Account", "OpenKogama accounts answered with error %1, try again later.").arg(status);
    message[0] = message[0].toUpper();
    return message + ".";
}

AccountEvents *events()
{
    static AccountEvents instance;
    return &instance;
}

QString name()
{
    return token().isEmpty() ? QString() : Settings::accountName();
}

QUrl signInPage()
{
    QByteArray random(VerifierBytes, Qt::Uninitialized);
    QRandomGenerator::system()->generate(random.begin(), random.end());
    const QByteArray verifier = random.toBase64(Base64Url);
    Settings::setAccountVerifier(QString::fromLatin1(verifier));
    QUrl page(Settings::accountsSite());
    page.setQuery(QUrlQuery{{"launcher", QString::fromLatin1(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256).toBase64(Base64Url))}});
    return page;
}

Reply finishSignIn(const QString &code)
{
    const QString verifier = Settings::accountVerifier();
    if (verifier.isEmpty()) return {Unauthorized, {{"error", QCoreApplication::translate("Account", "start signing in from the launcher")}}};
    const Reply reply = post("/launcher/token", {{"code", code}, {"verifier", verifier}});
    const QString session = reply.body.value("token").toString();
    if (!reply.ok() || session.isEmpty()) return reply;
    Settings::setAccountVerifier({});
    Settings::setAccount(reply.body.value("name").toString(), protect(session.toUtf8()));
    emit events()->changed();
    return reply;
}

Reply ticket(const QString &server)
{
    const Reply reply = post("/ticket", {{"token", token()}, {"server", server}});
    if (reply.status == Unauthorized) forget();
    return reply;
}

void signOut()
{
    const QString session = token();
    forget();
    if (!session.isEmpty()) post("/logout", {{"token", session}});
}

}
