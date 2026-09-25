#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "installdialog.h"
#include "installprogressdialog.h"
#include "launchdialog.h"
#include "worldsdialog.h"
#include <QDesktopServices>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMessageBox>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("OpenKogama Launcher");

    connect(ui->actionInstall, &QAction::triggered, this, &MainWindow::onInstallTriggered);
    connect(ui->actionDiscord, &QAction::triggered, this, &MainWindow::onDiscordTriggered);
    connect(ui->actionLaunch, &QAction::triggered, this, &MainWindow::onLaunchTriggered);
    ui->actionDiscord->setIcon(QIcon(":/discord.png"));
    ui->actionInstall->setIcon(QIcon(":/install.png"));
    ui->actionLaunch->setIcon(QIcon(":/launch.png"));

    instances = new QStandardItemModel(this);
    ui->instancesView->setModel(instances);
    ui->instancesView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->instancesView->setViewMode(QListView::IconMode);
    ui->instancesView->setIconSize(QSize(64, 64));
    ui->instancesView->setGridSize(QSize(120, 100));
    ui->instancesView->setResizeMode(QListView::Adjust);
    ui->instancesView->setMovement(QListView::Static);
    ui->instancesView->setWordWrap(true);
    ui->instancesView->setUniformItemSizes(true);
    ui->instancesView->setFrameShape(QFrame::NoFrame);
    ui->instancesView->setFocusPolicy(Qt::NoFocus);
    ui->instancesView->setStyleSheet(
        "QListView { border: 0; outline: 0; background: #2b2b2b; }"
        "QListView::item { color: #dddddd; padding: 4px; }"
        "QListView::item:selected { background: #232323; border-radius: 6px; color: #ffffff; }");
    ui->instancesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->instancesView, &QListView::customContextMenuRequested, this, &MainWindow::showInstanceMenu);
    connect(ui->instancesView, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        launchInstance(index.data(Qt::UserRole).toString());
    });

    ui->menuBar->setStyleSheet(
        "QMenuBar { background: #2b2b2b; border-bottom: 1px solid #1a1a1a; }");

    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);

    loadInstances();

    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *reply = nam->get(QNetworkRequest(QUrl("https://cdn.openkogama.org/versions.json")));
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() == QNetworkReply::NoError)
            m_versionsJson = reply->readAll();
    });
}

void MainWindow::loadInstances() {
    instances->clear();

    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/instances";
    QDir dir(base);

    QFileIconProvider iconProvider;
    const auto folders = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &folder : folders) {
        QDir instanceDir(dir.filePath(folder));

        QFile f(instanceDir.filePath("instance.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonObject meta = QJsonDocument::fromJson(f.readAll()).object();
        QString name = meta.value("name").toString();
        QString version = meta.value("version").toString();

        QPixmap pix(":/unknown.png");
        const auto exes = instanceDir.entryList({"*.exe"}, QDir::Files);
        if (!exes.isEmpty())
            pix = iconProvider.icon(QFileInfo(instanceDir.filePath(exes.first()))).pixmap(64, 64);

        QIcon icon;
        icon.addPixmap(pix, QIcon::Normal);
        icon.addPixmap(pix, QIcon::Selected);

        auto *item = new QStandardItem(icon, name);
        item->setToolTip("KoGaMa " + version);
        item->setData(instanceDir.path(), Qt::UserRole);
        instances->appendRow(item);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onInstallTriggered() {
    if (m_versionsJson.isEmpty()) {
        QMessageBox::warning(this, "Error", "Version list not loaded yet");
        return;
    }

    InstallDialog dialog(m_versionsJson, this);
    if (dialog.exec() != QDialog::Accepted) return;

    InstallProgressDialog progress(dialog.selectedVersion(), dialog.instanceName(), this);
    if (progress.exec() == QDialog::Accepted)
        loadInstances();
}
void MainWindow::onDiscordTriggered() {
    QDesktopServices::openUrl(QUrl("https://discord.gg/u6tKuP3k4M"));
}

void MainWindow::onLaunchTriggered() {
    QModelIndex index = ui->instancesView->currentIndex();
    if (!index.isValid()) {
        QMessageBox::information(this, "Launch", "Select an instance first");
        return;
    }
    launchInstance(index.data(Qt::UserRole).toString());
}

void MainWindow::showInstanceMenu(const QPoint &pos) {
    QModelIndex index = ui->instancesView->indexAt(pos);
    if (!index.isValid()) return;
    ui->instancesView->setCurrentIndex(index);

    QMenu menu(this);
    QAction *launch = menu.addAction(QIcon(":/launch.png"), "Launch");
    if (menu.exec(ui->instancesView->viewport()->mapToGlobal(pos)) == launch)
        launchInstance(index.data(Qt::UserRole).toString());
}

void MainWindow::launchInstance(const QString &path) {
    QDir dir(path);
    QString data = dir.entryList({"*_Data"}, QDir::Dirs).value(0);
    if (data.isEmpty()) {
        QMessageBox::warning(this, "Launch", "This version is not a standalone build and can't be launched yet");
        return;
    }

    QString exe = data.chopped(5) + ".exe";
    if (!dir.exists(exe)) {
        QString other = dir.entryList({"*.exe"}, QDir::Files).value(0);
        if (other.isEmpty() || !dir.rename(other, exe)) {
            QMessageBox::warning(this, "Launch", "No game executable in " + path);
            return;
        }
    }

    LaunchDialog launch(this);
    if (launch.exec() != QDialog::Accepted) return;

    WorldsDialog worlds(path, exe, launch.server(), this);
    worlds.exec();
}
