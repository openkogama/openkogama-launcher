#include "sharebar.h"
#include "launchdialog.h"
#include "serverconfigdialog.h"
#include "settings.h"
#include "tilegrid.h"
#include "tunnel.h"
#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int SlowMs = 10000;
constexpr int CopiedMs = 1500;
constexpr int Gap = 16;
constexpr int UploadMs = 5 * 60 * 1000;
constexpr int RetryMs = 5000;
constexpr int UploadTries = 3;
constexpr int JpegLimit = 64 * 1024;
}

ShareBar::ShareBar(QWidget *parent)
    : QFrame(parent), m_text(new QLabel(this)), m_code(new QLabel(this)), m_info(new QLabel(this)),
      m_copy(new QPushButton(tr("Copy code"), this)), m_settings(new QPushButton(tr("Server settings"), this)),
      m_stop(new QPushButton(this)), m_slow(new QTimer(this)), m_upload(new QTimer(this))
{
    m_upload->setInterval(UploadMs);
    connect(m_upload, &QTimer::timeout, this, [this]() { sendThumbnail(1); });
    setObjectName("shareBar");
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 3, 4, 3);
    layout->setSpacing(8);
    m_text->setObjectName("shareText");
    m_code->setObjectName("shareCode");
    m_info->setObjectName("shareInfo");
    for (QLabel *label : {m_text, m_code, m_info}) {
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    m_code->setContentsMargins(Gap, 0, 0, 0);
    m_info->setContentsMargins(Gap, 0, 0, 0);
    m_copy->setObjectName("shareCopyButton");
    m_settings->setObjectName("shareSettingsButton");
    m_stop->setObjectName("shareStopButton");
    layout->addWidget(m_text);
    layout->addWidget(m_code);
    layout->addWidget(m_info);
    layout->addStretch(1);
    layout->addWidget(m_copy);
    layout->addWidget(m_settings);
    layout->addWidget(m_stop);
    m_slow->setSingleShot(true);
    m_slow->setInterval(SlowMs);
    connect(m_slow, &QTimer::timeout, this, [this]() {
        m_waiting = true;
        refresh();
    });
    connect(m_copy, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(Tunnel::display(m_codeText));
        m_copy->setText(tr("Copied"));
        QTimer::singleShot(CopiedMs, m_copy, [this]() { m_copy->setText(tr("Copy code")); });
    });
    connect(m_settings, &QPushButton::clicked, this, &ShareBar::settingsRequested);
    connect(m_stop, &QPushButton::clicked, this, &ShareBar::stop);
    hide();
}

void ShareBar::share(int world, const QString &name, const QString &version)
{
    if (m_tunnel) {
        Tunnel *old = m_tunnel;
        m_tunnel = nullptr;
        old->stop();
        old->deleteLater();
    }
    m_world = world;
    m_name = name;
    m_version = version;
    m_codeText.clear();
    m_error.clear();
    m_players = 0;
    start();
    refresh();
    show();
    emit hostedWorldChanged(m_world);
}

void ShareBar::setVersion(const QString &version)
{
    if (version != m_version && m_tunnel) share(m_world, m_name, version);
}

void ShareBar::restart()
{
    if (m_tunnel) share(m_world, m_name, m_version);
}

void ShareBar::stop()
{
    if (m_tunnel && m_listed) {
        if (!m_net) m_net = new QNetworkAccessManager(this);
        QNetworkRequest request{QUrl(Settings::publicList() + "/announce")};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(8000);
        const QJsonObject goodbye{{"secret", Settings::hostSecret()}, {"left", true}};
        QNetworkReply *reply = m_net->post(request, QJsonDocument(goodbye).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_listed = false;
    m_upload->stop();
    if (m_tunnel) {
        Tunnel *tunnel = m_tunnel;
        m_tunnel = nullptr;
        tunnel->stop();
        tunnel->deleteLater();
    }
    m_slow->stop();
    m_codeText.clear();
    m_error.clear();
    m_players = 0;
    hide();
    emit hostedWorldChanged(0);
}

void ShareBar::start()
{
    const QJsonObject config = ServerConfigDialog::current();
    QString title = config.value("title").toString().trimmed();
    if (title.isEmpty()) title = m_name;
    m_title = title;
    m_listed = config.value("public").toBool();
    QJsonArray clients = config.value("clients").toArray();
    if (clients.isEmpty() && !config.value("mixedClients").toBool()) clients.append(m_version);
    const QJsonObject info{{"title", title}, {"version", clients.size() == 1 ? clients.first().toString() : QString()},
        {"clients", clients}, {"world", m_world}};
    Tunnel *tunnel = Tunnel::host(Settings::hostSecret(), QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Compact)),
        m_listed, this);
    m_tunnel = tunnel;
    connect(tunnel, &Tunnel::said, this, [this, tunnel](const QString &word, const QString &rest) {
        if (tunnel == m_tunnel) heard(word, rest);
    });
    connect(tunnel, &Tunnel::ended, this, [this, tunnel]() {
        if (tunnel == m_tunnel) ended();
    });
    m_waiting = false;
    m_slow->start();
}

void ShareBar::heard(const QString &word, const QString &rest)
{
    if (word == "code") {
        m_codeText = Tunnel::normalize(rest);
        m_slow->stop();
        if (m_listed && !m_codeText.isEmpty()) {
            emit listed(Tunnel::address(m_codeText), m_title);
            sendThumbnail(UploadTries);
            m_upload->start();
        }
    } else if (word == "players") {
        m_players = rest.toInt();
    } else if (word == "error") {
        m_error = rest.startsWith("no server") ? tr("your local server is not running, open the Worlds tab first")
            : rest.startsWith("rendezvous") ? tr("the OpenKogama P2P servers can't be reached") : rest;
    }
    refresh();
}

void ShareBar::sendThumbnail(int tries)
{
    if (!m_net) m_net = new QNetworkAccessManager(this);
    QNetworkRequest local(QUrl(LocalServerUrl + "/api/server/thumbnail?world=" + QString::number(m_world)));
    local.setTransferTimeout(8000);
    QNetworkReply *image = m_net->get(local);
    connect(image, &QNetworkReply::finished, this, [this, image, tries]() {
        image->deleteLater();
        const QByteArray jpeg = image->error() == QNetworkReply::NoError ? smallJpeg(image->readAll()) : QByteArray();
        if (jpeg.isEmpty() || !m_listed || !m_tunnel) return;
        QNetworkRequest request{QUrl(Settings::publicList() + "/thumbnail")};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(8000);
        const QJsonObject body{{"secret", Settings::hostSecret()}, {"image", QString::fromLatin1(jpeg.toBase64())}};
        QNetworkReply *reply = m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [this, reply, tries]() {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 404 && tries > 1) QTimer::singleShot(RetryMs, this, [this, tries]() { sendThumbnail(tries - 1); });
        });
    });
}

QByteArray ShareBar::smallJpeg(const QByteArray &bytes)
{
    QImage image;
    if (bytes.isEmpty() || !image.loadFromData(bytes)) return {};
    const QImage cover = image.scaled(TileGrid::WideImage, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QImage tile = cover.copy((cover.width() - TileGrid::WideImage.width()) / 2, (cover.height() - TileGrid::WideImage.height()) / 2,
        TileGrid::WideImage.width(), TileGrid::WideImage.height());
    for (int quality : {80, 60, 40}) {
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        buffer.open(QIODevice::WriteOnly);
        tile.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPG", quality);
        if (!jpeg.isEmpty() && jpeg.size() <= JpegLimit) return jpeg;
    }
    return {};
}

void ShareBar::ended()
{
    Tunnel *tunnel = m_tunnel;
    m_tunnel = nullptr;
    tunnel->deleteLater();
    m_slow->stop();
    m_codeText.clear();
    m_players = 0;
    if (m_error.isEmpty()) m_error = tr("hosting stopped unexpectedly");
    refresh();
    emit hostedWorldChanged(0);
}

void ShareBar::refresh()
{
    const bool live = m_tunnel && !m_codeText.isEmpty();
    const bool failed = !m_tunnel && !m_error.isEmpty();
    m_text->setText(failed ? tr("Could not host %1: %2").arg(m_name, m_error) : tr("Hosting %1").arg(m_name));
    m_code->setText(tr("Code %1").arg(Tunnel::display(m_codeText)));
    m_code->setVisible(live);
    if (live) m_info->setText(m_players == 0 ? tr("Nobody joined yet") : tr("%n player(s) joined", nullptr, m_players));
    else m_info->setText(m_waiting ? tr("Still reaching the P2P servers...") : tr("Getting a code..."));
    m_info->setVisible(!failed);
    m_copy->setVisible(live);
    m_stop->setText(m_tunnel ? tr("Stop hosting") : tr("Close"));
    m_settings->setVisible(!failed);
    setToolTip(tr("Players type the code in Servers, Join with code"));
}
