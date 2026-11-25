#include "worldsdialog.h"
#include "ui_worldsdialog.h"
#include "archivebrowser.h"
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
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFutureWatcher>
#include <QLocale>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <optional>
#include <QKeyEvent>
#include <QStyle>
#include <QStackedWidget>
#include <QTabBar>
#include <QPixmap>
#include <QPainter>
#include <QTimer>
#include <QGuiApplication>
#include <QStyleHints>
#include <algorithm>

static constexpr int NameRole = Qt::UserRole + 1;
static constexpr int ImageRole = Qt::UserRole + 2;
static constexpr int ActiveRole = Qt::UserRole + 3;
static constexpr int PlatformerRole = Qt::UserRole + 4;
static constexpr int PluginsTimeout = 120000;
static constexpr int MaxConcurrentModels = 4;
static const QUrl MapsArchive("https://cdn.openkogama.org/maps/v1/");
static const QString GameModelName("CubeModel");

static void syncList(QListWidget *list, const QList<int> &ids, const QIcon &placeholder, const QString &defaultTip = {}) {
    QHash<int, QListWidgetItem *> existing;
    existing.reserve(list->count());
    while (list->count() > 0) {
        QListWidgetItem *item = list->takeItem(0);
        const int id = item->data(Qt::UserRole).toInt();
        if (existing.contains(id)) delete item;
        else existing.insert(id, item);
    }
    for (int id : ids) {
        QListWidgetItem *item = existing.take(id);
        if (!item) {
            item = new QListWidgetItem(placeholder, QString());
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            item->setData(Qt::UserRole, id);
            if (!defaultTip.isEmpty()) item->setToolTip(defaultTip);
        }
        list->addItem(item);
    }
    qDeleteAll(existing);
}

static QListWidgetItem *findItemById(QListWidget *list, int id) {
    if (!list) return nullptr;
    for (int i = 0; i < list->count(); ++i) {
        QListWidgetItem *item = list->item(i);
        if (item->data(Qt::UserRole).toInt() == id) return item;
    }
    return nullptr;
}

static QList<QListWidgetItem *> visibleSelected(QListWidget *list) {
    QList<QListWidgetItem *> result;
    if (!list) return result;
    for (QListWidgetItem *item : list->selectedItems())
        if (!item->isHidden()) result.append(item);
    if (result.isEmpty()) {
        if (QListWidgetItem *current = list->currentItem(); current && !current->isHidden())
            result.append(current);
    }
    return result;
}

static QString safeFileName(QString name) {
    name.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("item") : name;
}

static QString uniqueFilePath(const QString &dir, const QString &base, const QString &ext) {
    QString path = QDir(dir).filePath(base + ext);
    for (int n = 2; QFile::exists(path); ++n)
        path = QDir(dir).filePath(base + " (" + QString::number(n) + ")" + ext);
    return path;
}

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

static QString platformerTip()
{
    return QCoreApplication::translate("WorldsDialog", "Side view on game versions 1.25 to 2.5. Newer versions play the world in 3D.");
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
    , m_mediaNet(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    m_net->setTransferTimeout(8000);
    m_mediaNet->setTransferTimeout(8000);
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
    QAction *renameWorldAction = worldMenu->addAction(tr("Rename"), this, &WorldsDialog::renameWorld);
    QAction *platformer = worldMenu->addAction(tr("Switch to 2D"), this, [this]() {
        if (QListWidgetItem *item = ui->worldsList->currentItem()) setPlatformer(!item->data(PlatformerRole).toBool());
    });
    connect(worldMenu, &QMenu::aboutToShow, this, [this, renameWorldAction, platformer]() {
        const QList<QListWidgetItem *> sel = visibleSelected(ui->worldsList);
        const bool single = sel.size() == 1;
        renameWorldAction->setEnabled(single);
        platformer->setEnabled(single);
        platformer->setText(single && sel.first()->data(PlatformerRole).toBool() ? tr("Switch to 3D") : tr("Switch to 2D"));
    });
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
    auto wrapWithArchive = [this](QWidget *tab, const QString &barName, const QString &localLabel, const QString &localTip,
                                  const QString &archiveTip, ArchiveBrowser::Kind kind,
                                  QTabBar **outBar, QStackedWidget **outStack, ArchiveBrowser **outBrowser) -> QWidget * {
        auto *outer = qobject_cast<QVBoxLayout *>(tab->layout());
        auto *local = new QWidget(tab);
        auto *inner = new QVBoxLayout(local);
        inner->setContentsMargins(0, 0, 0, 0);
        inner->setSpacing(8);
        while (QLayoutItem *child = outer->takeAt(0)) {
            if (QWidget *w = child->widget()) {
                inner->addWidget(w, w == ui->worldsList || w == ui->avatarsList || w == ui->inventoryList ? 1 : 0);
                delete child;
            } else if (QLayout *sub = child->layout()) {
                inner->addLayout(sub);
            } else {
                inner->addItem(child);
            }
        }
        auto *bar = new QTabBar(tab);
        bar->setObjectName(barName);
        bar->setDrawBase(false);
        bar->setExpanding(false);
        bar->addTab(localLabel);
        bar->addTab(tr("Archive"));
        bar->setTabToolTip(0, localTip);
        bar->setTabToolTip(1, archiveTip);

        auto *stack = new QStackedWidget(tab);
        auto *browser = new ArchiveBrowser(kind, stack);
        stack->addWidget(local);
        stack->addWidget(browser);

        outer->addWidget(bar);
        outer->addWidget(stack, 1);

        connect(bar, &QTabBar::currentChanged, this, [stack, browser](int index) {
            stack->setCurrentIndex(index);
            if (index == 1) browser->ensureLoaded();
        });
        connect(browser, &ArchiveBrowser::importRequested, this, &WorldsDialog::importRequested);

        *outBar = bar;
        *outStack = stack;
        *outBrowser = browser;
        return local;
    };

    QWidget *localWorlds = wrapWithArchive(ui->worldsTab, "worldsViewTabs", tr("My worlds"),
        tr("Worlds saved on your computer"), tr("Browse and download archived KoGaMa maps"),
        ArchiveBrowser::Maps, &m_worldsView, &m_worldsStack, &m_mapsArchive);
    QWidget *localAvatars = wrapWithArchive(ui->avatarsTab, "avatarsViewTabs", tr("My avatars"),
        tr("Avatars saved on your computer"), tr("Browse and download archived KoGaMa avatars"),
        ArchiveBrowser::Avatars, &m_avatarsView, &m_avatarsStack, &m_avatarsArchive);
    QWidget *localInventory = wrapWithArchive(ui->inventoryTab, "inventoryViewTabs", tr("My inventory"),
        tr("Models saved on your computer"), tr("Browse and download archived KoGaMa models"),
        ArchiveBrowser::Models, &m_inventoryView, &m_inventoryStack, &m_modelsArchive);

    auto *refreshWorlds = new QShortcut(QKeySequence::Refresh, localWorlds);
    refreshWorlds->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshWorlds, &QShortcut::activated, this, [this]() { loadWorlds(selectedWorld(), true); });
    auto *refreshAvatars = new QShortcut(QKeySequence::Refresh, localAvatars);
    refreshAvatars->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshAvatars, &QShortcut::activated, this, [this]() { loadAvatars(selectedAvatar(), true); });
    connect(ui->worldSearchLineEdit, &QLineEdit::textChanged, this, [this]() { filterWorlds(); });
    connect(ui->avatarSearchLineEdit, &QLineEdit::textChanged, this, [this]() { filterAvatars(); });
    connect(ui->worldsList, &QListWidget::currentItemChanged, this, [this]() { updateWorldActions(); });
    connect(ui->worldsList, &QListWidget::itemSelectionChanged, this, [this]() { updateWorldActions(); });
    connect(ui->avatarsList, &QListWidget::currentItemChanged, this, [this]() { updateAvatarActions(); });
    connect(ui->avatarsList, &QListWidget::itemSelectionChanged, this, [this]() { updateAvatarActions(); });
    connect(ui->worldsList, &QListWidget::itemActivated, this, [this]() { launch(ui->playButton->isEnabled() ? "play" : "edit", selectedWorld()); });

    connect(ui->useAvatarButton, &QPushButton::clicked, this, &WorldsDialog::useAvatar);
    connect(ui->importAvatarButton, &QPushButton::clicked, this, &WorldsDialog::importAvatar);
    auto *avatarMenu = new QMenu(this);
    avatarMenu->addAction(tr("Export..."), this, &WorldsDialog::exportAvatar);
    QAction *renameAvatarAction = avatarMenu->addAction(tr("Rename"), this, &WorldsDialog::renameAvatar);
    connect(avatarMenu, &QMenu::aboutToShow, this, [this, renameAvatarAction]() {
        renameAvatarAction->setEnabled(visibleSelected(ui->avatarsList).size() == 1);
    });
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
    connect(ui->inventoryList, &QListWidget::itemSelectionChanged, this, [this]() { updateInventoryActions(); });
    auto *itemMenu = new QMenu(this);
    itemMenu->addAction(tr("Export..."), this, &WorldsDialog::exportItem);
    QAction *renameItemAction = itemMenu->addAction(tr("Rename"), this, &WorldsDialog::renameItem);
    QAction *scaleItemAction = itemMenu->addAction(tr("Scale..."), this, &WorldsDialog::scaleItem);
    connect(itemMenu, &QMenu::aboutToShow, this, [this, renameItemAction, scaleItemAction]() {
        const bool single = visibleSelected(ui->inventoryList).size() == 1;
        renameItemAction->setEnabled(single);
        scaleItemAction->setEnabled(single);
    });
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
    auto *refreshInventory = new QShortcut(QKeySequence::Refresh, localInventory);
    refreshInventory->setContext(Qt::WidgetWithChildrenShortcut);
    connect(refreshInventory, &QShortcut::activated, this, [this]() { loadInventory(selectedItem(), true); });
    connect(ui->inventoryList, &QListWidget::customContextMenuRequested, this, &WorldsDialog::showItemMenu);

    TileGrid::setup(ui->worldsList, this);
    TileGrid::setup(ui->avatarsList, this);
    TileGrid::setup(ui->inventoryList, this);
    ui->worldsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->avatarsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->inventoryList->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
            m_pendingModels.clear();
            m_queuedModels.clear();
            m_inventoryGeneration++;
            m_pendingAvatars.clear();
            m_queuedAvatars.clear();
            m_avatarGeneration++;
            loadWorlds(world, true);
            loadAvatars(avatar, true);
            loadInventory(item, true);
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

void WorldsDialog::showWorlds(int select) {
    if (select > 0 && m_worldsView) m_worldsView->setCurrentIndex(0);
    loadWorlds(select < 0 ? selectedWorld() : select, select < 0);
}

void WorldsDialog::showAvatars(int select) {
    if (select > 0 && m_avatarsView) m_avatarsView->setCurrentIndex(0);
    loadAvatars(select < 0 ? selectedAvatar() : select, false);
}

void WorldsDialog::showInventory(int select) {
    if (select > 0 && m_inventoryView) m_inventoryView->setCurrentIndex(0);
    loadInventory(select < 0 ? selectedItem() : select, false);
}

void WorldsDialog::refreshAfterImport(const QString &kind, int select) {
    if (kind == "map") loadWorlds(select, false);
    else if (kind == "avatar") loadAvatars(select, false);
    else loadInventory(select, false);
}

void WorldsDialog::setArchiveThumbnail(int world, const QUrl &url) {
    if (world > 0 && url.isValid()) downloadArchiveThumbnail(world, url);
}

bool WorldsDialog::hasRunningGames() const {
    return std::any_of(m_clients.cbegin(), m_clients.cend(), [](const QPointer<QProcess> &client) {
        return client && client->state() != QProcess::NotRunning;
    });
}

void WorldsDialog::updateWorldActions() {
    const QList<QListWidgetItem *> sel = visibleSelected(ui->worldsList);
    const int count = int(sel.size());
    const bool single = count == 1;
    const QString guests = single ? hostedMode(sel.first()->data(Qt::UserRole).toInt()) : QString();
    ui->playButton->setEnabled(single && guests != "edit");
    ui->hostButton->setEnabled(single);
    ui->buildButton->setEnabled(single && guests != "play");
    ui->playButton->setToolTip(guests == "edit" ? tr("Your guests join in build (Server settings)") : QString());
    ui->buildButton->setToolTip(guests == "play" ? tr("Your guests join in play (Server settings)") : QString());
    ui->moreWorldButton->setEnabled(count > 0);
    ui->selectedWorldLabel->setText(count == 0 ? tr("No world selected")
        : single ? sel.first()->text() : tr("%n selected", nullptr, count));
}

void WorldsDialog::filterWorlds() {
    const QString search = ui->worldSearchLineEdit->text().trimmed();
    QListWidgetItem *first = nullptr;
    int visible = 0;
    for (int i = 0; i < ui->worldsList->count(); ++i) {
        auto *item = ui->worldsList->item(i);
        bool matches = item->text().contains(search, Qt::CaseInsensitive);
        item->setHidden(!matches);
        if (!matches && item->isSelected()) item->setSelected(false);
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
        if (!matches && item->isSelected()) item->setSelected(false);
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
    const QList<QListWidgetItem *> sel = visibleSelected(ui->avatarsList);
    const int count = int(sel.size());
    const bool single = count == 1;
    QListWidgetItem *item = single ? sel.first() : nullptr;
    const bool active = item && item->data(ActiveRole).toBool();
    ui->useAvatarButton->setEnabled(single && !active);
    ui->editAvatarButton->setEnabled(m_hasAvatarEditor && count <= 1);
    ui->moreAvatarButton->setEnabled(count > 0);
    ui->selectedAvatarLabel->setText(count == 0 ? tr("No avatar selected")
        : single ? item->text() : tr("%n selected", nullptr, count));
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

void WorldsDialog::loadWorlds(int select, bool refreshPictures) {
    if (ui->worldsList->count() == 0) {
        ui->worldStatusLabel->setText(tr("Loading your worlds..."));
        ui->worldStatusLabel->show();
    }
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/worlds")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select, refreshPictures, url = LocalServerUrl]() {
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
            const QIcon empty = TileGrid::icon(TileGrid::placeholder(TileGrid::WideImage, palette().color(QPalette::Button)));
            syncList(ui->worldsList, ids, empty);
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
            item->setData(PlatformerRole, world["platformer"].toBool());
            if (ids[i] == select)
                ui->worldsList->setCurrentItem(item);
            if (refreshPictures || item->data(ImageRole).toByteArray().isEmpty())
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
            loadWorlds(selectedWorld(), true);
    });
}

void WorldsDialog::loadThumbnail(QListWidgetItem *item, int world) {
    QPointer<QListWidget> list = ui->worldsList;
    QNetworkReply *reply = m_mediaNet->get(QNetworkRequest(QUrl(LocalServerUrl + "/images/0/" + QString::number(world) + ".png?v=" + QString::number(m_revision))));
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
    QNetworkRequest request(QUrl(LocalServerUrl + "/api/templates"));
    request.setTransferTimeout(PluginsTimeout);
    QNetworkReply *reply = m_net->get(request);
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
    auto *type = new QComboBox(&dialog);
    type->addItem(tr("3D"), false);
    type->addItem(tr("2D"), true);
    type->setItemData(1, platformerTip(), Qt::ToolTipRole);

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
    form->addRow(tr("Type:"), type);
    layout->addLayout(form);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return;

    QUrl url(LocalServerUrl + "/api/worlds");
    QUrlQuery query;
    query.addQueryItem("name", name->text().trimmed());
    if (templates->currentIndex() >= 0)
        query.addQueryItem("template", templates->currentData().toString());
    if (type->currentData().toBool())
        query.addQueryItem("platformer", "1");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    request.setTransferTimeout(PluginsTimeout);
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

void WorldsDialog::importWorlds(QStringList paths, QStringList failed, int lastId) {
    if (paths.isEmpty()) {
        if (lastId > 0) loadWorlds(lastId);
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("Import"), tr("These map(s) could not be imported: %1", nullptr, failed.size()).arg(failed.join(", ")));
        return;
    }
    const QString path = paths.takeFirst();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        importWorlds(paths, failed << QFileInfo(path).fileName(), lastId);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, path, paths, failed, lastId]() mutable {
        reply->deleteLater();
        int id = QJsonDocument::fromJson(reply->readAll()).object()["id"].toInt();
        if (id == 0) failed << QFileInfo(path).fileName();
        else {
            lastId = id;
            findArchiveThumbnail(id, data);
        }
        importWorlds(paths, failed, lastId);
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
    QNetworkReply *reply = m_mediaNet->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, world]() {
        reply->deleteLater();
        QImage image;
        QByteArray png;
        QBuffer buffer(&png);
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll())
            || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return;
        QNetworkRequest request(QUrl(LocalServerUrl + "/api/worlds/thumbnail?id=" + QString::number(world)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "image/png");
        QNetworkReply *upload = m_mediaNet->post(request, png);
        connect(upload, &QNetworkReply::finished, this, [this, upload, world]() {
            upload->deleteLater();
            if (upload->error() != QNetworkReply::NoError) return;
            if (QListWidgetItem *item = findItemById(ui->worldsList, world)) {
                item->setData(ImageRole, QByteArray());
                loadThumbnail(item, world);
            }
        });
    });
}

void WorldsDialog::exportWorld() {
    const QList<QListWidgetItem *> sel = visibleSelected(ui->worldsList);
    if (sel.isEmpty()) return;

    if (sel.size() > 1) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Export worlds"));
        if (dir.isEmpty()) return;
        QList<QPair<int, QString>> batch;
        for (QListWidgetItem *item : sel)
            batch.append({item->data(Qt::UserRole).toInt(), safeFileName(item->data(NameRole).toString())});
        exportBatch("/api/worlds/export", batch, dir, ".kgmap");
        return;
    }

    QListWidgetItem *item = sel.first();
    const QString name = safeFileName(item->data(NameRole).toString());
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
    const QList<QListWidgetItem *> sel = visibleSelected(ui->worldsList);
    if (sel.isEmpty()) return;

    const QString question = sel.size() == 1
        ? tr("Delete \"%1\"? This can't be undone.").arg(sel.first()->text())
        : tr("Delete %n world(s)? This can't be undone.", nullptr, int(sel.size()));
    auto answer = QMessageBox::question(this, tr("Delete world"), question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QList<int> ids;
    for (QListWidgetItem *item : sel)
        ids.append(item->data(Qt::UserRole).toInt());
    deleteBatch("/api/worlds/delete", ids, tr("Delete world"),
        tr("Some worlds could not be deleted because they are open in the game. Close them first."),
        [this]() { loadWorlds(); });
}

void WorldsDialog::setPlatformer(bool on) {
    QListWidgetItem *item = ui->worldsList->currentItem();
    if (!item) return;

    QUrl url(LocalServerUrl + "/api/worlds/platformer");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(item->data(Qt::UserRole).toInt()));
    query.addQueryItem("on", on ? "1" : "0");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        loadWorlds(selectedWorld());
    });
}

void WorldsDialog::showWorldMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->worldsList->itemAt(pos);
    if (!item) return;
    if (!item->isSelected()) {
        ui->worldsList->clearSelection();
        ui->worldsList->setCurrentItem(item);
        item->setSelected(true);
    }
    const QList<QListWidgetItem *> sel = visibleSelected(ui->worldsList);
    const bool single = sel.size() == 1;

    QMenu menu(this);
    QAction *play = nullptr;
    QAction *build = nullptr;
    QAction *rename = nullptr;
    QAction *platformer = nullptr;
    bool platformerWorld = false;
    if (single) {
        play = menu.addAction(tr("Play"));
        build = menu.addAction(tr("Build"));
        play->setEnabled(ui->playButton->isEnabled());
        build->setEnabled(ui->buildButton->isEnabled());
        menu.addSeparator();
    }
    QAction *exported = menu.addAction(single ? tr("Export...") : tr("Export %n world(s)...", nullptr, int(sel.size())));
    if (single) {
        rename = menu.addAction(tr("Rename"));
        platformerWorld = item->data(PlatformerRole).toBool();
        platformer = menu.addAction(platformerWorld ? tr("Switch to 3D") : tr("Switch to 2D"));
    }
    QAction *remove = menu.addAction(single ? tr("Delete") : tr("Delete %n world(s)", nullptr, int(sel.size())));

    QAction *chosen = menu.exec(ui->worldsList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == play) launch("play", selectedWorld());
    else if (chosen == build) launch("edit", selectedWorld());
    else if (chosen == exported) exportWorld();
    else if (chosen == rename) renameWorld();
    else if (chosen == platformer) setPlatformer(!platformerWorld);
    else if (chosen == remove) deleteWorld();
}

void WorldsDialog::loadAvatars(int select, bool refreshPictures) {
    if (ui->avatarsList->count() == 0) {
        ui->avatarStatusLabel->setText(tr("Loading your avatars..."));
        ui->avatarStatusLabel->show();
    }
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/avatars")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select, refreshPictures, url = LocalServerUrl]() {
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
        if (refreshPictures) {
            m_pendingAvatars.clear();
            m_queuedAvatars.clear();
            m_avatarGeneration++;
        }
        if (ids != shown) {
            const QIcon empty = TileGrid::icon(TileGrid::placeholder(TileGrid::SquareImage, palette().color(QPalette::Button)));
            syncList(ui->avatarsList, ids, empty, tr("No picture yet, take one with the camera in the avatar editor"));
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
            if (refreshPictures || active || item->data(ImageRole).toByteArray().isEmpty())
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
    QNetworkReply *reply = m_mediaNet->get(QNetworkRequest(QUrl(LocalServerUrl + "/images/1/" + QString::number(avatar) + ".png?v=" + version)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, list, item, avatar]() {
        reply->deleteLater();
        QByteArray data = reply->readAll();
        QPixmap image;
        if (!list) return;
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(data)) {
            if (QListWidgetItem *found = findItemById(list, avatar); found && found->data(ImageRole).toByteArray().isEmpty())
                enqueueAvatarPreview(avatar);
            return;
        }
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i) != item || item->data(Qt::UserRole).toInt() != avatar || item->data(ImageRole).toByteArray() == data) continue;
            item->setData(ImageRole, data);
            item->setIcon(TileGrid::icon(TileGrid::cover(image, TileGrid::SquareImage)));
            item->setToolTip(QString());
        }
    });
}

void WorldsDialog::enqueueAvatarPreview(int avatar) {
    if (m_queuedAvatars.contains(avatar)) return;
    m_queuedAvatars.insert(avatar);
    m_pendingAvatars.enqueue(avatar);
    fetchNextAvatarPreview();
}

void WorldsDialog::fetchNextAvatarPreview() {
    while (m_activeAvatars < MaxConcurrentModels && !m_pendingAvatars.isEmpty()) {
        const int avatar = m_pendingAvatars.dequeue();
        if (!findItemById(ui->avatarsList, avatar)) {
            m_queuedAvatars.remove(avatar);
            continue;
        }
        renderAvatarPreview(avatar);
    }
}

void WorldsDialog::renderAvatarPreview(int avatar) {
    m_activeAvatars++;
    const int gen = m_avatarGeneration;
    QUrl url(LocalServerUrl + "/api/avatars/export");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(avatar));
    url.setQuery(query);

    QNetworkReply *reply = m_mediaNet->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, avatar, gen]() {
        reply->deleteLater();
        auto done = [this, avatar, gen]() {
            if (gen == m_avatarGeneration)
                m_queuedAvatars.remove(avatar);
            m_activeAvatars = qMax(0, m_activeAvatars - 1);
            fetchNextAvatarPreview();
        };
        const QByteArray data = reply->readAll();
        QListWidgetItem *item = findItemById(ui->avatarsList, avatar);
        if (gen != m_avatarGeneration || reply->error() != QNetworkReply::NoError || !item || data.isEmpty()) {
            done();
            return;
        }
        if (item->data(ImageRole).toByteArray() == data) {
            done();
            return;
        }
        item->setData(ImageRole, data);

        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, avatar, gen, data, done]() {
            watcher->deleteLater();
            const QImage image = watcher->result();
            QListWidgetItem *target = findItemById(ui->avatarsList, avatar);
            if (gen == m_avatarGeneration && !image.isNull() && target && target->data(ImageRole).toByteArray() == data) {
                QPixmap tile = TileGrid::backdrop(TileGrid::SquareImage, palette().color(QPalette::Button));
                QPainter(&tile).drawImage(0, 0, image);
                target->setIcon(TileGrid::icon(tile));
                target->setToolTip(QString());
            }
            done();
        });
        watcher->setFuture(QtConcurrent::run([data]() { return ModelPreview::renderAvatar(data, TileGrid::SquareImage); }));
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

void WorldsDialog::importAvatars(QStringList paths, QStringList failed, int lastId) {
    if (paths.isEmpty()) {
        if (lastId > 0) loadAvatars(lastId);
        if (!failed.isEmpty())
            QMessageBox::warning(this, tr("Import"), tr("These file(s) are not avatar(s): %1", nullptr, failed.size()).arg(failed.join(", ")));
        return;
    }
    const QString path = paths.takeFirst();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        importAvatars(paths, failed << QFileInfo(path).fileName(), lastId);
        return;
    }

    QNetworkRequest request(QUrl(LocalServerUrl + "/api/avatars/import"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = m_net->post(request, file.readAll());
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, paths, failed, lastId]() mutable {
        reply->deleteLater();
        int id = QJsonDocument::fromJson(reply->readAll()).object()["id"].toInt();
        if (id == 0) failed << QFileInfo(path).fileName();
        else lastId = id;
        importAvatars(paths, failed, lastId);
    });
}

void WorldsDialog::exportAvatar() {
    const QList<QListWidgetItem *> sel = visibleSelected(ui->avatarsList);
    if (sel.isEmpty()) return;

    if (sel.size() > 1) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Export avatars"));
        if (dir.isEmpty()) return;
        QList<QPair<int, QString>> batch;
        for (QListWidgetItem *item : sel)
            batch.append({item->data(Qt::UserRole).toInt(), safeFileName(item->text())});
        exportBatch("/api/avatars/export", batch, dir, ".kgavatar");
        return;
    }

    QListWidgetItem *item = sel.first();
    int avatar = item->data(Qt::UserRole).toInt();
    QString path = QFileDialog::getSaveFileName(this, tr("Export avatar"), safeFileName(item->text()) + ".kgavatar", tr("KoGaMa avatars (*.kgavatar)"));
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
    const QList<QListWidgetItem *> sel = visibleSelected(ui->avatarsList);
    if (sel.isEmpty()) return;

    QList<int> ids;
    bool skippedActive = false;
    for (QListWidgetItem *item : sel) {
        if (item->data(ActiveRole).toBool()) skippedActive = true;
        else ids.append(item->data(Qt::UserRole).toInt());
    }
    if (ids.isEmpty()) {
        QMessageBox::information(this, tr("Delete avatar"), tr("You are using this avatar. Use another one first, then delete this one."));
        return;
    }

    QString question = ids.size() == 1 && sel.size() == 1
        ? tr("Delete \"%1\"? This can't be undone.").arg(sel.first()->text())
        : tr("Delete %n avatar(s)? This can't be undone.", nullptr, int(ids.size()));
    if (skippedActive)
        question += "\n\n" + tr("Your currently active avatar will be kept.");
    auto answer = QMessageBox::question(this, tr("Delete avatar"), question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    deleteBatch("/api/avatars/delete", ids, tr("Delete avatar"), tr("Could not delete some avatars."),
        [this]() { loadAvatars(); });
}

void WorldsDialog::showAvatarMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->avatarsList->itemAt(pos);
    if (!item) return;
    if (!item->isSelected()) {
        ui->avatarsList->clearSelection();
        ui->avatarsList->setCurrentItem(item);
        item->setSelected(true);
    }
    const QList<QListWidgetItem *> sel = visibleSelected(ui->avatarsList);
    const bool single = sel.size() == 1;

    QMenu menu(this);
    QAction *use = nullptr;
    QAction *rename = nullptr;
    if (single) {
        use = menu.addAction(tr("Use"));
        use->setEnabled(ui->useAvatarButton->isEnabled());
        menu.addSeparator();
    }
    QAction *exported = menu.addAction(single ? tr("Export...") : tr("Export %n avatar(s)...", nullptr, int(sel.size())));
    if (single)
        rename = menu.addAction(tr("Rename"));
    QAction *remove = menu.addAction(single ? tr("Delete") : tr("Delete %n avatar(s)", nullptr, int(sel.size())));

    QAction *chosen = menu.exec(ui->avatarsList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    if (chosen == use) useAvatar();
    else if (chosen == rename) renameAvatar();
    else if (chosen == exported) exportAvatar();
    else if (chosen == remove) deleteAvatar();
}

int WorldsDialog::selectedAvatar() const {
    QListWidgetItem *item = ui->avatarsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void WorldsDialog::loadInventory(int select, bool refreshPictures) {
    if (ui->inventoryList->count() == 0) {
        ui->inventoryStatusLabel->setText(tr("Loading your inventory..."));
        ui->inventoryStatusLabel->show();
    }
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(LocalServerUrl + "/api/inventory")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select, refreshPictures, url = LocalServerUrl]() {
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
        if (refreshPictures) {
            m_pendingModels.clear();
            m_queuedModels.clear();
            m_inventoryGeneration++;
        }
        if (ids != shown) {
            const QIcon empty = TileGrid::icon(TileGrid::placeholder(TileGrid::SquareImage, palette().color(QPalette::Button)));
            syncList(ui->inventoryList, ids, empty);
        }

        for (int i = 0; i < items.size(); ++i) {
            QListWidgetItem *item = ui->inventoryList->item(i);
            const QString name = items[i].toObject()["name"].toString();
            const QString text = name.isEmpty() || name == GameModelName ? tr("Model %1").arg(ids[i]) : name;
            if (item->text() != text) {
                item->setText(text);
                item->setData(NameRole, text);
            }
            if (refreshPictures || item->data(ImageRole).toByteArray().isEmpty())
                enqueueItemPicture(ids[i]);
            if (ids[i] == select)
                ui->inventoryList->setCurrentItem(item);
        }
        if (!ui->inventoryList->currentItem() && ui->inventoryList->count() > 0)
            ui->inventoryList->setCurrentRow(0);
        filterInventory();
        fetchNextItemPicture();
    });
}

void WorldsDialog::enqueueItemPicture(int itemId) {
    if (m_queuedModels.contains(itemId)) return;
    m_queuedModels.insert(itemId);
    m_pendingModels.enqueue(itemId);
}

void WorldsDialog::fetchNextItemPicture() {
    while (m_activeModels < MaxConcurrentModels && !m_pendingModels.isEmpty()) {
        const int itemId = m_pendingModels.dequeue();
        if (!findItemById(ui->inventoryList, itemId)) {
            m_queuedModels.remove(itemId);
            continue;
        }
        loadItemPicture(itemId);
    }
}

void WorldsDialog::loadItemPicture(int itemId) {
    m_activeModels++;
    const int gen = m_inventoryGeneration;
    QUrl url(LocalServerUrl + "/api/inventory/model");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(itemId));
    url.setQuery(query);
    QNetworkReply *reply = m_mediaNet->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, itemId, gen]() {
        reply->deleteLater();
        auto done = [this, itemId, gen]() {
            if (gen == m_inventoryGeneration)
                m_queuedModels.remove(itemId);
            m_activeModels = qMax(0, m_activeModels - 1);
            fetchNextItemPicture();
        };
        const QByteArray data = reply->readAll();
        QListWidgetItem *item = findItemById(ui->inventoryList, itemId);
        if (gen != m_inventoryGeneration || reply->error() != QNetworkReply::NoError || !item || data.isEmpty()) {
            done();
            return;
        }
        if (item->data(ImageRole).toByteArray() == data) {
            done();
            return;
        }
        item->setData(ImageRole, data);
        if (item == ui->inventoryList->currentItem())
            updateInventoryActions();

        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, itemId, gen, data, done]() {
            watcher->deleteLater();
            const QImage image = watcher->result();
            QListWidgetItem *target = findItemById(ui->inventoryList, itemId);
            if (gen == m_inventoryGeneration && !image.isNull() && target && target->data(ImageRole).toByteArray() == data) {
                QPixmap tile = TileGrid::backdrop(TileGrid::SquareImage, palette().color(QPalette::Button));
                QPainter(&tile).drawImage(0, 0, image);
                target->setIcon(TileGrid::icon(tile));
            }
            done();
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
        if (!matches && item->isSelected()) item->setSelected(false);
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
    const QList<QListWidgetItem *> sel = visibleSelected(ui->inventoryList);
    const int count = int(sel.size());
    const bool single = count == 1;
    QListWidgetItem *item = single ? sel.first() : nullptr;
    const QByteArray model = item ? item->data(ImageRole).toByteArray() : QByteArray();
    const int cubes = single ? ModelPreview::cubeCount(model) : 0;
    ui->moreItemButton->setEnabled(count > 0);
    ui->selectedItemLabel->setText(count == 0 ? tr("No item selected")
        : single ? item->text() : tr("%n selected", nullptr, count));
    ui->selectedItemDetail->setText(single && cubes > 0
        ? tr("%1 cube(s), scale %2", nullptr, cubes).arg(QLocale(QLocale::English).toString(cubes), QString::number(ModelPreview::scale(model)))
        : QString());
}

void WorldsDialog::importModel() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Import models"), QString(), tr("Models (*.kgmodel *.kscubes *.json *.bin);;All files (*)"));
    if (!paths.isEmpty()) importModels(paths, QString(), {}, {});
}

void WorldsDialog::importModels(QStringList paths, const QString &scale, QStringList failed, QStringList unscaled, int lastId) {
    if (paths.isEmpty()) {
        if (lastId > 0) loadInventory(lastId);
        if (!unscaled.isEmpty()) {
            const QString title = unscaled.size() == 1 ? tr("Import %1").arg(QFileInfo(unscaled.first()).completeBaseName()) : tr("Import %n model(s)", nullptr, unscaled.size());
            if (const auto chosen = askScale(this, title, tr("Import"), "0.25")) {
                importModels(unscaled, *chosen, failed, {}, lastId);
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
        importModels(paths, scale, failed << QFileInfo(path).fileName(), unscaled, lastId);
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, paths, scale, failed, unscaled, lastId]() mutable {
        reply->deleteLater();
        const QJsonObject result = QJsonDocument::fromJson(reply->readAll()).object();
        const int id = result["id"].toInt();
        if (result["needsScale"].toBool()) unscaled << path;
        else if (id == 0) failed << QFileInfo(path).fileName();
        else lastId = id;
        importModels(paths, scale, failed, unscaled, lastId);
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
        else if (QListWidgetItem *scaled = findItemById(ui->inventoryList, id))
            scaled->setData(ImageRole, QByteArray());
        loadInventory(id);
    });
}

void WorldsDialog::exportItem() {
    const QList<QListWidgetItem *> sel = visibleSelected(ui->inventoryList);
    if (sel.isEmpty()) return;

    if (sel.size() > 1) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Export models"));
        if (dir.isEmpty()) return;
        QList<QPair<int, QString>> batch;
        for (QListWidgetItem *item : sel)
            batch.append({item->data(Qt::UserRole).toInt(), safeFileName(item->text())});
        exportBatch("/api/inventory/model", batch, dir, ".kgmodel");
        return;
    }

    QListWidgetItem *item = sel.first();
    int itemId = item->data(Qt::UserRole).toInt();
    QString path = QFileDialog::getSaveFileName(this, tr("Export model"), safeFileName(item->text()) + ".kgmodel", tr("KoGaMa models (*.kgmodel)"));
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
    const QList<QListWidgetItem *> sel = visibleSelected(ui->inventoryList);
    if (sel.isEmpty()) return;

    const QString question = sel.size() == 1
        ? tr("Delete \"%1\" from your inventory? This can't be undone.").arg(sel.first()->text())
        : tr("Delete %n item(s) from your inventory? This can't be undone.", nullptr, int(sel.size()));
    auto answer = QMessageBox::question(this, tr("Delete item"), question,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QList<int> ids;
    for (QListWidgetItem *item : sel)
        ids.append(item->data(Qt::UserRole).toInt());
    deleteBatch("/api/inventory/delete", ids, tr("Delete item"), tr("Could not delete some items."),
        [this]() { loadInventory(); });
}

void WorldsDialog::deleteBatch(const QString &endpoint, QList<int> ids, const QString &errorTitle, const QString &errorMessage, std::function<void()> onDone, int failed) {
    if (ids.isEmpty()) {
        if (failed > 0)
            QMessageBox::warning(this, errorTitle, errorMessage);
        if (onDone) onDone();
        return;
    }
    const int id = ids.takeFirst();
    QUrl url(LocalServerUrl + endpoint);
    QUrlQuery query;
    query.addQueryItem("id", QString::number(id));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, endpoint, ids, errorTitle, errorMessage, onDone, failed]() mutable {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) failed++;
        deleteBatch(endpoint, ids, errorTitle, errorMessage, onDone, failed);
    });
}

void WorldsDialog::exportBatch(const QString &endpoint, QList<QPair<int, QString>> items, const QString &dir, const QString &ext, int failed) {
    if (items.isEmpty()) {
        if (failed > 0)
            QMessageBox::warning(this, tr("Export"), tr("Could not export %n item(s).", nullptr, failed));
        return;
    }
    const auto [id, baseName] = items.takeFirst();
    QUrl url(LocalServerUrl + endpoint);
    QUrlQuery query;
    query.addQueryItem("id", QString::number(id));
    url.setQuery(query);

    QNetworkReply *reply = m_net->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, endpoint, items, dir, ext, baseName, failed]() mutable {
        reply->deleteLater();
        QFile file(uniqueFilePath(dir, baseName, ext));
        if (reply->error() != QNetworkReply::NoError || !file.open(QIODevice::WriteOnly) || file.write(reply->readAll()) < 0)
            failed++;
        exportBatch(endpoint, items, dir, ext, failed);
    });
}

void WorldsDialog::showItemMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->inventoryList->itemAt(pos);
    if (!item) return;
    if (!item->isSelected()) {
        ui->inventoryList->clearSelection();
        ui->inventoryList->setCurrentItem(item);
        item->setSelected(true);
    }
    const QList<QListWidgetItem *> sel = visibleSelected(ui->inventoryList);
    const bool single = sel.size() == 1;

    QMenu menu(this);
    QAction *exported = menu.addAction(single ? tr("Export...") : tr("Export %n item(s)...", nullptr, int(sel.size())));
    QAction *rename = nullptr;
    QAction *scale = nullptr;
    if (single) {
        rename = menu.addAction(tr("Rename"));
        scale = menu.addAction(tr("Scale..."));
    }
    QAction *remove = menu.addAction(single ? tr("Delete") : tr("Delete %n item(s)", nullptr, int(sel.size())));

    QAction *chosen = menu.exec(ui->inventoryList->viewport()->mapToGlobal(pos));
    if (!chosen) return;
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
