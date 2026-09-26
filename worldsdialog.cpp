#include "worldsdialog.h"
#include "ui_worldsdialog.h"
#include "launchdialog.h"
#include "centereddelegate.h"
#include "webplayerruntime.h"
#include <QPixmap>
#include <QTimer>

static const QSize ThumbnailSize(200, 80);
static constexpr int NameRole = Qt::UserRole + 1;
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QShortcut>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>

WorldsDialog::WorldsDialog(const QString &path, const QString &exe, QProcess *server, QWidget *parent)
    : QDialog(nullptr)
    , ui(new Ui::WorldsDialog)
    , m_owner(parent)
    , m_path(path)
    , m_exe(exe)
    , m_server(server)
    , m_net(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    setWindowIcon(QIcon(":/launch.png"));
    setWindowTitle("Worlds - KoGaMa " + QDir(path).dirName());

    connect(ui->playButton, &QPushButton::clicked, this, [this]() { launch("play", selectedWorld()); });
    connect(ui->buildButton, &QPushButton::clicked, this, [this]() { launch("edit", selectedWorld()); });
    connect(ui->avatarButton, &QPushButton::clicked, this, [this]() { launch("avatar", 0); });
    connect(ui->newButton, &QPushButton::clicked, this, &WorldsDialog::createWorld);
    connect(ui->importButton, &QPushButton::clicked, this, &WorldsDialog::importWorld);
    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), ui->worldsList);
    connect(renameShortcut, &QShortcut::activated, this, &WorldsDialog::renameWorld);
    connect(ui->worldsList, &QListWidget::itemChanged, this, &WorldsDialog::onWorldRenamed);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, ui->worldsList);
    connect(deleteShortcut, &QShortcut::activated, this, &WorldsDialog::deleteWorld);
    ui->worldsList->setItemDelegate(new CenteredDelegate(ui->worldsList));
    ui->worldsList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->worldsList, &QListWidget::customContextMenuRequested, this, &WorldsDialog::showWorldMenu);
    connect(ui->closeButton, &QPushButton::clicked, this, &WorldsDialog::reject);
    ui->worldsList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(ui->worldsList, &QListWidget::itemDoubleClicked, this, [this]() { launch("play", selectedWorld()); });

    ui->worldsList->setViewMode(QListView::IconMode);
    ui->worldsList->setIconSize(ThumbnailSize);
    ui->worldsList->setGridSize(QSize(ThumbnailSize.width() + 20, ThumbnailSize.height() + 40));
    ui->worldsList->setResizeMode(QListView::Adjust);
    ui->worldsList->setMovement(QListView::Static);
    ui->worldsList->setWordWrap(true);
    ui->worldsList->setUniformItemSizes(true);
    ui->worldsList->setFrameShape(QFrame::NoFrame);
    ui->worldsList->setFocusPolicy(Qt::NoFocus);
    ui->worldsList->setStyleSheet(
        "QListView { border: 0; outline: 0; background: #2b2b2b; }"
        "QListView::item { color: #dddddd; padding: 4px; }"
        "QListView::item:selected { background: #232323; border-radius: 6px; color: #ffffff; }");

    loadWorlds();
    loadTemplates();

    auto *poll = new QTimer(this);
    connect(poll, &QTimer::timeout, this, &WorldsDialog::checkRevision);
    poll->start(1000);
}

WorldsDialog::~WorldsDialog()
{
    delete ui;
}

void WorldsDialog::reject() {
    if (m_server)
        LaunchDialog::stopServer(m_server);
    QDialog::reject();
}

void WorldsDialog::loadWorlds(int select) {
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(ServerUrl + "/api/worlds")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, select]() {
        reply->deleteLater();
        QSignalBlocker blocker(ui->worldsList);
        ui->worldsList->clear();

        QPixmap empty(ThumbnailSize);
        empty.fill(QColor("#3a3a3a"));

        const QJsonArray worlds = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &value : worlds) {
            QJsonObject world = value.toObject();
            int id = world["id"].toInt();
            auto *item = new QListWidgetItem(thumbnail(empty), world["name"].toString(), ui->worldsList);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            item->setData(Qt::UserRole, id);
            item->setData(NameRole, world["name"].toString());
            auto date = [](const QJsonValue &value) {
                return QDateTime::fromString(value.toString(), Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm");
            };
            item->setToolTip("Saved: " + date(world["savedAt"]) + "\n"
                +(world["publishedAt"].isString() ? "Published: " + date(world["publishedAt"]) : QString("Not published")));
            if (id == select)
                ui->worldsList->setCurrentItem(item);
            loadThumbnail(item, id);
        }
        if (!ui->worldsList->currentItem() && ui->worldsList->count() > 0)
            ui->worldsList->setCurrentRow(0);
    });
}

void WorldsDialog::checkRevision() {
    QNetworkRequest request(QUrl(ServerUrl + "/api/worlds/revision"));
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
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(ServerUrl + "/images/0/" + QString::number(world) + ".png?v=" + QString::number(m_revision))));
    connect(reply, &QNetworkReply::finished, this, [reply, list, item, world]() {
        reply->deleteLater();
        QPixmap image;
        if (reply->error() != QNetworkReply::NoError || !image.loadFromData(reply->readAll()) || !list) return;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i) == item && item->data(Qt::UserRole).toInt() == world)
                item->setIcon(thumbnail(image.scaled(ThumbnailSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)));
    });
}

QIcon WorldsDialog::thumbnail(const QPixmap &pixmap) {
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    icon.addPixmap(pixmap, QIcon::Selected);
    return icon;
}

void WorldsDialog::loadTemplates() {
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(ServerUrl + "/api/templates")));
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
    dialog.setWindowTitle("New World");

    auto *name = new QLineEdit("New World", &dialog);
    auto *templates = new QComboBox(&dialog);
    for (const auto &[id, title] : std::as_const(m_templates))
        templates->addItem(title, id);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *form = new QFormLayout(&dialog);
    form->addRow("Name:", name);
    form->addRow("Template:", templates);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return;

    QUrl url(ServerUrl + "/api/worlds");
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
    QString path = QFileDialog::getOpenFileName(this, "Import World", QString(), "KoGaMa maps (*.kgmap *.kgm)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Import", "Could not read " + path);
        return;
    }

    QUrl url(ServerUrl + "/api/worlds/import");
    QUrlQuery query;
    query.addQueryItem("name", QFileInfo(path).completeBaseName());
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    QNetworkReply *reply = m_net->post(request, file.readAll());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        int id = QJsonDocument::fromJson(reply->readAll()).object()["id"].toInt();
        if (id == 0) QMessageBox::warning(this, "Import", "This map could not be imported");
        loadWorlds(id);
    });
}

void WorldsDialog::renameWorld() {
    if (QListWidgetItem *item = ui->worldsList->currentItem())
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
    int id = item->data(Qt::UserRole).toInt();
    QUrl url(ServerUrl + "/api/worlds/rename");
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
            QMessageBox::warning(this, "Rename", "Could not rename this world");
            loadWorlds(id);
        }
    });
}

void WorldsDialog::deleteWorld() {
    QListWidgetItem *item = ui->worldsList->currentItem();
    if (!item) return;

    auto answer = QMessageBox::question(this, "Delete World",
        "Delete \"" + item->text() + "\"? This can't be undone.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QUrl url(ServerUrl + "/api/worlds/delete");
    QUrlQuery query;
    query.addQueryItem("id", QString::number(item->data(Qt::UserRole).toInt()));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    QNetworkReply *reply = m_net->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            QMessageBox::warning(this, "Delete World", "This world is open in the game. Close it first.");
        loadWorlds();
    });
}

void WorldsDialog::showWorldMenu(const QPoint &pos) {
    QListWidgetItem *item = ui->worldsList->itemAt(pos);
    if (!item) return;
    ui->worldsList->setCurrentItem(item);

    QMenu menu(this);
    QAction *play = menu.addAction("Play");
    QAction *build = menu.addAction("Build");
    menu.addSeparator();
    QAction *rename = menu.addAction("Rename");
    QAction *remove = menu.addAction("Delete");

    QAction *chosen = menu.exec(ui->worldsList->viewport()->mapToGlobal(pos));
    if (chosen == play) launch("play", selectedWorld());
    else if (chosen == build) launch("edit", selectedWorld());
    else if (chosen == rename) renameWorld();
    else if (chosen == remove) deleteWorld();
}

int WorldsDialog::selectedWorld() const {
    QListWidgetItem *item = ui->worldsList->currentItem();
    return item ? item->data(Qt::UserRole).toInt() : 0;
}

void WorldsDialog::launch(const QString &mode, int world) {
    if (mode != "avatar" && world == 0) {
        QMessageBox::information(this, "Worlds", "Select a world first");
        return;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("http_proxy", "http://127.0.0.1:8081");
    env.insert("no_proxy", "127.0.0.1,localhost");

    QString session = ServerUrl + "/session?mode=" + mode + (world ? "&world=" + QString::number(world) : QString());

    auto *client = new QProcess(m_owner);
    QDir logs(QDir(m_path).filePath("logs"));
    logs.mkpath(".");
    QString log = logs.filePath("client-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz") + ".log");

    if (WebPlayerRuntime::isWebPlayerFile(m_exe)) {
        QFile meta(QDir(m_path).filePath("instance.json"));
        meta.open(QIODevice::ReadOnly);
        QString version = QJsonDocument::fromJson(meta.readAll()).object().value("version").toString();
        client->setProgram(WebPlayerRuntime::playerPath());
        client->setArguments({QDir::toNativeSeparators(QDir(m_path).filePath(m_exe)), "--version", version,
            "--reply", "sendPlayerParams=" + session, "--title", "KoGaMa " + QDir(m_path).dirName(), "--log", QDir::toNativeSeparators(log)});
    } else {
        client->setProgram(QDir(m_path).filePath(m_exe));
        client->setArguments({"kogamaPackage:" + QString::fromLatin1(session.toUtf8().toBase64()), "-logFile", QDir::toNativeSeparators(log)});
    }
    client->setWorkingDirectory(m_path);
    client->setProcessEnvironment(env);
    connect(client, &QProcess::finished, client, &QObject::deleteLater);
    client->start();

    if (!client->waitForStarted()) {
        client->deleteLater();
        QMessageBox::warning(this, "Worlds", "Could not start " + m_exe);
    }
}
