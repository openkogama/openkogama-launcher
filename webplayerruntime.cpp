#include "webplayerruntime.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProgressDialog>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
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

void registerRuntime() {
    QSettings settings("HKEY_CURRENT_USER\\Software\\Unity\\WebPlayer", QSettings::NativeFormat);
    if (!settings.value("Directory").toString().isEmpty())
        return;
    settings.setValue("Directory", QDir::toNativeSeparators(runtimeRoot()));
    settings.setValue("UnityWebPlayerReleaseChannel", "Stable");
    settings.setValue("UnityWebPlayerDevelopment", "no");
}

}

bool WebPlayerRuntime::isWebPlayerFile(const QString &name) {
    return name.endsWith(".unityweb", Qt::CaseInsensitive) || name.endsWith(".unity3d", Qt::CaseInsensitive);
}

QString WebPlayerRuntime::bundledDir(const QString &name) {
    QString bundled = QCoreApplication::applicationDirPath() + "/" + name;
    if (QDir(bundled).exists())
        return bundled;
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/" + name;
}

QString WebPlayerRuntime::playerPath() {
    return bundledDir("player") + "/openkogama-player.exe";
}

bool WebPlayerRuntime::ensureInstalled(QWidget *parent) {
    if (installed()) {
        registerRuntime();
        return true;
    }

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

    QBuffer archive(&data);
    archive.open(QIODevice::ReadOnly);
    QDir root(runtimeRoot());
    QZipReader zip(&archive);
    const QList<QZipReader::FileInfo> entries = zip.fileInfoList();
    QSet<QString> folders;
    for (const QZipReader::FileInfo &entry : entries)
        for (qsizetype slash = entry.filePath.indexOf(u'/'); slash > 0; slash = entry.filePath.indexOf(u'/', slash + 1))
            folders.insert(entry.filePath.left(slash));

    QString failed;
    for (const QZipReader::FileInfo &entry : entries) {
        QString path = root.filePath(entry.filePath);
        bool isDir = entry.isDir || folders.contains(entry.filePath);
        QString folder = isDir ? path : QFileInfo(path).absolutePath();
        if (!QDir().mkpath(folder)) {
            failed = "Could not create the folder " + folder + ", check the folder permissions or run the launcher as administrator.";
        } else if (!isDir && entry.isFile) {
            QByteArray content = zip.fileData(entry.filePath);
            if (content.size() != entry.size)
                content = zip.fileData(QString(entry.filePath).replace(u'/', u'\\'));
            QFile file(path);
            if (content.size() != entry.size)
                failed = "Could not unpack " + entry.filePath + " from the download.";
            else if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
                failed = "Could not write " + path + " (" + file.errorString() + "), check your antivirus.";
        }
        if (!failed.isEmpty()) break;
    }
    zip.close();

    if (!failed.isEmpty() || !installed()) {
        QMessageBox::warning(parent, "Unity Web Player", "Could not install to " + runtimeRoot() + "\n" + (failed.isEmpty() ? "Files were removed after installing, check your antivirus." : failed));
        return false;
    }
    registerRuntime();
    return true;
}
