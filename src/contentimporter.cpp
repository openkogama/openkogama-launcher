#include "contentimporter.h"
#include "paths.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace {

constexpr int RequestTimeoutMs = 60000;
const char *BoneNames[] = {"Head", "Torso", "LArm", "RArm", "LUpLeg", "LLowLeg", "RUpLeg", "RLowLeg"};

bool readBig32(const QByteArray &data, int at, quint32 *value)
{
    if (at < 0 || at + 4 > data.size()) return false;
    *value = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData()) + at);
    return true;
}

bool readBig16(const QByteArray &data, int at, quint16 *value)
{
    if (at < 0 || at + 2 > data.size()) return false;
    *value = qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData()) + at);
    return true;
}

bool readBigInt(const QByteArray &data, int at, qint32 *value)
{
    quint32 bits = 0;
    if (!readBig32(data, at, &bits)) return false;
    *value = static_cast<qint32>(bits);
    return true;
}

bool readBigFloat(const QByteArray &data, int at, float *value)
{
    quint32 bits = 0;
    if (!readBig32(data, at, &bits)) return false;
    std::memcpy(value, &bits, sizeof bits);
    return true;
}

double rounded(float value)
{
    return std::round(static_cast<double>(value) * 10000.0) / 10000.0;
}

// Model and avatar pack entries are zlib streams; leave anything else untouched.
QByteArray inflateZlib(const QByteArray &data)
{
    if (data.size() < 4) return data;
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    if ((bytes[0] & 0x0f) != 0x08 || ((bytes[0] << 8) | bytes[1]) % 31 != 0) return data;

    QByteArray wrapped;
    wrapped.resize(4);
    const quint32 hint = quint32(qMin<qsizetype>(data.size(), 0x7fffffff));
    qToBigEndian<quint32>(hint, reinterpret_cast<uchar *>(wrapped.data()));
    wrapped.append(data);
    return qUncompress(wrapped);
}

QString displayName(const ImportRequest &request)
{
    return request.name.isEmpty() ? QStringLiteral("this item") : QStringLiteral("\"%1\"").arg(request.name);
}

}

ContentImporter::ContentImporter(const QString &clientVersion, QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_clientVersion(clientVersion)
{
}

void ContentImporter::start(const ImportRequest &request)
{
    m_request = request;
    m_urlIndex = 0;
    m_cancelled = false;
    m_finished = false;
    m_retriedScale = false;
    m_lastError.clear();
    fetch();
}

void ContentImporter::cancel()
{
    if (m_finished) return;
    m_cancelled = true;
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    finish(false, {}, 0);
}

void ContentImporter::fetch()
{
    if (m_cancelled || m_finished) return;
    if (m_urlIndex >= m_request.urls.size()) {
        const QString error = m_lastError.isEmpty() ? tr("Could not download %1.").arg(displayName(m_request))
                                                    : tr("Could not download %1. %2").arg(displayName(m_request), m_lastError);
        finish(false, error, 0);
        return;
    }

    QNetworkRequest request(m_request.urls.at(m_urlIndex));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(10);
    request.setTransferTimeout(RequestTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, "OpenKogama Launcher");
    request.setRawHeader("Accept-Encoding", "identity");
    if (m_request.offset >= 0 && m_request.length > 0) {
        const qint64 end = m_request.offset + m_request.length - 1;
        request.setRawHeader("Range", "bytes=" + QByteArray::number(m_request.offset) + "-" + QByteArray::number(end));
    }

    m_reply = m_net->get(request);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &ContentImporter::progress);
    connect(m_reply, &QNetworkReply::finished, this, &ContentImporter::downloaded);
}

void ContentImporter::downloaded()
{
    if (m_finished || m_cancelled) return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) return;
    reply->deleteLater();

    QByteArray data = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        m_lastError = reply->errorString();
        m_urlIndex++;
        fetch();
        return;
    }

    if (m_request.offset >= 0 && m_request.length > 0) {
        if (data.size() > m_request.length)
            data = data.mid(qsizetype(m_request.offset), qsizetype(m_request.length));
        if (data.size() != m_request.length) {
            m_lastError = tr("The server sent an unexpected amount of data.");
            m_urlIndex++;
            fetch();
            return;
        }
    }

    if (data.isEmpty()) {
        m_lastError = tr("The file is empty.");
        m_urlIndex++;
        fetch();
        return;
    }

    if (m_request.kind != "map") {
        const QByteArray unpacked = inflateZlib(data);
        if (unpacked.isEmpty()) {
            m_lastError = tr("The downloaded file could not be unpacked.");
            m_urlIndex++;
            fetch();
            return;
        }
        data = unpacked;
    }

    if (!m_request.sha256.isEmpty()) {
        const QString actual = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        if (actual != m_request.sha256) {
            m_lastError = tr("The downloaded file does not match the archive.");
            m_urlIndex++;
            fetch();
            return;
        }
    }

    m_payload = data;
    if (m_request.kind == "avatar") {
        m_payload = avatarJson(data);
        if (m_payload.isEmpty()) {
            m_lastError = tr("This avatar file could not be read.");
            m_urlIndex++;
            fetch();
            return;
        }
    }
    upload();
}

void ContentImporter::upload()
{
    if (m_cancelled || m_finished) return;

    QUrl url;
    QUrlQuery query;
    if (m_request.kind == "map") {
        url = QUrl(LocalServerUrl + "/api/worlds/import");
        query.addQueryItem("name", m_request.name);
        if (!m_clientVersion.isEmpty()) query.addQueryItem("client", m_clientVersion);
    } else if (m_request.kind == "avatar") {
        url = QUrl(LocalServerUrl + "/api/avatars/import");
    } else {
        url = QUrl(LocalServerUrl + "/api/inventory/import");
        query.addQueryItem("name", m_request.name);
        if (!m_request.scale.isEmpty()) query.addQueryItem("scale", m_request.scale);
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setTransferTimeout(RequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
        m_request.kind == "avatar" ? "application/json" : "application/octet-stream");
    m_reply = m_net->post(request, m_payload);
    connect(m_reply, &QNetworkReply::finished, this, &ContentImporter::uploaded);
}

void ContentImporter::uploaded()
{
    if (m_finished || m_cancelled) return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply) return;
    reply->deleteLater();

    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        finish(false, tr("Could not add %1 to the game. %2").arg(displayName(m_request), reply->errorString()), 0);
        return;
    }

    const QJsonObject result = QJsonDocument::fromJson(body).object();
    const int id = result.value("id").toInt();
    if (id <= 0) {
        if (m_request.kind == "model" && result.value("needsScale").toBool() && !m_retriedScale) {
            m_retriedScale = true;
            if (m_request.scale.isEmpty()) m_request.scale = "0.25";
            upload();
            return;
        }
        finish(false, tr("Could not add %1 to the game.").arg(displayName(m_request)), 0);
        return;
    }

    emit progress(1, 1);
    finish(true, {}, id);
}

void ContentImporter::finish(bool ok, const QString &error, int id)
{
    if (m_finished) return;
    m_finished = true;
    emit finished(ok, error, id);
}

QByteArray ContentImporter::avatarJson(const QByteArray &data) const
{
    const int size = data.size();
    if (size < 5) return {};
    const uchar *bytes = reinterpret_cast<const uchar *>(data.constData());
    const int count = bytes[4];
    int pos = 5;
    QJsonArray parts;

    for (int i = 0; i < count; i++) {
        if (pos >= size) return {};
        const bool present = bytes[pos++] != 0;
        if (!present) continue;
        if (pos + 36 > size) return {};
        float scale = 0;
        quint32 cubesSize = 0;
        if (!readBigFloat(data, pos + 28, &scale) || !readBig32(data, pos + 32, &cubesSize)) return {};
        pos += 36;
        if (cubesSize > quint32(size - pos)) return {};
        qint32 first = 0;
        if (cubesSize >= 4 && readBigInt(data, pos, &first) && first > 0) {
            QJsonObject part;
            part.insert("bone", i < 8 ? QString::fromLatin1(BoneNames[i]) : QStringLiteral("Part%1").arg(i));
            part.insert("scale", rounded(scale));
            part.insert("cubes", QString::fromLatin1(data.mid(pos, int(cubesSize)).toBase64()));
            parts.append(part);
        }
        pos += int(cubesSize);
    }

    if (pos >= size) return {};
    const int accessoriesCount = bytes[pos++];
    QJsonArray accessories;
    for (int i = 0; i < accessoriesCount; i++) {
        if (pos + 15 > size) return {};
        quint16 extra = 0;
        if (!readBig16(data, pos + 13, &extra)) return {};
        if (pos + 15 + extra > size) return {};
        qint32 itemId = 0;
        float offset = 0, scale = 0;
        if (!readBigInt(data, pos + 1, &itemId) || !readBigFloat(data, pos + 5, &offset)
            || !readBigFloat(data, pos + 9, &scale)) return {};
        QJsonObject accessory;
        accessory.insert("item", int(itemId));
        accessory.insert("slot", int(bytes[pos]));
        accessory.insert("offset", rounded(offset));
        accessory.insert("scale", rounded(scale));
        accessories.append(accessory);
        pos += 15 + int(extra);
    }

    QJsonObject root;
    root.insert("parts", parts);
    root.insert("accessories", accessories);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}
