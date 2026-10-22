#include "updater.h"
#include "launchdialog.h"
#include <QApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
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
};

std::shared_ptr<Velopack::UpdateManager> managerFor(bool prerelease) {
    return std::make_shared<Velopack::UpdateManager>(std::make_unique<Velopack::GithubSource>(Repository, "", prerelease));
}

Update download() {
    Update update;
    try {
        update.manager = managerFor(false);
    } catch (const std::exception &) {
        return update;
    }
    try {
        if (update.manager->GetCurrentVersion().find('-') != std::string::npos)
            update.manager = managerFor(true);
        update.info = update.manager->CheckForUpdates();
        if (update.info)
            update.manager->DownloadUpdates(*update.info);
    } catch (const std::exception &error) {
        qWarning("Update failed: %s", error.what());
        update.info.reset();
    }
    return update;
}

}

void Updater::startup() {
    Velopack::VelopackApp::Build().Run();
}

void Updater::check(QWidget *window) {
    auto *watcher = new QFutureWatcher<Update>(window);
    QObject::connect(watcher, &QFutureWatcher<Update>::finished, window, [watcher, window]() {
        Update update = watcher->result();
        watcher->deleteLater();
        if (!update.info)
            return;
        QString version = QString::fromStdString(update.info->TargetFullRelease.Version);
        if (QMessageBox::question(window, QCoreApplication::translate("Updater", "Update"), QCoreApplication::translate("Updater", "OpenKogama Launcher %1 is ready. Restart now to update?").arg(version)) != QMessageBox::Yes)
            return;
        try {
            stopChildren(window);
            update.manager->WaitExitThenApplyUpdates(*update.info);
            QApplication::quit();
        } catch (const std::exception &error) {
            QMessageBox::warning(window, QCoreApplication::translate("Updater", "Update"), QString::fromUtf8(error.what()));
        }
    });
    watcher->setFuture(QtConcurrent::run(download));
}
