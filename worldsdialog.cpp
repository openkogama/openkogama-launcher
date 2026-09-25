#include "worldsdialog.h"
#include "ui_worldsdialog.h"
#include "launchdialog.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>

WorldsDialog::WorldsDialog(const QString &path, const QString &exe, QProcess *server, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::WorldsDialog)
    , m_path(path)
    , m_exe(exe)
    , m_server(server)
    , m_net(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/launch.png"));
    setWindowTitle("Worlds - KoGaMa " + QDir(path).dirName());

    connect(ui->playButton, &QPushButton::clicked, this, [this]() { launch("play", selectedWorld()); });
    connect(ui->buildButton, &QPushButton::clicked, this, [this]() { launch("edit", selectedWorld()); });
    connect(ui->avatarButton, &QPushButton::clicked, this, [this]() { launch("avatar", 0); });
    connect(ui->newButton, &QPushButton::clicked, this, &WorldsDialog::createWorld);
    connect(ui->importButton, &QPushButton::clicked, this, &WorldsDialog::importWorld);
    connect(ui->closeButton, &QPushButton::clicked, this, &WorldsDialog::reject);
    connect(ui->worldsList, &QListWidget::itemDoubleClicked, this, [this]() { launch("play", selectedWorld()); });

    loadWorlds();
    loadTemplates();
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
        ui->worldsList->clear();

        const QJsonArray worlds = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &value : worlds) {
            QJsonObject world = value.toObject();
            QString saved = QDateTime::fromString(world["savedAt"].toString(), Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm");
            auto *item = new QListWidgetItem(world["name"].toString() + "\n" + saved, ui->worldsList);
            item->setData(Qt::UserRole, world["id"].toInt());
            if (world["id"].toInt() == select)
                ui->worldsList->setCurrentItem(item);
        }
        if (!ui->worldsList->currentItem() && ui->worldsList->count() > 0)
            ui->worldsList->setCurrentRow(0);
    });
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

    auto *client = new QProcess(parentWidget());
    client->setProgram(QDir(m_path).filePath(m_exe));
    client->setArguments({"kogamaPackage:" + QString::fromLatin1(session.toUtf8().toBase64())});
    client->setWorkingDirectory(m_path);
    client->setProcessEnvironment(env);
    connect(client, &QProcess::finished, client, &QObject::deleteLater);
    client->start();

    if (!client->waitForStarted()) {
        client->deleteLater();
        QMessageBox::warning(this, "Worlds", "Could not start " + m_exe);
    }
}
