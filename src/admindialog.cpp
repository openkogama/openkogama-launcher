#include "admindialog.h"
#include "paths.h"
#include "serverconfigdialog.h"
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int RefreshMs = 3000;
constexpr int RequestTimeout = 3000;
constexpr int ForeverYears = 5;
constexpr int PeerRole = Qt::UserRole;
constexpr int AdminRole = Qt::UserRole + 1;
constexpr int KeysRole = Qt::UserRole;
const QList<QPair<const char *, int>> Durations = {
    {QT_TRANSLATE_NOOP("AdminDialog", "1 hour"), 1}, {QT_TRANSLATE_NOOP("AdminDialog", "1 day"), 24},
    {QT_TRANSLATE_NOOP("AdminDialog", "1 week"), 24 * 7}, {QT_TRANSLATE_NOOP("AdminDialog", "30 days"), 24 * 30},
    {QT_TRANSLATE_NOOP("AdminDialog", "Forever"), 24 * 365 * 10}};

QTreeWidget *table(const QStringList &columns, QWidget *parent)
{
    auto *tree = new QTreeWidget(parent);
    tree->setColumnCount(columns.size());
    tree->setHeaderLabels(columns);
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setAlternatingRowColors(true);
    tree->header()->setStretchLastSection(false);
    tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    return tree;
}

QString place(const QJsonObject &player)
{
    const QString mode = player.value("mode").toString();
    if (mode == "avatar") return QCoreApplication::translate("AdminDialog", "Avatar editor");
    const QString world = player.value("world").toString();
    const QString name = world.isEmpty() ? QCoreApplication::translate("AdminDialog", "World") : world;
    return mode == "build" ? QCoreApplication::translate("AdminDialog", "%1 (build)").arg(name) : name;
}

QString joined(const QJsonObject &player)
{
    if (player.value("code").toBool()) return QCoreApplication::translate("AdminDialog", "With a code");
    return player.value("admin").toBool() ? QCoreApplication::translate("AdminDialog", "Admin") : QCoreApplication::translate("AdminDialog", "Directly");
}

QString until(const QString &iso)
{
    QDateTime end = QDateTime::fromString(iso, Qt::ISODateWithMs);
    if (!end.isValid()) end = QDateTime::fromString(iso, Qt::ISODate);
    end = end.toLocalTime();
    if (!end.isValid()) return iso;
    if (end > QDateTime::currentDateTime().addYears(ForeverYears)) return QCoreApplication::translate("AdminDialog", "Forever");
    return end.toString("yyyy-MM-dd HH:mm");
}
}

AdminDialog::AdminDialog(QWidget *parent)
    : QDialog(parent), m_net(new QNetworkAccessManager(this)), m_status(new QLabel(this))
{
    setWindowTitle(tr("Admin"));
    resize(560, 380);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *tabs = new QTabWidget(this);
    tabs->addTab(playersPage(), tr("Players"));
    tabs->addTab(bansPage(), tr("Bans"));
    tabs->addTab(adminsPage(), tr("Admins"));
    layout->addWidget(tabs, 1);
    m_status->setObjectName("adminStatus");
    m_status->setWordWrap(true);
    m_status->hide();
    layout->addWidget(m_status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1) loadBans();
    });
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &AdminDialog::loadPlayers);
    timer->start(RefreshMs);
    loadPlayers();
    loadBans();
    updateButtons();
}

QWidget *AdminDialog::playersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    m_players = table({tr("Player"), tr("Version"), tr("World"), tr("Joined")}, page);
    layout->addWidget(m_players, 1);
    auto *row = new QHBoxLayout;
    m_kick = new QPushButton(tr("Kick"), page);
    m_ban = new QPushButton(tr("Ban..."), page);
    row->addWidget(m_kick);
    row->addWidget(m_ban);
    row->addStretch();
    layout->addLayout(row);
    connect(m_players, &QTreeWidget::itemSelectionChanged, this, &AdminDialog::updateButtons);
    connect(m_kick, &QPushButton::clicked, this, &AdminDialog::kick);
    connect(m_ban, &QPushButton::clicked, this, &AdminDialog::ban);
    return page;
}

QWidget *AdminDialog::bansPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    m_bans = table({tr("Player"), tr("Reason"), tr("Until"), tr("Ban on")}, page);
    layout->addWidget(m_bans, 1);
    auto *row = new QHBoxLayout;
    m_unban = new QPushButton(tr("Unban"), page);
    row->addWidget(m_unban);
    row->addStretch();
    layout->addLayout(row);
    connect(m_bans, &QTreeWidget::itemSelectionChanged, this, &AdminDialog::updateButtons);
    connect(m_unban, &QPushButton::clicked, this, &AdminDialog::unban);
    return page;
}

QWidget *AdminDialog::adminsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 8, 0, 0);
    auto *hint = new QLabel(tr("Players with these OpenKogama account names or IP addresses are admins. A name only counts for a player signed in with that account. Players on this computer always are."), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    m_admins = table({tr("Name or IP address")}, page);
    showAdmins(ServerConfigDialog::admins());
    layout->addWidget(m_admins, 1);
    auto *row = new QHBoxLayout;
    m_address = new QLineEdit(page);
    m_address->setPlaceholderText(tr("Account name or IP address"));
    m_add = new QPushButton(tr("Add"), page);
    m_remove = new QPushButton(tr("Remove"), page);
    row->addWidget(m_address, 1);
    row->addWidget(m_add);
    row->addWidget(m_remove);
    layout->addLayout(row);
    connect(m_admins, &QTreeWidget::itemSelectionChanged, this, &AdminDialog::updateButtons);
    connect(m_address, &QLineEdit::textChanged, this, &AdminDialog::updateButtons);
    connect(m_address, &QLineEdit::returnPressed, this, &AdminDialog::addAdmin);
    connect(m_add, &QPushButton::clicked, this, &AdminDialog::addAdmin);
    connect(m_remove, &QPushButton::clicked, this, &AdminDialog::removeAdmin);
    return page;
}

void AdminDialog::loadPlayers()
{
    QNetworkRequest request(QUrl(LocalServerUrl + "/api/admin/players"));
    request.setTransferTimeout(RequestTimeout);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || !document.isArray()) {
            showProblem(tr("The server on this computer is not running."));
            m_players->clear();
            updateButtons();
            return;
        }
        m_status->hide();
        const int selected = m_players->currentItem() ? m_players->currentItem()->data(0, PeerRole).toInt() : -1;
        m_players->clear();
        for (const QJsonValue &value : document.array()) {
            const QJsonObject player = value.toObject();
            auto *item = new QTreeWidgetItem(m_players, {player.value("name").toString(), player.value("client").toString(), place(player), joined(player)});
            item->setData(0, PeerRole, player.value("peer").toInt());
            item->setData(0, AdminRole, player.value("admin").toBool());
            if (item->data(0, PeerRole).toInt() == selected) m_players->setCurrentItem(item);
        }
        updateButtons();
    });
}

void AdminDialog::loadBans()
{
    QNetworkRequest request(QUrl(LocalServerUrl + "/api/admin/bans"));
    request.setTransferTimeout(RequestTimeout);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || !document.isArray()) return;
        m_bans->clear();
        for (const QJsonValue &value : document.array()) {
            const QJsonObject entry = value.toObject();
            auto *item = new QTreeWidgetItem(m_bans, {entry.value("name").toString(), entry.value("reason").toString(),
                until(entry.value("until").toString()), entry.value("code").toBool() ? tr("Nickname and IP") : tr("IP address")});
            item->setData(0, KeysRole, entry.value("keys").toVariant());
        }
        updateButtons();
    });
}

void AdminDialog::kick()
{
    QTreeWidgetItem *item = m_players->currentItem();
    if (!item) return;
    QUrlQuery query;
    query.addQueryItem("peer", QString::number(item->data(0, PeerRole).toInt()));
    post("/api/admin/kick", query, [this](bool ok) {
        if (!ok) showProblem(tr("Could not kick this player."));
        loadPlayers();
    });
}

void AdminDialog::ban()
{
    QTreeWidgetItem *item = m_players->currentItem();
    if (!item) return;
    const int peer = item->data(0, PeerRole).toInt();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Ban %1").arg(item->text(0)));
    dialog.setMinimumWidth(380);
    auto *duration = new QComboBox(&dialog);
    for (const auto &[label, hours] : Durations)
        duration->addItem(tr(label), hours);
    duration->setCurrentIndex(1);
    auto *reason = new QLineEdit(&dialog);
    reason->setMaxLength(200);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->addRow(tr("For:"), duration);
    form->addRow(tr("Reason:"), reason);
    layout->addLayout(form);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *confirm = buttons->button(QDialogButtonBox::Ok);
    confirm->setText(tr("Ban"));
    confirm->setProperty("primary", true);
    confirm->style()->polish(confirm);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addStretch();
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    QUrlQuery query;
    query.addQueryItem("peer", QString::number(peer));
    query.addQueryItem("hours", QString::number(duration->currentData().toInt()));
    query.addQueryItem("reason", reason->text().trimmed());
    post("/api/admin/ban", query, [this](bool ok) {
        if (!ok) showProblem(tr("Could not ban this player."));
        loadPlayers();
        loadBans();
    });
}

void AdminDialog::unban()
{
    QTreeWidgetItem *item = m_bans->currentItem();
    if (!item) return;
    QUrlQuery query;
    for (const QVariant &key : item->data(0, KeysRole).toList())
        query.addQueryItem("key", key.toString());
    post("/api/admin/unban", query, [this](bool ok) {
        if (!ok) showProblem(tr("Could not lift this ban."));
        loadBans();
    });
}

void AdminDialog::addAdmin()
{
    const QString entry = m_address->text().trimmed();
    if (entry.isEmpty()) return;
    const QHostAddress address(entry);
    bool mapped = false;
    const quint32 ipv4 = address.toIPv4Address(&mapped);
    const QString text = address.isNull() ? entry : mapped ? QHostAddress(ipv4).toString() : address.toString();
    QStringList admins = ServerConfigDialog::admins();
    if (!admins.contains(text, Qt::CaseInsensitive)) admins << text;
    saveAdmins(admins);
    m_address->clear();
}

void AdminDialog::removeAdmin()
{
    QTreeWidgetItem *item = m_admins->currentItem();
    if (!item) return;
    QStringList admins = ServerConfigDialog::admins();
    admins.removeAll(item->text(0));
    saveAdmins(admins);
}

void AdminDialog::showAdmins(const QStringList &admins)
{
    m_admins->clear();
    for (const QString &address : admins)
        new QTreeWidgetItem(m_admins, {address});
}

void AdminDialog::saveAdmins(const QStringList &admins)
{
    if (!ServerConfigDialog::setAdmins(admins)) {
        showProblem(tr("Could not save the admins into server.json. Check it in File, Server settings."));
        return;
    }
    showAdmins(admins);
    QNetworkRequest request(QUrl(LocalServerUrl + "/api/admin/admins"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(RequestTimeout);
    QNetworkReply *reply = m_net->post(request, QJsonDocument(QJsonArray::fromStringList(admins)).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    updateButtons();
}

void AdminDialog::post(const QString &path, const QUrlQuery &query, const std::function<void(bool)> &done)
{
    QUrl url(LocalServerUrl + path);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    request.setTransferTimeout(RequestTimeout);
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        reply->deleteLater();
        done(reply->error() == QNetworkReply::NoError);
    });
}

void AdminDialog::updateButtons()
{
    QTreeWidgetItem *player = m_players->currentItem();
    const bool removable = player && !player->data(0, AdminRole).toBool();
    m_kick->setEnabled(removable);
    m_ban->setEnabled(removable);
    m_unban->setEnabled(m_bans->currentItem() != nullptr);
    m_add->setEnabled(!m_address->text().trimmed().isEmpty());
    m_remove->setEnabled(m_admins->currentItem() != nullptr);
}

void AdminDialog::showProblem(const QString &text)
{
    m_status->setText(text);
    m_status->show();
}
