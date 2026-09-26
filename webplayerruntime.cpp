#include "webplayerruntime.h"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtCore/private/qzipreader_p.h>

namespace {

const QString RuntimeUrl = "https://cdn.openkogama.org/webplayer/unity-webplayer-4.6.6f2-win.zip";
const QByteArray RuntimeSha256 = "ae15b89a3cb3bd37401bcba04a94daf6617414ae547e9c1e78d89f2ec7f1a056";

QString runtimeRoot() {
    return QDir::homePath() + "/AppData/LocalLow/Unity/WebPlayer";
}

bool installed() {
    QDir root(runtimeRoot());
    return root.exists("loader/npUnity3D32.dll") && root.exists("player/Stable3.x.x/webplayer_win.dll") && root.exists("mono/Stable3.x.x/mono-1-vc.dll");
}

}

bool WebPlayerRuntime::isWebPlayerFile(const QString &name) {
    return name.endsWith(".unityweb", Qt::CaseInsensitive) || name.endsWith(".unity3d", Qt::CaseInsensitive);
}

QString WebPlayerRuntime::playerPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/player/openkogama-player.exe";
}

bool WebPlayerRuntime::ensureInstalled(QWidget *parent) {
    if (installed())
        return true;

    QProgressDialog progress("Downloading Unity Web Player...", "Cancel", 0, 100, parent);
    progress.setWindowTitle("Unity Web Player");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);

    QNetworkAccessManager net;
    QNetworkReply *reply = net.get(QNetworkRequest(QUrl(RuntimeUrl)));
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress, [&progress](qint64 received, qint64 total) {
        if (total > 0)
            progress.setValue(int(received * 100 / total));
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (reply->error() != QNetworkReply::OperationCanceledError)
            QMessageBox::warning(parent, "Unity Web Player", "Download failed: " + reply->errorString());
        return false;
    }

    QByteArray data = reply->readAll();
    if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != RuntimeSha256) {
        QMessageBox::warning(parent, "Unity Web Player", "The downloaded file is damaged, try again.");
        return false;
    }

    progress.setLabelText("Installing Unity Web Player...");
    progress.setCancelButton(nullptr);

    QTemporaryFile archive;
    if (!archive.open() || archive.write(data) != data.size()) {
        QMessageBox::warning(parent, "Unity Web Player", "Could not save the download.");
        return false;
    }
    archive.close();

    QDir().mkpath(runtimeRoot());
    QZipReader zip(archive.fileName());
    bool extracted = zip.extractAll(runtimeRoot());
    zip.close();

    if (!extracted || !installed()) {
        QMessageBox::warning(parent, "Unity Web Player", "Could not install to " + runtimeRoot());
        return false;
    }
    return true;
}
