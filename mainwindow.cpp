#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "installdialog.h"
#include "installprogressdialog.h"
#include "launchdialog.h"
#include "settingsdialog.h"
#include "worldsdialog.h"
#include "centereddelegate.h"
#include "webplayerruntime.h"
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
#include <QSslError>
#include <QMessageBox>
#include <QMenu>
#include <QShortcut>
#include <QDirIterator>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent>

static constexpr int NameRole = Qt::UserRole + 1;

static QString instancesBase() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/instances";
}

static bool saveInstanceName(const QString &path, const QString &name) {
    QFile file(QDir(path).filePath("instance.json"));
    QJsonObject meta;
    if (file.open(QIODevice::ReadOnly)) {
        meta = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
    }

    meta["name"] = name;
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(meta).toJson());
    return true;
}

static bool copyInstance(const QString &source, const QString &dest) {
    QDir from(source);
    QDir to(dest);
    if (!to.mkpath(".")) return false;

    QDirIterator it(source, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFileInfo entry(it.next());
        QString target = to.filePath(from.relativeFilePath(entry.filePath()));
        if (entry.isDir()) {
            if (!to.mkpath(target)) return false;
        } else if (!QFile::copy(entry.filePath(), target)) {
            return false;
        }
    }
    return true;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle("OpenKogama Launcher");

    connect(ui->actionInstall, &QAction::triggered, this, &MainWindow::onInstallTriggered);
    connect(ui->actionDiscord, &QAction::triggered, this, &MainWindow::onDiscordTriggered);
    connect(ui->actionLaunch, &QAction::triggered, this, &MainWindow::onLaunchTriggered);
    connect(ui->actionSettings, &QAction::triggered, this, [this]() { SettingsDialog(this).exec(); });
    ui->actionDiscord->setIcon(QIcon(":/discord.png"));
    ui->actionInstall->setIcon(QIcon(":/install.png"));
    ui->actionLaunch->setIcon(QIcon(":/launch.png"));
    ui->actionLaunch->setText(QString());
    ui->actionLaunch->setToolTip("Launch");

    instances = new QStandardItemModel(this);
    ui->instancesView->setModel(instances);
    ui->instancesView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->instancesView->setViewMode(QListView::IconMode);
    ui->instancesView->setIconSize(QSize(64, 64));
    ui->instancesView->setGridSize(QSize(140, 100));
    ui->instancesView->setResizeMode(QListView::Adjust);
    ui->instancesView->setMovement(QListView::Static);
    ui->instancesView->setWordWrap(true);
    ui->instancesView->setUniformItemSizes(true);
    ui->instancesView->setFrameShape(QFrame::NoFrame);
    ui->instancesView->setFocusPolicy(Qt::NoFocus);
    ui->instancesView->setTextElideMode(Qt::ElideNone);
    ui->instancesView->setStyleSheet(
        "QListView { border: 0; outline: 0; background: #2b2b2b; }"
        "QListView::item { color: #dddddd; padding: 4px; border-radius: 6px; }"
        "QListView::item:hover { background: #1f1f1f; color: #ffffff; }"
        "QListView::item:selected { background: #181818; border-radius: 6px; color: #ffffff; }");
    ui->instancesView->setItemDelegate(new CenteredDelegate(ui->instancesView));
    ui->instancesView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->instancesView, &QListView::customContextMenuRequested, this, &MainWindow::showInstanceMenu);
    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), ui->instancesView);
    connect(renameShortcut, &QShortcut::activated, this, &MainWindow::renameInstance);
    connect(instances, &QStandardItemModel::itemChanged, this, &MainWindow::onInstanceRenamed);
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, ui->instancesView);
    connect(deleteShortcut, &QShortcut::activated, this, &MainWindow::deleteInstance);

    connect(ui->instancesView, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        QString dataPath = index.data(Qt::UserRole).toString();
        if (dataPath == "action_add_instance") {
            onInstallTriggered();
        } else {
            launchInstance(dataPath);
        }
    });

    ui->instancesView->setMouseTracking(true);
    ui->instancesView->viewport()->installEventFilter(this);

    ui->menuBar->setStyleSheet(
        "QMenuBar { background: #2b2b2b; border-bottom: 1px solid #1a1a1a; }");

    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);

    loadInstances();
    fetchVersions();
}

void MainWindow::fetchVersions(std::function<void()> done) {
    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *reply = nam->get(QNetworkRequest(QUrl("https://cdn.openkogama.org/versions.json")));
    auto sslErrors = std::make_shared<QStringList>();
    connect(reply, &QNetworkReply::sslErrors, this, [sslErrors](const QList<QSslError> &errors) {
        for (const QSslError &error : errors)
            sslErrors->append(error.errorString());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, sslErrors, done]() {
        reply->deleteLater();
        nam->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_versionsJson = reply->readAll();
            m_versionsError.clear();
        } else {
            m_versionsError = reply->errorString();
            if (!sslErrors->isEmpty())
                m_versionsError += "" + sslErrors->join("");
        }
        if (done) done();
    });
}

void MainWindow::loadInstances() {
    instances->clear();

    QIcon plusIcon;
    QPixmap plusPix(":/lemonplus.png");
    plusIcon.addPixmap(plusPix, QIcon::Normal);
    plusIcon.addPixmap(plusPix, QIcon::Selected);

    auto *plusItem = new QStandardItem(plusIcon, "Add New");
    plusItem->setToolTip("Click here to install new KoGaMa instance.");
    plusItem->setData("action_add_instance", Qt::UserRole);
    instances->appendRow(plusItem);

    QDir dir(instancesBase());

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
        item->setData(name, NameRole);
        instances->appendRow(item);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::onInstallTriggered() {
    if (m_versionsJson.isEmpty()) {
        ui->actionInstall->setEnabled(false);
        fetchVersions([this]() {
            ui->actionInstall->setEnabled(true);
            if (m_versionsJson.isEmpty())
                QMessageBox::warning(this, "Error", "Could not load the version list:\n" + m_versionsError);
            else
                onInstallTriggered();
        });
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
    if (index.data(Qt::UserRole).toString() == "action_add_instance") {
        onInstallTriggered();
    } else {
        launchInstance(index.data(Qt::UserRole).toString());
    }
}

void MainWindow::showInstanceMenu(const QPoint &pos) {
    QModelIndex index = ui->instancesView->indexAt(pos);

    if (index.isValid() && index.data(Qt::UserRole).toString() == "action_add_instance") {
        return;
    }

    if (!index.isValid()) {
        ui->instancesView->clearSelection();
        QMenu menu(this);
        menu.addAction(ui->actionInstall);
        menu.exec(ui->instancesView->viewport()->mapToGlobal(pos));
        return;
    }
    ui->instancesView->setCurrentIndex(index);

    QMenu menu(this);
    QAction *launch = menu.addAction(QIcon(":/launch.png"), "Launch");
    menu.addSeparator();
    QAction *rename = menu.addAction("Rename");
    QAction *duplicate = menu.addAction("Duplicate");
    QAction *remove = menu.addAction("Delete");

    QAction *chosen = menu.exec(ui->instancesView->viewport()->mapToGlobal(pos));
    if (chosen == launch)
        launchInstance(index.data(Qt::UserRole).toString());
    else if (chosen == rename)
        renameInstance();
    else if (chosen == duplicate)
        duplicateInstance();
    else if (chosen == remove)
        deleteInstance();
}

void MainWindow::renameInstance() {
    QModelIndex index = ui->instancesView->currentIndex();
    if (index.isValid() && index.data(Qt::UserRole).toString() == "action_add_instance") return;

    if (index.isValid())
        ui->instancesView->edit(index);
}

void MainWindow::onInstanceRenamed(QStandardItem *item) {
    QString name = item->text().trimmed();
    QString previous = item->data(NameRole).toString();
    if (name == previous) return;
    if (name.isEmpty()) {
        item->setText(previous);
        return;
    }

    if (!saveInstanceName(item->data(Qt::UserRole).toString(), name)) {
        QMessageBox::warning(this, "Rename", "Could not save instance.json");
        item->setText(previous);
        return;
    }
    item->setData(name, NameRole);
}

void MainWindow::duplicateInstance() {
    QModelIndex index = ui->instancesView->currentIndex();
    if (!index.isValid() || index.data(Qt::UserRole).toString() == "action_add_instance") return;

    QString source = index.data(Qt::UserRole).toString();
    QString name = index.data(NameRole).toString() + " (copy)";
    QString folder = QFileInfo(source).fileName() + " (copy)";
    QString dest = instancesBase() + "/" + folder;
    for (int n = 2; QDir(dest).exists(); n++)
        dest = instancesBase() + "/" + folder + " (" + QString::number(n) + ")";

    auto *progress = new QProgressDialog("Duplicating " + index.data(NameRole).toString() + "...", QString(), 0, 0, this);
    progress->setWindowTitle("Duplicate Instance");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->show();

    auto *watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, progress, dest, name]() {
        progress->deleteLater();
        watcher->deleteLater();
        if (!watcher->result() || !saveInstanceName(dest, name)) {
            QDir(dest).removeRecursively();
            QMessageBox::warning(this, "Duplicate Instance", "Could not copy the instance files");
            return;
        }
        loadInstances();
    });
    watcher->setFuture(QtConcurrent::run([source, dest]() { return copyInstance(source, dest); }));
}

void MainWindow::deleteInstance() {
    QModelIndex index = ui->instancesView->currentIndex();
    if (!index.isValid() || index.data(Qt::UserRole).toString() == "action_add_instance") return;

    auto answer = QMessageBox::question(this, "Delete Instance",
                                        "Delete \"" + index.data().toString() + "\" and all its files? This can't be undone.",
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    QDir dir(index.data(Qt::UserRole).toString());
    if (QDir::cleanPath(dir.absolutePath()).startsWith(QDir::cleanPath(instancesBase()) + "/") && !dir.removeRecursively())
        QMessageBox::warning(this, "Delete Instance", "Some files could not be deleted. Close the game if it is running and try again.");

    loadInstances();
}

void MainWindow::launchInstance(const QString &path) {
    QDir dir(path);
    QString exe = dir.entryList({"*.unityweb", "*.unity3d"}, QDir::Files).value(0);

    if (!exe.isEmpty()) {
        if (!QFile::exists(WebPlayerRuntime::playerPath())) {
            QMessageBox::warning(this, "Launch", "OpenKogama Player is missing from " + QDir::toNativeSeparators(WebPlayerRuntime::playerPath()));
            return;
        }
        if (!WebPlayerRuntime::ensureInstalled(this))
            return;
    } else {
        QString data = dir.entryList({"*_Data"}, QDir::Dirs).value(0);
        if (data.isEmpty()) {
            QMessageBox::warning(this, "Launch", "This version can't be launched yet");
            return;
        }

        exe = data.chopped(5) + ".exe";
        if (!dir.exists(exe)) {
            QString other = dir.entryList({"*.exe"}, QDir::Files).value(0);
            if (other.isEmpty() || !dir.rename(other, exe)) {
                QMessageBox::warning(this, "Launch", "No game executable in " + path);
                return;
            }
        }
    }

    LaunchDialog launch(this);
    if (launch.exec() != QDialog::Accepted) return;

    auto *worlds = new WorldsDialog(path, exe, launch.server(), this);
    worlds->setAttribute(Qt::WA_DeleteOnClose);
    worlds->setModal(false);
    worlds->show();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == ui->instancesView->viewport() && event->type() == QEvent::MouseMove) {
        QPoint pos = ui->instancesView->viewport()->mapFromGlobal(QCursor::pos());
        QModelIndex index = ui->instancesView->indexAt(pos);
        if (index.isValid()) {
            ui->instancesView->viewport()->setCursor(Qt::PointingHandCursor);
        } else {
            ui->instancesView->viewport()->setCursor(Qt::ArrowCursor);
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
