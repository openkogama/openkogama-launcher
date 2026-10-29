#include "serverspage.h"
#include "centereddelegate.h"
#include "tilegrid.h"
#include "settings.h"
#include "tunnel.h"
#include <QBuffer>
#include <QFrame>
#include <QToolButton>
#include <QStyle>
#include <QMenu>
#include <QGuiApplication>
#include <QStyleHints>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHostAddress>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace {
constexpr int TitleRole = Qt::UserRole + 1;
constexpr int StatusRole = Qt::UserRole + 2;
constexpr int ClientsRole = Qt::UserRole + 3;
constexpr qint64 ThumbnailLimit = 256 * 1024;
constexpr qint64 DownloadLimit = 2 * 1024 * 1024;
constexpr qint64 ListLimit = 256 * 1024;
constexpr int PublicLimit = 200;
constexpr int ClientsLimit = 10;
constexpr int PlayingEveryMs = 60000;
constexpr int RefreshMs = 10000;

QString playersText(int count)
{
    return QCoreApplication::translate("ServersPage", "%n player(s)", nullptr, count);
}

QString titleText(const QString &text)
{
    QString title;
    int length = 0;
    for (char32_t ch : text.simplified().toUcs4()) {
        if (!QChar::isPrint(ch)) continue;
        title += QString::fromUcs4(&ch, 1);
        if (++length == 40) break;
    }
    return title.trimmed();
}

QString shortAddress(const QString &address)
{
    if (Tunnel::isAddress(address)) return Tunnel::display(Tunnel::code(address));
    const QUrl url(address);
    if (url.host().isEmpty()) return address;
    const bool defaultPort = url.port() == -1 || (url.scheme() == "http" && url.port() == 8080) || (url.scheme() == "https" && url.port() == 443);
    return defaultPort ? url.host() : url.host() + ":" + QString::number(url.port());
}

QString sharedAddress(const QString &address)
{
    if (Tunnel::isAddress(address)) return Tunnel::display(Tunnel::code(address));
    return QUrl(address).scheme() == "https" ? address : shortAddress(address);
}

QStringList addressCandidates(const QUrl &entered)
{
    const QString host = entered.host();
    const bool plainHttp = !QHostAddress(host).isNull() || host.compare("localhost", Qt::CaseInsensitive) == 0;
    const auto at = [&host](const QString &scheme, int port) {
        QUrl url;
        url.setScheme(scheme);
        url.setHost(host);
        url.setPort(port);
        return url.toString(QUrl::StripTrailingSlash);
    };
    const int port = entered.port();
    if (port != -1)
        return plainHttp ? QStringList{at("http", port), at("https", port)} : QStringList{at("https", port), at("http", port)};
    return plainHttp ? QStringList{at("http", 8080), at("https", -1)} : QStringList{at("https", -1), at("http", 8080), at("http", -1)};
}

QIcon imageTile(const QByteArray &bytes, const QColor &background)
{
    QPixmap image;
    if (!bytes.isEmpty() && image.loadFromData(bytes, "PNG"))
        return TileGrid::icon(TileGrid::cover(image, TileGrid::WideImage));
    return TileGrid::icon(TileGrid::placeholder(TileGrid::WideImage, background));
}

QByteArray normalizedThumbnail(QByteArray bytes)
{
    if (bytes.isEmpty() || bytes.size() > DownloadLimit) return {};
    QBuffer input(&bytes);
    input.open(QIODevice::ReadOnly);
    QImageReader reader(&input);
    const QByteArray format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "webp") return {};
    const QSize size = reader.size();
    if (!size.isValid() || size.width() > 4096 || size.height() > 4096) return {};
    if (size.width() > 1024 || size.height() > 1024)
        reader.setScaledSize(size.scaled(QSize(1024, 1024), Qt::KeepAspectRatio));
    const QImage image = reader.read();
    if (image.isNull()) return {};
    QImage tile(TileGrid::WideImage, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    const double scale = std::max(double(tile.width()) / image.width(), double(tile.height()) / image.height());
    const QSizeF crop(tile.width() / scale, tile.height() / scale);
    QPainter painter(&tile);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRectF(tile.rect()), image, QRectF((image.width() - crop.width()) / 2,
        (image.height() - crop.height()) / 2, crop.width(), crop.height()));
    painter.end();
    QByteArray result;
    QBuffer output(&result);
    output.open(QIODevice::WriteOnly);
    tile.save(&output, "PNG");
    return result.size() <= ThumbnailLimit ? result : QByteArray();
}
}

ServersPage::ServersPage(QWidget *parent)
    : QWidget(parent), m_net(new QNetworkAccessManager(this)), m_view(new QTabBar(this)), m_list(new QListWidget(this)),
      m_search(new QLineEdit(this)), m_empty(new QLabel(this)), m_selected(new QLabel(this)),
      m_address(new QLabel(this)), m_more(new QToolButton(this)), m_join(new QPushButton(tr("&Join"), this)),
      m_joinCode(new QPushButton(tr("Join with code"), this))
{
    setObjectName("serversPage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    m_view->setObjectName("serversViewTabs");
    m_view->setDrawBase(false);
    m_view->setExpanding(false);
    m_view->addTab(tr("Public"));
    m_view->addTab(tr("Saved"));
    m_view->setTabToolTip(0, tr("Servers that anyone can join"));
    m_view->setTabToolTip(1, tr("Servers you added or joined with a code"));
    m_showPublic = Settings::publicView();
    m_view->setCurrentIndex(m_showPublic ? 0 : 1);
    m_search->setPlaceholderText(tr("Search servers..."));
    m_search->setAccessibleName(tr("Search servers"));
    m_search->setClearButtonEnabled(true);
    auto *add = new QPushButton(tr("Add server..."), this);
    add->setObjectName("addServerButton");
    m_joinCode->setObjectName("joinCodeButton");
    m_joinCode->setToolTip(tr("Join a hosted world with its code"));
    toolbar->addWidget(m_search, 1);
    toolbar->addWidget(m_joinCode);
    toolbar->addWidget(add);
    layout->addWidget(m_view);
    layout->addLayout(toolbar);

    m_empty->setWordWrap(true);
    m_empty->setTextFormat(Qt::PlainText);
    layout->addWidget(m_empty);

    m_list->setObjectName("serversList");
    m_list->setAccessibleName(tr("Your servers"));
    TileGrid::setup(m_list, this);
    layout->addWidget(m_list, 1);

    auto *line = new QFrame(this);
    line->setObjectName("serverActionsLine");
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    layout->addWidget(line);

    auto *actions = new QHBoxLayout;
    m_selected->setObjectName("selectedServerLabel");
    m_selected->setTextFormat(Qt::PlainText);
    m_address->setObjectName("selectedServerAddress");
    m_address->setTextFormat(Qt::PlainText);
    m_address->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_more->setObjectName("moreServerButton");
    m_more->setText(QString::fromUtf8("\u2022\u2022\u2022"));
    m_more->setToolTip(tr("More"));
    m_more->setAutoRaise(true);
    m_more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Copy address"), this, &ServersPage::copyAddress);
    m_save = menu->addAction(tr("Save to my servers"), this, &ServersPage::saveServer);
    menu->addSeparator();
    m_remove = menu->addAction(tr("Remove..."), this, &ServersPage::removeServer);
    m_more->setMenu(menu);
    m_join->setObjectName("joinServerButton");
    m_join->setProperty("primary", true);
    m_join->setMinimumWidth(96);
    actions->addWidget(m_selected);
    actions->addWidget(m_address);
    actions->addStretch(1);
    actions->addWidget(m_more);
    actions->addWidget(m_join);
    layout->addLayout(actions);

    connect(m_view, &QTabBar::currentChanged, this, [this](int index) { showView(index == 0); });
    connect(add, &QPushButton::clicked, this, &ServersPage::addServer);
    connect(m_joinCode, &QPushButton::clicked, this, &ServersPage::joinWithCode);
    connect(m_join, &QPushButton::clicked, this, &ServersPage::join);
    connect(m_list, &QListWidget::itemActivated, this, [this]() { join(); });
    connect(m_list, &QListWidget::currentItemChanged, this, [this]() { updateActions(); });
    connect(m_list, &QListWidget::customContextMenuRequested, this, &ServersPage::showMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this]() { filter(); });
    auto *shortcut = new QShortcut(QKeySequence::Refresh, this);
    shortcut->setObjectName("refreshServersShortcut");
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut, &QShortcut::activated, this, &ServersPage::refresh);
    auto *remove = new QShortcut(QKeySequence::Delete, m_list);
    remove->setContext(Qt::WidgetShortcut);
    connect(remove, &QShortcut::activated, this, &ServersPage::removeServer);
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() {
        if (isVisible()) refresh();
    });
    timer->start(RefreshMs);
    load(Settings::currentServer());
    refresh();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        QTimer::singleShot(0, this, [this]() { load(selectedAddress()); });
    });
}

void ServersPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void ServersPage::setCanJoin(bool enabled)
{
    m_canJoin = enabled;
    updateActions();
}


QString ServersPage::selectedAddress() const
{
    const auto *item = m_list->currentItem();
    return item && !item->isHidden() ? item->data(Qt::UserRole).toString() : QString();
}

QListWidgetItem *ServersPage::find(const QString &address) const
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == address) return m_list->item(i);
    return nullptr;
}

void ServersPage::showView(bool publicView, const QString &select)
{
    if (m_view->currentIndex() != (publicView ? 0 : 1)) {
        const QSignalBlocker blocker(m_view);
        m_view->setCurrentIndex(publicView ? 0 : 1);
    }
    m_showPublic = publicView;
    Settings::setPublicView(publicView);
    load(select.isEmpty() ? Settings::currentServer() : select);
    refresh();
}

void ServersPage::load(const QString &select)
{
    m_list->clear();
    if (m_showPublic) {
        for (const PublicServer &server : std::as_const(m_public)) {
            const QString title = server.title.isEmpty() ? shortAddress(server.address) : server.title;
            auto *item = new QListWidgetItem(imageTile(m_thumbnails.value(server.address), palette().color(QPalette::Button)), title, m_list);
            item->setData(Qt::UserRole, server.address);
            item->setData(TitleRole, title);
            item->setData(ClientsRole, server.clients);
            setStatus(server.address, playersText(server.players));
            if (server.address == select) m_list->setCurrentItem(item);
        }
        if (!m_list->currentItem() && m_list->count()) m_list->setCurrentRow(0);
        filter();
        return;
    }
    for (const GameServer &server : Settings::servers()) {
        const QString title = titleText(server.title.isEmpty()
            ? (server.name.isEmpty() ? server.address : server.name) : server.title);
        auto *item = new QListWidgetItem(imageTile(server.thumbnail, palette().color(QPalette::Button)), title, m_list);
        item->setData(Qt::UserRole, server.address);
        item->setData(TitleRole, title);
        item->setData(ClientsRole, server.clients);
        setStatus(server.address, tr("Checking..."));
        if (server.address == select) m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->count()) m_list->setCurrentRow(0);
    filter();
}

void ServersPage::filter()
{
    const QString search = m_search->text().trimmed();
    QListWidgetItem *first = nullptr;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        const bool visible = item->data(TitleRole).toString().contains(search, Qt::CaseInsensitive)
            || item->data(Qt::UserRole).toString().contains(search, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && !first) first = item;
    }
    if (!m_list->currentItem() || m_list->currentItem()->isHidden()) m_list->setCurrentItem(first);
    QString empty = tr("No servers match your search.");
    if (m_list->count() == 0 && m_showPublic)
        empty = !m_publicLoaded ? tr("Looking for public servers...")
            : m_publicFailed ? tr("Could not load the public servers. Check your internet connection.")
            : tr("No public servers right now.");
    else if (m_list->count() == 0)
        empty = tr("Add a server by its address, or join a hosted world with its code.");
    m_empty->setText(empty);
    m_empty->setVisible(!first);
    updateActions();
}

void ServersPage::updateActions()
{
    const QString address = selectedAddress();
    const auto *item = find(address);
    m_selected->setText(item ? item->data(TitleRole).toString() : tr("No server selected"));
    m_address->setText(shortAddress(address));
    m_more->setEnabled(!address.isEmpty());
    m_save->setVisible(m_showPublic);
    m_save->setEnabled(!address.isEmpty() && !isSaved(address));
    m_remove->setVisible(!m_showPublic);
    m_join->setEnabled(m_canJoin && !address.isEmpty());
    m_join->setToolTip(m_canJoin ? QString() : tr("Add a game version first"));
    m_joinCode->setEnabled(m_canJoin);
}

void ServersPage::setStatus(const QString &address, const QString &status)
{
    if (auto *item = find(address)) {
        const QString versions = item->data(ClientsRole).toStringList().join(", ");
        item->setData(StatusRole, status);
        item->setData(CenteredDelegate::SubtitleRole, versions.isEmpty() ? status : status + QStringLiteral(" · ") + versions);
        item->setToolTip(shortAddress(address) + "\n" + status + (versions.isEmpty() ? QString() : "\n" + tr("Versions: %1").arg(versions)));
    }
}

void ServersPage::get(const QString &url, qint64 limit, std::function<void(int, const QByteArray &)> done)
{
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(8000);
    auto *reply = m_net->get(request);
    reply->setReadBufferSize(limit + 1);
    auto data = std::make_shared<QByteArray>();
    auto collect = [reply, data, limit]() {
        *data += reply->read(limit + 1 - data->size());
        if (data->size() > limit) reply->abort();
    };
    connect(reply, &QNetworkReply::readyRead, this, collect);
    connect(reply, &QNetworkReply::metaDataChanged, this, [reply, limit]() {
        if (reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > limit) reply->abort();
    });
    QTimer::singleShot(10000, reply, [reply]() { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [reply, data, limit, collect, done]() {
        collect();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && data->size() <= limit;
        reply->deleteLater();
        done(ok || status == 404 ? status : 0, ok ? *data : QByteArray());
    });
}

void ServersPage::refresh()
{
    fetchPublic();
    if (m_showPublic) return;
    for (const GameServer &server : Settings::servers()) probe(server.address);
}

void ServersPage::reportPlaying(QObject *game, const QString &address)
{
    auto send = [this, address](bool left) {
        QNetworkRequest request{QUrl(Settings::publicList() + "/playing")};
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        request.setTransferTimeout(8000);
        QJsonObject body{{"address", address}};
        if (left) body.insert("left", true);
        QNetworkReply *reply = m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    };
    auto *timer = new QTimer(game);
    connect(timer, &QTimer::timeout, this, [send]() { send(false); });
    connect(game, &QObject::destroyed, this, [send]() { send(true); });
    timer->start(PlayingEveryMs);
    send(false);
}

void ServersPage::fetchPublic()
{
    if (m_fetching) return;
    m_fetching = true;
    get(Settings::publicList() + "/servers", ListLimit, [this](int status, const QByteArray &data) {
        m_fetching = false;
        m_publicLoaded = true;
        const QJsonDocument document = QJsonDocument::fromJson(data);
        m_publicFailed = status != 200 || !document.isArray();
        if (m_publicFailed) {
            if (m_showPublic) filter();
            return;
        }
        QList<PublicServer> servers;
        for (const QJsonValue &value : document.array()) {
            const QJsonObject entry = value.toObject();
            const QString address = GameServer::addressOf(entry.value("address").toString());
            if (address.isEmpty() || std::any_of(servers.cbegin(), servers.cend(), [&](const PublicServer &s) { return s.address == address; }))
                continue;
            PublicServer server{address, titleText(entry.value("title").toString()), {},
                std::clamp(entry.value("players").toInt(), 0, 999), entry.value("thumbnail").toBool()};
            for (const QJsonValue &client : entry.value("clients").toArray()) {
                const QString text = client.toString().trimmed().left(64);
                if (!text.isEmpty() && server.clients.size() < ClientsLimit) server.clients.append(text);
            }
            servers.append(server);
            if (servers.size() == PublicLimit) break;
        }
        const auto same = [](const PublicServer &a, const PublicServer &b) {
            return a.address == b.address && a.title == b.title && a.clients == b.clients && a.players == b.players && a.thumbnail == b.thumbnail;
        };
        const bool changed = servers.size() != m_public.size() || !std::equal(servers.cbegin(), servers.cend(), m_public.cbegin(), same);
        m_public = servers;
        if (m_showPublic && changed) load(selectedAddress());
        else if (m_showPublic) filter();
        for (const PublicServer &server : std::as_const(m_public)) {
            if (!server.thumbnail || m_thumbnails.contains(server.address)) continue;
            m_thumbnails.insert(server.address, {});
            fetchThumbnail(server.address, Settings::publicList() + "/thumbnail?address=" + QString::fromLatin1(QUrl::toPercentEncoding(server.address)));
        }
    });
}

bool ServersPage::isSaved(const QString &address) const
{
    const QList<GameServer> servers = Settings::servers();
    return std::any_of(servers.cbegin(), servers.cend(), [&](const GameServer &s) { return s.address == address; });
}

bool ServersPage::isPublic(const QString &address) const
{
    return std::any_of(m_public.cbegin(), m_public.cend(), [&](const PublicServer &s) { return s.address == address; });
}

QString ServersPage::titleOf(const QString &address) const
{
    for (const PublicServer &server : m_public)
        if (server.address == address && !server.title.isEmpty()) return server.title;
    for (const GameServer &server : Settings::servers())
        if (server.address == address) return server.title.isEmpty() ? server.name : server.title;
    return {};
}

QStringList ServersPage::clientsOf(const QString &address) const
{
    for (const GameServer &server : Settings::servers())
        if (server.address == address) return server.clients;
    for (const PublicServer &server : m_public)
        if (server.address == address) return server.clients;
    return {};
}

void ServersPage::saveServer()
{
    const QString address = selectedAddress();
    if (address.isEmpty() || isSaved(address)) return;
    auto servers = Settings::servers();
    servers.append({QString(), address, titleOf(address), m_thumbnails.value(address), clientsOf(address)});
    Settings::setServers(servers);
    updateActions();
}

void ServersPage::probe(const QString &address)
{
    if (Tunnel::isAddress(address)) {
        probeCode(address);
        return;
    }
    if (m_pending.contains(address)) return;
    m_pending.insert(address);
    get(address + "/api/server", 4096, [this, address](int status, const QByteArray &data) {
        if (status == 404) {
            get(address + "/ping", 4096, [this, address](int code, const QByteArray &) {
                m_pending.remove(address);
                setStatus(address, code == 200 ? tr("Online") : tr("Offline"));
            });
            return;
        }
        m_pending.remove(address);
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (status != 200 || !document.isObject()) {
            setStatus(address, tr("Offline"));
            return;
        }
        auto servers = Settings::servers();
        auto it = std::find_if(servers.begin(), servers.end(), [&](const GameServer &s) { return s.address == address; });
        if (it == servers.end()) return;
        const QString title = titleText(document.object().value("title").toString());
        QStringList clients;
        for (const QJsonValue &value : document.object().value("clients").toArray()) {
            const QString client = value.toString().trimmed().left(64);
            if (!client.isEmpty()) clients.append(client);
        }
        if ((!title.isEmpty() && title != it->title) || clients != it->clients) {
            if (!title.isEmpty()) it->title = title;
            it->clients = clients;
            Settings::setServers(servers);
        }
        if (auto *item = find(address)) {
            item->setData(ClientsRole, clients);
            if (!title.isEmpty()) {
                item->setData(TitleRole, title);
                item->setText(title);
            }
        }
        setStatus(address, tr("Online"));
        filter();
        if (document.object().value("thumbnail").toString() == "/api/server/thumbnail") fetchThumbnail(address, address + "/api/server/thumbnail");
    });
}

void ServersPage::probeCode(const QString &address)
{
    if (m_pending.contains(address)) return;
    m_pending.insert(address);
    Tunnel *tunnel = Tunnel::find(Tunnel::code(address), this);
    auto found = std::make_shared<bool>(false);
    connect(tunnel, &Tunnel::said, this, [this, address, found](const QString &word, const QString &rest) {
        if (word != "found") return;
        *found = true;
        showFound(address, QJsonDocument::fromJson(rest.toUtf8()).object());
    });
    connect(tunnel, &Tunnel::ended, this, [this, tunnel, address, found]() {
        m_pending.remove(address);
        if (!*found) setStatus(address, tr("Offline"));
        tunnel->deleteLater();
    });
}

void ServersPage::showFound(const QString &address, const QJsonObject &info)
{
    const QString title = titleText(info.value("title").toString());
    const QStringList clients = Tunnel::clients(info);
    auto servers = Settings::servers();
    auto it = std::find_if(servers.begin(), servers.end(), [&](const GameServer &s) { return s.address == address; });
    if (it != servers.end() && ((!title.isEmpty() && title != it->title) || clients != it->clients)) {
        if (!title.isEmpty()) it->title = title;
        it->clients = clients;
        Settings::setServers(servers);
    }
    if (auto *item = find(address)) {
        item->setData(ClientsRole, clients);
        if (!title.isEmpty()) {
            item->setData(TitleRole, title);
            item->setText(title);
        }
    }
    setStatus(address, tr("Online"));
    filter();
}

void ServersPage::probeThrough(const QString &address, const QString &base, int world)
{
    const QString query = world > 0 ? "?world=" + QString::number(world) : QString();
    get(base + "/api/server" + query, 4096, [this, address, base, query](int status, const QByteArray &data) {
        if (status == 200 && QJsonDocument::fromJson(data).object().value("thumbnail").toString().startsWith("/api/server/thumbnail"))
            fetchThumbnail(address, base + "/api/server/thumbnail" + query);
    });
}

void ServersPage::fetchThumbnail(const QString &address, const QString &url)
{
    get(url, DownloadLimit, [this, address](int status, const QByteArray &data) {
        if (status != 200) return;
        const QByteArray thumbnail = normalizedThumbnail(data);
        if (thumbnail.isEmpty()) return;
        m_thumbnails.insert(address, thumbnail);
        auto servers = Settings::servers();
        auto it = std::find_if(servers.begin(), servers.end(), [&](const GameServer &s) { return s.address == address; });
        if (it != servers.end()) {
            it->thumbnail = thumbnail;
            Settings::setServers(servers);
        }
        if (auto *item = find(address)) item->setIcon(imageTile(thumbnail, palette().color(QPalette::Button)));
    });
}

void ServersPage::addServer()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Add server"));
    dialog.setMinimumWidth(380);
    auto *name = new QLineEdit(&dialog);
    name->setMaxLength(40);
    name->setPlaceholderText(tr("Optional name"));
    auto *address = new QLineEdit(&dialog);
    address->setPlaceholderText(tr("192.168.1.20 or example.com"));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Address:"), address);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *confirm = buttons->button(QDialogButtonBox::Ok);
    confirm->setText(tr("Add"));
    confirm->setProperty("primary", true);
    confirm->style()->polish(confirm);
    confirm->setEnabled(false);
    connect(address, &QLineEdit::textChanged, &dialog, [buttons](const QString &text) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]() {
        const QString entered = address->text().trimmed();
        const QString code = entered.contains('.') || entered.contains(':') ? QString() : Tunnel::normalize(entered);
        if (!code.isEmpty()) {
            keepServer(Tunnel::address(code), name->text().trimmed());
            dialog.accept();
            return;
        }
        const bool explicitScheme = entered.contains("://");
        QUrl url(explicitScheme ? entered : "http://" + entered);
        if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")
            || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()
            || (!url.path().isEmpty() && url.path() != "/") || url.port() == 0) {
            QMessageBox::warning(&dialog, tr("Add server"), tr("Enter a server host with an optional port, or an HTTP/HTTPS address."));
            return;
        }
        if (url.path() == "/") url.setPath(QString());
        const QString chosenName = name->text().trimmed();
        if (explicitScheme) {
            keepServer(url.toString(QUrl::StripTrailingSlash), chosenName);
            dialog.accept();
            return;
        }
        const QStringList candidates = addressCandidates(url);
        name->setEnabled(false);
        address->setEnabled(false);
        confirm->setEnabled(false);
        confirm->setText(tr("Checking..."));
        QPointer<QDialog> open(&dialog);
        resolve(candidates, [this, open, candidates, chosenName](const QString &found) {
            if (!open || !open->isVisible()) return;
            keepServer(found.isEmpty() ? candidates.first() : found, chosenName);
            open->accept();
        });
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    address->setFocus();
    dialog.exec();
}

void ServersPage::joinWithCode()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Join with code"));
    dialog.setMinimumWidth(380);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *hint = new QLabel(tr("Type the code shown in the host's launcher after they click Host."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *code = new QLineEdit(&dialog);
    code->setObjectName("joinCodeEdit");
    code->setPlaceholderText("ABC-DEF");
    code->setMaxLength(7);
    code->setAlignment(Qt::AlignCenter);
    layout->addWidget(code);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *confirm = buttons->button(QDialogButtonBox::Ok);
    confirm->setText(tr("Join"));
    confirm->setProperty("primary", true);
    confirm->style()->polish(confirm);
    confirm->setEnabled(false);
    connect(code, &QLineEdit::textEdited, &dialog, [code, confirm](const QString &text) {
        QString clean;
        for (const QChar letter : text.toUpper())
            if (letter.isLetterOrNumber() && clean.size() < 6) clean += letter;
        const QString shown = clean.size() > 3 ? clean.left(3) + "-" + clean.mid(3) : clean;
        if (shown != text) code->setText(shown);
        confirm->setEnabled(!Tunnel::normalize(shown).isEmpty());
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addStretch();
    layout->addWidget(buttons);
    code->setFocus();
    if (dialog.exec() != QDialog::Accepted) return;
    const QString address = Tunnel::address(Tunnel::normalize(code->text()));
    if (!find(address) && isSaved(address)) showView(false, address);
    else if (!find(address)) keepServer(address, QString());
    m_search->clear();
    m_list->setCurrentItem(find(address));
    join();
}

void ServersPage::resolve(QStringList candidates, std::function<void(const QString &)> done)
{
    if (candidates.isEmpty()) {
        done({});
        return;
    }
    const QString address = candidates.takeFirst();
    get(address + "/api/server", 4096, [this, address, candidates, done](int status, const QByteArray &data) {
        if (status == 200 && QJsonDocument::fromJson(data).isObject()) done(address);
        else resolve(candidates, done);
    });
}

void ServersPage::keepServer(const QString &address, const QString &name)
{
    auto servers = Settings::servers();
    auto it = std::find_if(servers.begin(), servers.end(), [&](const GameServer &s) { return s.address == address; });
    const QString fallback = Tunnel::isAddress(address) ? shortAddress(address) : QUrl(address).host();
    const QString title = titleText(name.isEmpty() ? fallback : name);
    if (it == servers.end()) servers.append({title, address, {}, {}});
    else it->name = title;
    Settings::setServers(servers);
    Settings::setCurrentServer(address);
    showView(false, address);
}

void ServersPage::removeServer()
{
    const QString address = selectedAddress();
    if (address.isEmpty() || m_showPublic) return;
    const QString title = find(address)->data(TitleRole).toString();
    if (QMessageBox::question(this, tr("Remove server"), tr("Remove \"%1\" from your servers?").arg(title)) != QMessageBox::Yes) return;
    auto servers = Settings::servers();
    servers.removeIf([&](const GameServer &server) { return server.address == address; });
    Settings::setServers(servers);
    if (Settings::currentServer() == address) Settings::setCurrentServer({});
    load();
    refresh();
}

void ServersPage::copyAddress()
{
    const QString address = selectedAddress();
    if (!address.isEmpty()) QGuiApplication::clipboard()->setText(sharedAddress(address));
}

void ServersPage::showMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item) return;
    m_list->setCurrentItem(item);
    QMenu menu(this);
    QAction *joinAction = menu.addAction(tr("Join"), this, &ServersPage::join);
    joinAction->setEnabled(m_canJoin);
    menu.addAction(tr("Copy address"), this, &ServersPage::copyAddress);
    if (m_showPublic) {
        menu.addAction(tr("Save to my servers"), this, &ServersPage::saveServer)->setEnabled(!isSaved(selectedAddress()));
    } else {
        menu.addSeparator();
        menu.addAction(tr("Remove..."), this, &ServersPage::removeServer);
    }
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}

void ServersPage::join()
{
    const QString address = selectedAddress();
    if (!m_canJoin || address.isEmpty()) return;
    Settings::setCurrentServer(address);
    emit joinRequested(address);
}

bool ServersPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && (event->type() == QEvent::Resize || event->type() == QEvent::Show))
        TileGrid::fit(m_list, TileGrid::WideTile);
    return QWidget::eventFilter(watched, event);
}
