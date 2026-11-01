#include "updater.h"
#include "launchdialog.h"
#include <QAction>
#include <QApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QtConcurrent>
#include <Velopack.hpp>

namespace {

const std::string Repository = "https://github.com/openkogama/openkogama-launcher";
constexpr int ShutdownMs = 2000;
constexpr int SaveMs = 4000;
constexpr int KillMs = 3000;

bool isLocalServer(const QProcess *process)
{
    const QString first = process->arguments().value(0);
    return QFileInfo(process->program()).fileName() == "openkogama-server.exe" && first != "tunnel" && first != "install";
}

void stopChildren(QWidget *window)
{
    QNetworkAccessManager net;
    QNetworkRequest request(QUrl(LocalServerUrl + "/shutdown"));
    request.setTransferTimeout(ShutdownMs);
    QNetworkReply *reply = net.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();
    const QList<QProcess *> processes = window->findChildren<QProcess *>();
    QDeadlineTimer saving(SaveMs);
    for (QProcess *process : processes)
        if (process->state() != QProcess::NotRunning && isLocalServer(process))
            process->waitForFinished(int(saving.remainingTime()));
    for (QProcess *process : processes) {
        if (process->state() == QProcess::NotRunning) continue;
        process->kill();
        process->waitForFinished(KillMs);
    }
}

struct Update {
    std::shared_ptr<Velopack::UpdateManager> manager;
    std::optional<Velopack::UpdateInfo> info;
    QString error;
};

struct Progress {
    QPointer<QAction> action;
    QString version;
    int shown = -1;
};

std::shared_ptr<Velopack::UpdateManager> managerFor(bool prerelease) {
    return std::make_shared<Velopack::UpdateManager>(std::make_unique<Velopack::GithubSource>(Repository, "", prerelease));
}

void label(const QPointer<QAction> &action, const QString &text) {
    QMetaObject::invokeMethod(qApp, [action, text]() {
        if (action) action->setText(text);
    }, Qt::QueuedConnection);
}

void reportProgress(void *data, size_t percent) {
    auto *progress = static_cast<Progress *>(data);
    if (int(percent) == progress->shown) return;
    progress->shown = int(percent);
    label(progress->action, QCoreApplication::translate("Updater", "Downloading %1... %2%").arg(progress->version).arg(progress->shown));
}

Update download(QPointer<QAction> action) {
    Update update;
    try {
        update.manager = managerFor(false);
    } catch (const std::exception &error) {
        update.error = QString::fromUtf8(error.what());
        return update;
    }
    try {
        if (update.manager->GetCurrentVersion().find('-') != std::string::npos)
            update.manager = managerFor(true);
        update.info = update.manager->CheckForUpdates();
        if (update.info) {
            Progress progress{action, QString::fromStdString(update.info->TargetFullRelease.Version)};
            reportProgress(&progress, 0);
            update.manager->DownloadUpdates(*update.info, reportProgress, &progress);
        }
    } catch (const std::exception &error) {
        qWarning("Update failed: %s", error.what());
        update.error = QString::fromUtf8(error.what());
        update.info.reset();
    }
    return update;
}

}

void Updater::startup() {
    Velopack::VelopackApp::Build().Run();
}

QString Updater::version() {
    try {
        return QString::fromUtf8(managerFor(false)->GetCurrentVersion().c_str());
    } catch (const std::exception &) {
        return {};
    }
}

void Updater::check(QWidget *window, QAction *action, bool asked) {
    static bool busy = false;
    if (busy) return;
    busy = true;
    const QString idle = action->text();
    QPointer<QAction> guard(action);
    action->setEnabled(false);
    action->setText(QCoreApplication::translate("Updater", "Checking for updates..."));
    auto *watcher = new QFutureWatcher<Update>(window);
    QObject::connect(watcher, &QFutureWatcher<Update>::finished, window, [watcher, window, guard, idle, asked]() {
        busy = false;
        Update update = watcher->result();
        watcher->deleteLater();
        if (guard) {
            guard->setText(idle);
            guard->setEnabled(true);
        }
        const QString title = QCoreApplication::translate("Updater", "Update");
        if (!update.info) {
            if (!asked) return;
            if (update.error.isEmpty())
                QMessageBox::information(window, title, QCoreApplication::translate("Updater", "You have the newest version (%1).").arg(version()));
            else
                QMessageBox::warning(window, title, QCoreApplication::translate("Updater", "Could not check for updates: %1").arg(update.error));
            return;
        }
        QString target = QString::fromStdString(update.info->TargetFullRelease.Version);
        if (QMessageBox::question(window, title, QCoreApplication::translate("Updater", "OpenKogama Launcher %1 is ready. Restart now to update?").arg(target)) != QMessageBox::Yes)
            return;
        try {
            stopChildren(window);
            update.manager->WaitExitThenApplyUpdates(*update.info);
            QApplication::quit();
        } catch (const std::exception &error) {
            QMessageBox::warning(window, title, QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run(download, guard));
}
