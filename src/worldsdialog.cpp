#include "worldsdialog.h"
#include "ui_worldsdialog.h"
#include "launchdialog.h"
#include "gamelauncher.h"
#include "playerprofile.h"
#include "centereddelegate.h"
#include "tilegrid.h"
#include "webplayerruntime.h"
#include "consolewindow.h"
#include "modelpreview.h"
#include "serverconfigdialog.h"
#include "settings.h"
#include "theme.h"
#include <QEvent>
#include <QFutureWatcher>
#include <QLocale>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>
#include <QKeyEvent>
#include <QStyle>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QGuiApplication>
#include <QStyleHints>
#include <algorithm>

static constexpr int NameRole = Qt::UserRole + 1;
static constexpr int ImageRole = Qt::UserRole + 2;
static constexpr int ActiveRole = Qt::UserRole + 3;
static const QUrl MapsArchive("https://cdn.openkogama.org/maps/v1/");
static const QString GameModelName("CubeModel");

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QImage>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QInputDialog>
#include <QMenu>
#include <QShortcut>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QUrlQuery>

static QString instanceTitle(const QString &path)
{
    QFile file(QDir(path).filePath("instance.json"));
    QJsonObject info;
    if (file.open(QIODevice::ReadOnly))
        info = QJsonDocument::fromJson(file.readAll()).object();
    for (const char *key : {"name", "version"}) {
        QString value = info.value(key).toString().trimmed();
        if (!value.isEmpty())
            return "KoGaMa " + value;
    }
    return "KoGaMa";
}

static QString guestMode()
{
    return ServerConfigDialog::current().value("joinMode").toString().compare("build", Qt::CaseInsensitive) == 0 ? "edit" : "play";
}

static std::optional<QString> askScale(QWidget *parent, const QString &title, const QString &confirm, const QString &current)
{
    constexpr double MaxScale = 100;
    const QList<std::pair<QString, QString>> presets{
        {QCoreApplication::translate("WorldsDialog", "Small (0.125)"), "0.125"},
        {QCoreApplication::translate("WorldsDialog", "Medium (0.25)"), "0.25"},
        {QCoreApplication::translate("WorldsDialog", "Large (0.5)"), "0.5"},
    };

    QDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setMinimumWidth(380);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *hint = new QLabel(QCoreApplication::translate("WorldsDialog", "Size of one cube in the game."), &dialog);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *size = new QComboBox(&dialog);
    for (const auto &[label, value] : presets) size->addItem(label, value);
    size->addItem(QCoreApplication::translate("WorldsDialog", "Custom"));
    const int custom = size->count() - 1;
    auto *field = new QLineEdit(&dialog);
    field->setValidator(new QRegularExpressionValidator(QRegularExpression("\\d{0,3}([.,]\\d{0,4})?"), field));
    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->addRow(QCoreApplication::translate("WorldsDialog", "Size:"), size);
    form->addRow(QCoreApplication::translate("WorldsDialog", "Scale:"), field);
    layout->addLayout(form);
    const int preset = size->findData(current);
    size->setCurrentIndex(preset >= 0 ? preset : custom);
    if (preset < 0) field->setText(current);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    ok->setText(confirm);
    ok->setProperty("primary", true);
    ok->style()->polish(ok);
    const auto check = [size, field, form, ok, custom]() {
        const bool own = size->currentIndex() == custom;
        form->setRowVisible(field, own);
        const QString text = field->text().trimmed();
        bool number = false;
        const double value = QString(text).replace(',', '.').toDouble(&number);
        ok->setEnabled(!own || (number && value > 0 && value <= MaxScale));
    };
    QObject::connect(size, &QComboBox::currentIndexChanged, &dialog, [check, size, field, custom]() {
        check();
        if (size->currentIndex() == custom) field->setFocus();
    });
    QObject::connect(field, &QLineEdit::textChanged, &dialog, check);
    check();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addStretch();
    layout->addWidget(buttons);
    size->setFocus();

    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    if (size->currentIndex() != custom) return size->currentData().toString();
    return field->text().trimmed().replace(',', '.');
}

WorldsDialog::WorldsDialog(const QString &path, const QString &exe, QProcess *server, QWidget *parent)
    : QDialog(nullptr)
    , ui(new Ui::WorldsDialog)
    , m_owner(parent)
    , m_path(path)
    , m_title(instanceTitle(path))
    , m_exe(exe)
    , m_server(server)
    , m_net(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    m_net->setTransferTimeout(8000);
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setWindowIcon(Theme::icon(":/launch.png"));
    setWindowTitle(m_title);

    connect(ui->playButton, &QPushButton::clicked, this, [this]() { launch("play", selectedWorld()); });
    connect(ui->buildButton, &QPushButton::clicked, this, [this]() { launch("edit", selectedWorld()); });
    connect(ui->hostButton, &QPushButton::clicked, this, [this]() {
        const int world = selectedWorld();
        if (world == 0) return;
        const QString name = ui->worldsList->currentItem()->data(NameRole).toString();
        emit hostRequested(world, name, launch(guestMode(), world));
    });
    connect(ui->editAvatarButton, &QPushButton::clicked, this, &WorldsDialog::editAvatar);
    connect(ui->newButton, &QPushButton::clicked, this, &WorldsDialog::createWorld);
    connect(ui->importButton, &QPushButton::clicked, this, &WorldsDialog::importWorld);
    auto *worldMenu = new QMenu(this);
    worldMenu->addAction(tr("Export..."), this, &WorldsDialog::exportWorld);
    worldMenu->addAction(tr("Rename"), this, &WorldsDialog::renameWorld);
    worldMenu->addSeparator();
    worldMenu->addAction(tr("Delete..."), this, &WorldsDialog::deleteWorld);
    ui->moreWorldButton->setMenu(worldMenu);
    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), ui->worldsList);
    renameShortcut->setContext(Qt::WidgetShortcut);
    connect(renameShortcut, &QShortcut::activated, this, &WorldsDialog::renameWorld);
    connect(ui->worldsList, &QListWidget::itemChanged, this, &WorldsDialog::onWorldRenamed);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, ui->worldsList);
    deleteShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, &WorldsDialog::deleteWorld);
    connect(ui->worldsList, &QListWidget::customContextMenuRequested, this, &WorldsDialog::showWorldMenu);
    auto *refreshWorlds = new QShortcut(QKeySequence::Refresh, ui->worldsTab);
    refreshWorlds->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshWorlds, &QShortcut::activated, this, [this]() { loadWorlds(selectedWorld()); });
    auto *refreshAvatars = new QShortcut(QKeySequence::Refresh, ui->avatarsTab);
    refreshAvatars->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshAvatars, &QShortcut::activated, this, [this]() { loadAvatars(selectedAvatar()); });
    connect(ui->worldSearchLineEdit, &QLineEdit::textChanged, this, [this]() { filterWorlds(); });
    connect(ui->avatarSearchLineEdit, &QLineEdit::textChanged, this, [this]() { filterAvatars(); });
    connect(ui->worldsList, &QListWidget::currentItemChanged, this, [this]() { updateWorldActions(); });
    connect(ui->avatarsList, &QListWidget::currentItemChanged, this, [this]() { updateAvatarActions(); });
    connect(ui->worldsList, &QListWidget::itemActivated, this, [this]() { launch(ui->playButton->isEnabled() ? "play" : "edit", selectedWorld()); });

    connect(ui->useAvatarButton, &QPushButton::clicked, this, &WorldsDialog::useAvatar);
    connect(ui->importAvatarButton, &QPushButton::clicked, this, &WorldsDialog::importAvatar);
    auto *avatarMenu = new QMenu(this);
    avatarMenu->addAction(tr("Export..."), this, &WorldsDialog::exportAvatar);
    avatarMenu->addAction(tr("Rename"), this, &WorldsDialog::renameAvatar);
    avatarMenu->addSeparator();
    avatarMenu->addAction(tr("Delete..."), this, &WorldsDialog::deleteAvatar);
    ui->moreAvatarButton->setMenu(avatarMenu);
    connect(ui->avatarsList, &QListWidget::itemChanged, this, &WorldsDialog::onAvatarRenamed);
    auto *renameAvatarShortcut = new QShortcut(QKeySequence(Qt::Key_F2), ui->avatarsList);
    renameAvatarShortcut->setContext(Qt::WidgetShortcut);
    connect(renameAvatarShortcut, &QShortcut::activated, this, &WorldsDialog::renameAvatar);
    auto *deleteAvatarShortcut = new QShortcut(QKeySequence::Delete, ui->avatarsList);
    deleteAvatarShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteAvatarShortcut, &QShortcut::activated, this, &WorldsDialog::deleteAvatar);
    connect(ui->avatarsList, &QListWidget::itemActivated, this, &WorldsDialog::useAvatar);
    connect(ui->avatarsList, &QListWidget::customContextMenuRequested, this, &WorldsDialog::showAvatarMenu);

    connect(ui->importModelButton, &QPushButton::clicked, this, &WorldsDialog::importModel);
    connect(ui->inventorySearchLineEdit, &QLineEdit::textChanged, this, [this]() { filterInventory(); });
    connect(ui->inventoryList, &QListWidget::currentItemChanged, this, [this]() { updateInventoryActions(); });
    auto *itemMenu = new QMenu(this);
    itemMenu->addAction(tr("Export..."), this, &WorldsDialog::exportItem);
    itemMenu->addAction(tr("Rename"), this, &WorldsDialog::renameItem);
    itemMenu->addAction(tr("Scale..."), this, &WorldsDialog::scaleItem);
    itemMenu->addSeparator();
    itemMenu->addAction(tr("Delete..."), this, &WorldsDialog::deleteItem);
    ui->moreItemButton->setMenu(itemMenu);
    connect(ui->inventoryList, &QListWidget::itemChanged, this, &WorldsDialog::onItemRenamed);
    auto *renameItemShortcut = new QShortcut(QKeySequence(Qt::Key_F2), ui->inventoryList);
    renameItemShortcut->setContext(Qt::WidgetShortcut);
    connect(renameItemShortcut, &QShortcut::activated, this, &WorldsDialog::renameItem);
    auto *deleteItemShortcut = new QShortcut(QKeySequence::Delete, ui->inventoryList);
    deleteItemShortcut->setContext(Qt::WidgetShortcut);
    connect(deleteItemShortcut, &QShortcut::activated, this, &WorldsDialog::deleteItem);
    auto *refreshInventory = new QShortcut(QKeySequence::Refresh, ui->inventoryTab);
    refreshInventory->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshInventory, &QShortcut::activated, this, [this]() { loadInventory(selectedItem()); });
    connect(ui->inventoryList, &QListWidget::customContextMenuRequested, this, &WorldsDialog::showItemMenu);

    TileGrid::setup(ui->worldsList, this);
    TileGrid::setup(ui->avatarsList, this);
    TileGrid::setup(ui->inventoryList, this);
    static_cast<CenteredDelegate *>(ui->avatarsList->itemDelegate())->setBadgeRole(ActiveRole);

    setInstance(path, exe);
    updateWorldActions();
    updateAvatarActions();
    updateInventoryActions();
    loadWorlds();
    loadTemplates();
    loadAvatars();
    loadInventory();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        QTimer::singleShot(0, this, [this]() {
            const int world = selectedWorld();
            const int avatar = selectedAvatar();
            const int item = selectedItem();
            ui->worldsList->clear();
            ui->avatarsList->clear();
            ui->inventoryList->clear();
            loadWorlds(world);
            loadAvatars(avatar);
            loadInventory(item);
        });
    });

    auto *poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, &WorldsDialog::checkRevision);
    poll->start(1000);
}

WorldsDialog::~WorldsDialog()
{
    delete ui;
}

void WorldsDialog::setInstance(const QString &path, const QString &exe, QProcess *server) {
    if (server) m_server = server;
    m_path = path;
    m_exe = exe;
    m_title = instanceTitle(path);
    QFile file(QDir(path).filePath("instance.json"));
    QJsonObject info;
    if (file.open(QIODevice::ReadOnly))
        info = QJsonDocument::fromJson(file.readAll()).object();
    m_hasAvatarEditor = !WebPlayerRuntime::isWebPlayerFile(exe)
        || !info.value("unityVersion").toString().startsWith("3.4");
    ui->editAvatarButton->setToolTip(m_hasAvatarEditor ? QString() : tr("This game version has no avatar editor"));
    updateAvatarActions();
}

QWidget *WorldsDialog::worldsPage() const {
    return ui->worldsTab;
}

QWidget *WorldsDialog::avatarsPage() const {
    return ui->avatarsTab;
}

QWidget *WorldsDialog::inventoryPage() const {
    return ui->inventoryTab;
}

void WorldsDialog::showAvatars() {
    loadAvatars(selectedAvatar());
}

void WorldsDialog::showInventory() {
    loadInventory(selectedItem());
}

bool WorldsDialog::hasRunningGames() const {
    return std::any_of(m_clients.cbegin(), m_clients.cend(), [](const QPointer<QProcess> &client) {
        return client && client->state() != QProcess::NotRunning;
    });
}

void WorldsDialog::updateWorldActions() {
    QListWidgetItem *item = ui->worldsList->currentItem();
    bool selected = item && !item->isHidden();
    const QString guests = hostedMode(selectedWorld());
    ui->playButton->setEnabled(selected && guests != "edit");
    ui->hostButton->setEnabled(selected);
    ui->buildButton->setEnabled(selected && guests != "play");
    ui->playButton->setToolTip(guests == "edit" ? tr("Your guests join in build (Server settings)") : QString());
    ui->buildButton->setToolTip(guests == "play" ? tr("Your guests join in play (Server settings)") : QString());
    ui->moreWorldButton->setEnabled(selected);
    ui->selectedWorldLabel->setText(selected ? item->text() : tr("No world selected"));
}

void WorldsDialog::filterWorlds() {
    const QString search = ui->worldSearchLineEdit->text().trimmed();
    QListWidgetItem *first = nullptr;
    int visible = 0;
    for (int i = 0; i < ui->worldsList->count(); ++i) {
        auto *item = ui->worldsList->item(i);
        bool matches = item->text().contains(search, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches) {
            ++visible;
            if (!first) first = item;
        }
    }
    auto *current = ui->worldsList->currentItem();
    if (!current || current->isHidden())
        ui->worldsList->setCurrentItem(first);
    int total = ui->worldsList->count();
    ui->worldSearchLineEdit->setPlaceholderText(total > 0 ? tr("Search %n world(s)...", nullptr, total) : tr("Search your worlds..."));
    ui->worldStatusLabel->setText(total == 0
        ? tr("No worlds yet. Click New world to create one, or Import to open a saved file.")
        : visible == 0 ? tr("No worlds match your search.") : QString());
    ui->worldStatusLabel->setVisible(!ui->worldStatusLabel->text().isEmpty());
    updateWorldActions();
}

void WorldsDialog::filterAvatars() {
    const QString search = ui->avatarSearchLineEdit->text().trimmed();
    QListWidgetItem *first = nullptr;
    int visible = 0;
    for (int i = 0; i < ui->avatarsList->count(); ++i) {
        auto *item = ui->avatarsList->item(i);
        bool matches = item->text().contains(search, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches) {
            ++visible;
            if (!first) first = item;
        }
    }
    auto *current = ui->avatarsList->currentItem();
    if (!current || current->isHidden())
        ui->avatarsList->setCurrentItem(first);
    int total = ui->avatarsList->count();
    ui->avatarSearchLineEdit->setPlaceholderText(total > 0 ? tr("Search %n avatar(s)...", nullptr, total) : tr("Search your avatars..."));
    ui->avatarStatusLabel->setText(!m_hasAvatarEditor ? tr("This game version has no avatar editor. Choose another version to edit characters.")
        : total == 0 ? tr("No avatars yet. Open the avatar editor to create your character.")
        : visible == 0 ? tr("No avatars match your search.") : QString());
    ui->avatarStatusLabel->setVisible(!ui->avatarStatusLabel->text().isEmpty());
    updateAvatarActions();
}

void WorldsDialog::updateAvatarActions() {
    QListWidgetItem *item = ui->avatarsList->currentItem();
    const bool active = item && item->data(ActiveRole).toBool();
    ui->useAvatarButton->setEnabled(item && !active);
    ui->editAvatarButton->setEnabled(m_hasAvatarEditor);
    ui->moreAvatarButton->setEnabled(item != nullptr);
    ui->selectedAvatarLabel->setText(item ? item->text() : tr("No avatar selected"));
}

void WorldsDialog::keyPressEvent(QKeyEvent *event) {
    if (isWindow())
        QDialog::keyPressEvent(event);
    else
        QWidget::keyPressEvent(event);
}

void WorldsDialog::reject() {
    if (m_server)
        LaunchDialog::stopServer(m_server);
    QDialog::reject();
}

void WorldsDialog::loadWorlds(int select) {
    if (ui->worldsList->count() == 0) {
        ui->worldStatusLabel->setText(tr("Loading your worlds..."));
        ui->worldStatusLabel->show();
    }
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/worlds")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select, url = LocalServerUrl]() {
        reply->deleteLater();
        if (url != LocalServerUrl) return;
        if (reply->error() != QNetworkReply::NoError) {
            ui->worldStatusLabel->setText(tr("Could not load your worlds. Press F5 to try again."));
            ui->worldStatusLabel->show();
            return;
        }
        const QJsonArray worlds = QJsonDocument::fromJson(reply->readAll()).array();
        QSignalBlocker blocker(ui->worldsList);

        QList<int> ids;
        for (const QJsonValue &value : worlds)
            ids.append(value.toObject()["id"].toInt());
        QList<int> shown;
        for (int i = 0; i < ui->worldsList->count(); ++i)
            shown.append(ui->worldsList->item(i)->data(Qt::UserRole).toInt());
        if (ids != shown) {
            ui->worldsList->clear();
            const QIcon empty = TileGrid::icon(TileGrid::placeholder(TileGrid::WideImage, palette().color(QPalette::Button)));
            for (int id : std::as_const(ids)) {
                auto *item = new QListWidgetItem(empty, QString(), ui->worldsList);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(Qt::UserRole, id);
            }
        }

        auto date = [](const QJsonValue &value) {
            return QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm");
        };
        for (int i = 0; i < worlds.size(); ++i) {
            QJsonObject world = worlds[i].toObject();
            QListWidgetItem *item = ui->worldsList->item(i);
            QString name = world["name"].toString();
            if (item->data(NameRole).toString() != name) {
                item->setText(name);
                item->setData(NameRole, name);
            }
            QString tip = tr("Last played: %1").arg(world["playedAt"].isString() ? date(world["playedAt"]) : tr("never")) + "\n"
                + tr("Saved: %1").arg(date(world["savedAt"])) + "\n"
                + (world["publishedAt"].isString() ? tr("Published: %1").arg(date(world["publishedAt"])) : tr("Not published"));
            if (item->toolTip() != tip)
                item->setToolTip(tip);
            if (ids[i] == select)
                ui->worldsList->setCurrentItem(item);
            loadThumbnail(item, ids[i]);
        }
        if (!ui->worldsList->currentItem() && ui->worldsList->count() > 0)
            ui->worldsList->setCurrentRow(0);
        filterWorlds();
    });
}

void WorldsDialog::checkRevision() {
    QNetworkRequest request(QUrl(LocalServerUrl + "/api/worlds/revision"));
    request.setTransferTimeout(500);
    QNetworkReply *reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        int revision = QJsonDocument::fromJson(reply->readAll()).object()["revision"].toInt();
        if (revision == m_revision) return;
        bool first = m_revision < 0;
        m_revision = revision;
        if (!first && !ui->worldsList->viewport()->findChild<QLineEdit *>())
            loadWorlds(selectedWorld());
    });
}

void WorldsDialog::loadThumbnail(QListWidgetItem *item, int world) {
    QPointer<QListWidget> list = ui->worldsList;
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/images/0/" + QString::number(world) + ".png?v=" + QString::number(m_revision))));
    connect(reply, &QNetworkReply::finished, this, [reply, list, item, world]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QPixmap image;
        if (reply->error() != QNetworkReply::NoError || !list) return;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i) != item || item->data(Qt::UserRole).toInt() != world || item->data(ImageRole).toByteArray() == data) continue;
            if (!image.loadFromData(data)) return;
            item->setData(ImageRole, data);
            item->setIcon(TileGrid::icon(TileGrid::cover(image, TileGrid::WideImage)));
        }
    });
}

bool WorldsDialog::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Resize && (watched == ui->worldsList || watched == ui->avatarsList || watched == ui->inventoryList)) {
        auto *list = static_cast<QListWidget *>(watched);
        TileGrid::fit(list, list == ui->worldsList ? TileGrid::WideTile : TileGrid::SquareTile);
    }
    return QDialog::eventFilter(watched, event);
}

void WorldsDialog::loadTemplates() {
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/templates")));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_templates.clear();
        const QJsonArray templates = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &value : templates)
            m_templates.append({value.toObject()["id"].toString(), value.toObject()["name"].toString()});
    });
}

void WorldsDialog::createWorld() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New world"));

    dialog.setMinimumWidth(380);

    auto *name = new QLineEdit(tr("New world"), &dialog);
    name->selectAll();
    auto *templates = new QComboBox(&dialog);
    for (const auto &[id, title] : std::as_const(m_templates))
        templates->addItem(title, id);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *create = buttons->button(QDialogButtonBox::Ok);
    create->setText(tr("Create"));
    create->setProperty("primary", true);
    create->style()->polish(create);
    connect(name, &QLineEdit::textChanged, &dialog, [create](const QString &value) { create->setEnabled(!value.trimmed().isEmpty()); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    auto *form = new QFormLayout;
    form->setSpacing(8);
    form->addRow(tr("Name:"), name);
    form->addRow(tr("Template:"), templates);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return;

    QUrl url(LocalServerUrl + "/api/worlds");
    QUrlQuery query;
    query.addQueryItem("name", name->text().trimmed());
    if (templates->currentIndex() >= 0)
        query.addQueryItem("template", templates->currentData().toString());
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        loadWorlds(QJsonDocument::fromJson(reply->readAll()).object()["id"].toInt());
    });
}

void WorldsDialog::importWorld() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Import worlds"), QString(), tr("KoGaMa and ReGaMa maps (*.kgmap *.kgm *.json *.bin);;All files (*)"));
    if (!paths.isEmpty()) importWorlds(paths, {});
}

void WorldsDialog::importWorlds(QStringList paths, QStringList failed) {
    if (paths.isEmpty()) {
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("Import"), tr("These map(s) could not be imported: %1", nullptr, failed.size()).arg(failed.join(", ")));
        return;
    }
    const QString path = paths.takeFirst();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        importWorlds(paths, failed << QFileInfo(path).fileName());
        return;
    }

    QUrl url(LocalServerUrl + "/api/worlds/import");
    QUrlQuery query;
    query.addQueryItem("name", QFileInfo(path).completeBaseName());
    QFile meta(QDir(m_path).filePath("instance.json"));
    QString version = meta.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(meta.readAll()).object().value("version").toString() : QString();
    if (!version.isEmpty())
        query.addQueryItem("client", version);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    const QByteArray data = file.readAll();
    QNetworkReply *reply = m_net->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, path, paths, failed]() mutable {
        reply->deleteLater();
        int id = QJsonDocument::fromJson(reply->readAll()).object()["id"].toInt();
        if (id == 0) failed << QFileInfo(path).fileName();
        else findArchiveThumbnail(id, data);
        loadWorlds(id);
        importWorlds(paths, failed);
    });
}

void WorldsDialog::findArchiveThumbnail(int world, const QByteArray &file) {
    const QString sha = QCryptographicHash::hash(file, QCryptographicHash::Sha256).toHex();
    QNetworkReply *reply = m_net->get(QNetworkRequest(MapsArchive.resolved(QUrl("latest.json"))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, world, sha]() {
        reply->deleteLater();
        const QJsonObject latest = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject hashes = latest["hashes"].toObject();
        if (reply->error() != QNetworkReply::NoError || hashes.isEmpty()) return;
        const QUrl build = MapsArchive.resolved(QUrl("builds/" + latest["build"].toString() + "/"));
        const QUrl images = build.resolved(QUrl(latest["images"].toObject()["path"].toString()));
        QNetworkReply *lookup = m_net->get(QNetworkRequest(build.resolved(QUrl(hashes["path"].toString() + sha.left(hashes["prefix"].toInt(2)) + ".json"))));
        connect(lookup, &QNetworkReply::finished, this, [this, lookup, world, sha, images]() {
            lookup->deleteLater();
            const QString image = QJsonDocument::fromJson(lookup->readAll()).object()[sha].toArray().at(1).toString();
            if (lookup->error() == QNetworkReply::NoError && !image.isEmpty())
                downloadArchiveThumbnail(world, images.resolved(QUrl(image + ".jpg")));
        });
    });
}

void WorldsDialog::downloadArchiveThumbnail(int world, const QUrl &url) {
    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, world]() {
        reply->deleteLater();
        QImage image;
        QByteArray png;
        QBuffer buffer(&png);
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll())
            || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return;
        QNetworkRequest request(QUrl(LocalServerUrl + "/api/worlds/thumbnail?id=" + QString::number(world)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        QNetworkReply *upload = m_net->post(request, png);
        connect(upload, &QNetworkReply::finished, upload, &QObject::deleteLater);
    });
}

void WorldsDialog::exportWorld() {
    QListWidgetItem *item = ui->worldsList->currentItem();
    if (!item) return;

    QString name = item->data(NameRole).toString();
    name.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    QString path = QFileDialog::getSaveFileName(this, tr("Export world"), name + ".kgmap", tr("KoGaMa maps (*.kgmap)"));
    if (path.isEmpty()) return;

    QUrl url(LocalServerUrl + "/api/worlds/export");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(item->data(Qt::UserRole).toInt()));
    url.setQuery(query);

    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        QFile file(path);
        if (reply->error() != QNetworkReply::NoError || !file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0)
            QMessageBox::warning(this, tr("Export"), tr("Could not export this world"));
    });
}

void WorldsDialog::renameWorld() {
    QListWidgetItem *item = ui->worldsList->currentItem();
    if (!item || item->isHidden()) return;
    ui->worldsList->editItem(item);
}

void WorldsDialog::onWorldRenamed(QListWidgetItem *item) {
    QString name = item->text().trimmed();
    QString previous = item->data(NameRole).toString();
    if (name == previous) return;
    if (name.isEmpty()) {
        item->setText(previous);
        return;
    }

    item->setData(NameRole, name);
    updateWorldActions();
    int id = item->data(Qt::UserRole).toInt();
    QUrl url(LocalServerUrl + "/api/worlds/rename");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(id));
    query.addQueryItem("name", name);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Rename"), tr("Could not rename this world"));
            loadWorlds(id);
        }
    });
}

void WorldsDialog::deleteWorld() {
    QListWidgetItem *item = ui->worldsList->currentItem();
    if (!item) return;

    auto answer = QMessageBox::question(this, tr("Delete world"),
        tr("Delete \"%1\"? This can't be undone.").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QUrl url(LocalServerUrl + "/api/worlds/delete");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(item->data(Qt::UserRole).toInt()));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            QMessageBox::warning(this, tr("Delete world"), tr("This world is open in the game. Close it first."));
        loadWorlds();
    });
}

void WorldsDialog::showWorldMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->worldsList->itemAt(pos);
    if (!item) return;
    ui->worldsList->setCurrentItem(item);

    QMenu menu(this);
    QAction *play = menu.addAction(tr("Play"));
    QAction *build = menu.addAction(tr("Build"));
    play->setEnabled(ui->playButton->isEnabled());
    build->setEnabled(ui->buildButton->isEnabled());
    menu.addSeparator();
    QAction *exported = menu.addAction(tr("Export..."));
    QAction *rename = menu.addAction(tr("Rename"));
    QAction *remove = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(ui->worldsList->viewport()->mapToGlobal(pos));
    if (chosen == play) launch("play", selectedWorld());
    else if (chosen == build) launch("edit", selectedWorld());
    else if (chosen == exported) exportWorld();
    else if (chosen == rename) renameWorld();
    else if (chosen == remove) deleteWorld();
}

void WorldsDialog::loadAvatars(int select) {
    if (ui->avatarsList->count() == 0) {
        ui->avatarStatusLabel->setText(tr("Loading your avatars..."));
        ui->avatarStatusLabel->show();
    }
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/avatars")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select, url = LocalServerUrl]() {
        reply->deleteLater();
        if (url != LocalServerUrl) return;
        if (reply->error() != QNetworkReply::NoError) {
            ui->avatarStatusLabel->setText(tr("Could not load your avatars. Press F5 to try again."));
            ui->avatarStatusLabel->show();
            return;
        }
        const QJsonArray avatars = QJsonDocument::fromJson(reply->readAll()).array();
        QString version = QString::number(QDateTime::currentMSecsSinceEpoch());

        QList<int> ids;
        for (const QJsonValue &value : avatars)
            ids.append(value.toObject()["id"].toInt());
        QList<int> shown;
        for (int i = 0; i < ui->avatarsList->count(); ++i)
            shown.append(ui->avatarsList->item(i)->data(Qt::UserRole).toInt());
        QSignalBlocker blocker(ui->avatarsList);
        if (ids != shown) {
            ui->avatarsList->clear();
            const QIcon empty = TileGrid::icon(TileGrid::placeholder(TileGrid::SquareImage, palette().color(QPalette::Button)));
            for (int id : std::as_const(ids)) {
                auto *item = new QListWidgetItem(empty, QString(), ui->avatarsList);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(Qt::UserRole, id);
                item->setToolTip(tr("No picture yet, take one with the camera in the avatar editor"));
            }
        }

        for (int i = 0; i < avatars.size(); ++i) {
            bool active = avatars[i].toObject()["active"].toBool();
            QListWidgetItem *item = ui->avatarsList->item(i);
            item->setData(ActiveRole, active);
            QString name = avatars[i].toObject()["name"].toString();
            QString text = name.isEmpty() ? tr("Avatar %1").arg(ids[i]) : name;
            if (item->text() != text) {
                item->setText(text);
                item->setData(NameRole, text);
            }
            QFont font = item->font();
            if (font.bold() != active) {
                font.setBold(active);
                item->setFont(font);
            }
            loadAvatarPicture(item, ids[i], version);
            if (ids[i] == select || (select == 0 && active))
                ui->avatarsList->setCurrentItem(item);
        }
        if (!ui->avatarsList->currentItem() && ui->avatarsList->count() > 0)
            ui->avatarsList->setCurrentRow(0);
        filterAvatars();
    });
}

void WorldsDialog::loadAvatarPicture(QListWidgetItem *item, int avatar, const QString &version) {
    QPointer<QListWidget> list = ui->avatarsList;
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/images/1/" + QString::number(avatar) + ".png?v=" + version)));
    connect(reply, &QNetworkReply::finished, this, [reply, list, item, avatar]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QPixmap image;
        if (reply->error() != QNetworkReply::NoError || !list) return;
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i) != item || item->data(Qt::UserRole).toInt() != avatar || item->data(ImageRole).toByteArray() == data) continue;
            if (!image.loadFromData(data)) return;
            item->setData(ImageRole, data);
            item->setIcon(TileGrid::icon(TileGrid::cover(image, TileGrid::SquareImage)));
            item->setToolTip(QString());
        }
    });
}

void WorldsDialog::useAvatar() {
    int avatar = selectedAvatar();
    if (avatar != 0) activateAvatar(avatar, {});
}

void WorldsDialog::editAvatar() {
    QListWidgetItem *item = ui->avatarsList->currentItem();
    if (!item || item->data(ActiveRole).toBool())
        launch("avatar", 0);
    else
        activateAvatar(selectedAvatar(), [this]() { launch("avatar", 0); });
}

void WorldsDialog::activateAvatar(int avatar, std::function<void()> done) {
    QUrl url(LocalServerUrl + "/api/avatars/active");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(avatar));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, avatar, done]() {
        reply->deleteLater();
        bool ok = reply->error() == QNetworkReply::NoError;
        if (!ok)
            QMessageBox::warning(this, tr("Avatars"), tr("Could not switch to this avatar"));
        else
            PlayerProfile::refresh(this);
        loadAvatars(avatar);
        if (ok && done) done();
    });
}

void WorldsDialog::importAvatar() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Import avatars"), QString(), tr("KoGaMa and ReGaMa avatars (*.kgavatar *.json *.bin);;All files (*)"));
    if (!paths.isEmpty()) importAvatars(paths, {});
}

void WorldsDialog::importAvatars(QStringList paths, QStringList failed) {
    if (paths.isEmpty()) {
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("Import"), tr("These file(s) are not avatar(s): %1", nullptr, failed.size()).arg(failed.join(", ")));
        return;
    }
    const QString path = paths.takeFirst();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        importAvatars(paths, failed << QFileInfo(path).fileName());
        return;
    }

    QNetworkRequest request(QUrl(LocalServerUrl + "/api/avatars/import"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_net->post(request, file.readAll());
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, paths, failed]() mutable {
        reply->deleteLater();
        int id = QJsonDocument::fromJson(reply->readAll()).object()["id"].toInt();
        if (id == 0) failed << QFileInfo(path).fileName();
        loadAvatars(id);
        importAvatars(paths, failed);
    });
}

void WorldsDialog::exportAvatar() {
    int avatar = selectedAvatar();
    if (avatar == 0) return;

    QString path = QFileDialog::getSaveFileName(this, tr("Export avatar"), ui->avatarsList->currentItem()->text() + ".kgavatar", tr("KoGaMa avatars (*.kgavatar)"));
    if (path.isEmpty()) return;

    QUrl url(LocalServerUrl + "/api/avatars/export");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(avatar));
    url.setQuery(query);

    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        QFile file(path);
        if (reply->error() != QNetworkReply::NoError || !file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0)
            QMessageBox::warning(this, tr("Export"), tr("Could not export this avatar"));
    });
}

void WorldsDialog::renameAvatar() {
    if (QListWidgetItem *item = ui->avatarsList->currentItem())
        ui->avatarsList->editItem(item);
}

void WorldsDialog::onAvatarRenamed(QListWidgetItem *item) {
    QString name = item->text().trimmed();
    QString previous = item->data(NameRole).toString();
    if (name == previous) return;
    if (name.isEmpty()) {
        QSignalBlocker blocker(ui->avatarsList);
        item->setText(previous);
        return;
    }

    item->setData(NameRole, name);
    updateAvatarActions();
    int id = item->data(Qt::UserRole).toInt();
    QUrl url(LocalServerUrl + "/api/avatars/rename");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(id));
    query.addQueryItem("name", name);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Rename"), tr("Could not rename this avatar"));
            loadAvatars(id);
        }
    });
}

void WorldsDialog::deleteAvatar() {
    QListWidgetItem *item = ui->avatarsList->currentItem();
    if (!item) return;

    if (item->data(ActiveRole).toBool()) {
        QMessageBox::information(this, tr("Delete avatar"), tr("You are using this avatar. Use another one first, then delete this one."));
        return;
    }
    auto answer = QMessageBox::question(this, tr("Delete avatar"),
        tr("Delete \"%1\"? This can't be undone.").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QUrl url(LocalServerUrl + "/api/avatars/delete");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(item->data(Qt::UserRole).toInt()));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            QMessageBox::warning(this, tr("Delete avatar"), tr("Could not delete this avatar"));
        loadAvatars();
    });
}

void WorldsDialog::showAvatarMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->avatarsList->itemAt(pos);
    if (!item) return;
    ui->avatarsList->setCurrentItem(item);

    QMenu menu(this);
    QAction *use = menu.addAction(tr("Use"));
    use->setEnabled(ui->useAvatarButton->isEnabled());
    menu.addSeparator();
    QAction *exported = menu.addAction(tr("Export..."));
    QAction *rename = menu.addAction(tr("Rename"));
    QAction *remove = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(ui->avatarsList->viewport()->mapToGlobal(pos));
    if (chosen == use) useAvatar();
    else if (chosen == rename) renameAvatar();
    else if (chosen == exported) exportAvatar();
    else if (chosen == remove) deleteAvatar();
}

int WorldsDialog::selectedAvatar() const {
    QListWidgetItem *item = ui->avatarsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void WorldsDialog::loadInventory(int select) {
    if (ui->inventoryList->count() == 0) {
        ui->inventoryStatusLabel->setText(tr("Loading your inventory..."));
        ui->inventoryStatusLabel->show();
    }
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/inventory")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select, url = LocalServerUrl]() {
        reply->deleteLater();
        if (url != LocalServerUrl) return;
        if (reply->error() != QNetworkReply::NoError) {
            ui->inventoryStatusLabel->setText(tr("Could not load your inventory. Press F5 to try again."));
            ui->inventoryStatusLabel->show();
            return;
        }
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll()).array();

        QList<int> ids;
        for (const QJsonValue &value : items)
            ids.append(value.toObject()["id"].toInt());
        QList<int> shown;
        for (int i = 0; i < ui->inventoryList->count(); ++i)
            shown.append(ui->inventoryList->item(i)->data(Qt::UserRole).toInt());
        QSignalBlocker blocker(ui->inventoryList);
        if (ids != shown) {
            ui->inventoryList->clear();
            const QIcon empty = TileGrid::icon(TileGrid::placeholder(TileGrid::SquareImage, palette().color(QPalette::Button)));
            for (int id : std::as_const(ids)) {
                auto *item = new QListWidgetItem(empty, QString(), ui->inventoryList);
                item->setFlags(item->flags() | Qt::ItemIsEditable);
                item->setData(Qt::UserRole, id);
            }
        }

        for (int i = 0; i < items.size(); ++i) {
            QListWidgetItem *item = ui->inventoryList->item(i);
            const QString name = items[i].toObject()["name"].toString();
            const QString text = name.isEmpty() || name == GameModelName ? tr("Model %1").arg(ids[i]) : name;
            if (item->text() != text) {
                item->setText(text);
                item->setData(NameRole, text);
            }
            loadItemPicture(item, ids[i]);
            if (ids[i] == select)
                ui->inventoryList->setCurrentItem(item);
        }
        if (!ui->inventoryList->currentItem() && ui->inventoryList->count() > 0)
            ui->inventoryList->setCurrentRow(0);
        filterInventory();
    });
}

void WorldsDialog::loadItemPicture(QListWidgetItem *item, int itemId) {
    QPointer<QListWidget> list = ui->inventoryList;
    const auto listed = [list, item, itemId]() {
        if (!list) return false;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i) == item) return item->data(Qt::UserRole).toInt() == itemId;
        return false;
    };

    QUrl url(LocalServerUrl + "/api/inventory/model");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(itemId));
    url.setQuery(query);
    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, item, listed]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || !listed() || item->data(ImageRole).toByteArray() == data) return;
        item->setData(ImageRole, data);
        updateInventoryActions();

        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, item, listed, data]() {
            watcher->deleteLater();
            const QImage image = watcher->result();
            if (image.isNull() || !listed() || item->data(ImageRole).toByteArray() != data) return;
            QPixmap tile = TileGrid::backdrop(TileGrid::SquareImage, palette().color(QPalette::Button));
            QPainter(&tile).drawImage(0, 0, image);
            item->setIcon(TileGrid::icon(tile));
        });
        watcher->setFuture(QtConcurrent::run([data]() { return ModelPreview::render(data, TileGrid::SquareImage); }));
    });
}

void WorldsDialog::filterInventory() {
    const QString search = ui->inventorySearchLineEdit->text().trimmed();
    QListWidgetItem *first = nullptr;
    int visible = 0;
    for (int i = 0; i < ui->inventoryList->count(); ++i) {
        auto *item = ui->inventoryList->item(i);
        bool matches = item->text().contains(search, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (matches) {
            ++visible;
            if (!first) first = item;
        }
    }
    auto *current = ui->inventoryList->currentItem();
    if (!current || current->isHidden())
        ui->inventoryList->setCurrentItem(first);
    int total = ui->inventoryList->count();
    ui->inventorySearchLineEdit->setPlaceholderText(total > 0 ? tr("Search %n item(s)...", nullptr, total) : tr("Search your inventory..."));
    ui->inventoryStatusLabel->setText(total == 0
        ? tr("No items yet. Import a model from openkogama.org/models, or add one to your inventory in build mode.")
        : visible == 0 ? tr("No items match your search.") : QString());
    ui->inventoryStatusLabel->setVisible(!ui->inventoryStatusLabel->text().isEmpty());
    updateInventoryActions();
}

void WorldsDialog::updateInventoryActions() {
    QListWidgetItem *item = ui->inventoryList->currentItem();
    const QByteArray model = item ? item->data(ImageRole).toByteArray() : QByteArray();
    const int cubes = ModelPreview::cubeCount(model);
    ui->moreItemButton->setEnabled(item != nullptr);
    ui->selectedItemLabel->setText(item ? item->text() : tr("No item selected"));
    ui->selectedItemDetail->setText(cubes > 0
        ? tr("%1 cube(s), scale %2", nullptr, cubes).arg(QLocale(QLocale::English).toString(cubes), QString::number(ModelPreview::scale(model)))
        : QString());
}

void WorldsDialog::importModel() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Import models"), QString(), tr("Models (*.kgmodel *.kscubes *.json *.bin);;All files (*)"));
    if (!paths.isEmpty()) importModels(paths, QString(), {}, {});
}

void WorldsDialog::importModels(QStringList paths, const QString &scale, QStringList failed, QStringList unscaled) {
    if (paths.isEmpty()) {
        if (!unscaled.isEmpty()) {
            const QString title = unscaled.size() == 1 ? tr("Import %1").arg(QFileInfo(unscaled.first()).completeBaseName()) : tr("Import %n model(s)", nullptr, unscaled.size());
            if (const auto chosen = askScale(this, title, tr("Import"), "0.25")) {
                importModels(unscaled, *chosen, failed, {});
                return;
            }
        }
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("Import"), tr("These file(s) are not model(s): %1", nullptr, failed.size()).arg(failed.join(", ")));
        return;
    }
    const QString path = paths.takeFirst();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        importModels(paths, scale, failed << QFileInfo(path).fileName(), unscaled);
        return;
    }

    QUrl url(LocalServerUrl + "/api/inventory/import");
    QUrlQuery query;
    query.addQueryItem("name", QFileInfo(path).completeBaseName());
    if (!scale.isEmpty()) query.addQueryItem("scale", scale);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    QNetworkReply *reply = m_net->post(request, file.readAll());
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, paths, scale, failed, unscaled]() mutable {
        reply->deleteLater();
        const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object();
        const int id = result["id"].toInt();
        if (result["needsScale"].toBool()) unscaled << path;
        else if (id == 0) failed << QFileInfo(path).fileName();
        loadInventory(id);
        importModels(paths, scale, failed, unscaled);
    });
}

void WorldsDialog::scaleItem() {
    QListWidgetItem *item = ui->inventoryList->currentItem();
    if (!item) return;
    const QByteArray model = item->data(ImageRole).toByteArray();
    if (model.isEmpty()) {
        QMessageBox::information(this, tr("Scale"), tr("Only models have a scale."));
        return;
    }
    const auto scale = askScale(this, tr("Scale %1").arg(item->text()), tr("Save"), QString::number(ModelPreview::scale(model)));
    if (!scale) return;

    const int id = item->data(Qt::UserRole).toInt();
    QUrl url(LocalServerUrl + "/api/inventory/scale");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(id));
    query.addQueryItem("scale", *scale);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            QMessageBox::warning(this, tr("Scale"), tr("Could not change the scale of this model"));
        loadInventory(id);
    });
}

void WorldsDialog::exportItem() {
    int itemId = selectedItem();
    if (itemId == 0) return;

    QString path = QFileDialog::getSaveFileName(this, tr("Export model"), ui->inventoryList->currentItem()->text() + ".kgmodel", tr("KoGaMa models (*.kgmodel)"));
    if (path.isEmpty()) return;

    QUrl url(LocalServerUrl + "/api/inventory/model");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(itemId));
    url.setQuery(query);

    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, path]() {
        reply->deleteLater();
        QFile file(path);
        if (reply->error() != QNetworkReply::NoError || !file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0)
            QMessageBox::warning(this, tr("Export"), tr("Could not export this item"));
    });
}

void WorldsDialog::renameItem() {
    if (QListWidgetItem *item = ui->inventoryList->currentItem())
        ui->inventoryList->editItem(item);
}

void WorldsDialog::onItemRenamed(QListWidgetItem *item) {
    QString name = item->text().trimmed();
    QString previous = item->data(NameRole).toString();
    if (name == previous) return;
    if (name.isEmpty()) {
        QSignalBlocker blocker(ui->inventoryList);
        item->setText(previous);
        return;
    }

    item->setData(NameRole, name);
    updateInventoryActions();
    int id = item->data(Qt::UserRole).toInt();
    QUrl url(LocalServerUrl + "/api/inventory/rename");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(id));
    query.addQueryItem("name", name);
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, tr("Rename"), tr("Could not rename this item"));
            loadInventory(id);
        }
    });
}

void WorldsDialog::deleteItem() {
    QListWidgetItem *item = ui->inventoryList->currentItem();
    if (!item) return;

    auto answer = QMessageBox::question(this, tr("Delete item"),
        tr("Delete \"%1\" from your inventory? This can't be undone.").arg(item->text()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QUrl url(LocalServerUrl + "/api/inventory/delete");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(item->data(Qt::UserRole).toInt()));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            QMessageBox::warning(this, tr("Delete item"), tr("Could not delete this item"));
        loadInventory();
    });
}

void WorldsDialog::showItemMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->inventoryList->itemAt(pos);
    if (!item) return;
    ui->inventoryList->setCurrentItem(item);

    QMenu menu(this);
    QAction *exported = menu.addAction(tr("Export..."));
    QAction *rename = menu.addAction(tr("Rename"));
    QAction *scale = menu.addAction(tr("Scale..."));
    QAction *remove = menu.addAction(tr("Delete"));

    QAction *chosen = menu.exec(ui->inventoryList->viewport()->mapToGlobal(pos));
    if (chosen == exported) exportItem();
    else if (chosen == rename) renameItem();
    else if (chosen == scale) scaleItem();
    else if (chosen == remove) deleteItem();
}

int WorldsDialog::selectedItem() const {
    QListWidgetItem *item = ui->inventoryList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

int WorldsDialog::selectedWorld() const {
    QListWidgetItem *item = ui->worldsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

QString WorldsDialog::hostedMode(int world) const {
    return world != 0 && world == m_hostedWorld ? guestMode() : QString();
}

void WorldsDialog::setHostedWorld(int world) {
    m_hostedWorld = world;
    updateWorldActions();
}

QProcess *WorldsDialog::launch(const QString &mode, int world) {
    if (mode != "avatar" && world == 0) {
        QMessageBox::information(this, tr("Worlds"), tr("Select a world first"));
        return nullptr;
    }
    QProcess *client = GameLauncher::start(m_path, m_exe, LocalServerUrl, mode, world, m_owner, this);
    if (!client) return nullptr;
    m_clients.append(client);
    connect(client, &QProcess::finished, this, [this, client]() { m_clients.removeAll(client); });
    return client;
}
