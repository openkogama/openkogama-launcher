#include "installprogressdialog.h"
#include "ui_installprogressdialog.h"
#include <QIcon>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>
#include <QtConcurrent>
#include <QtCore/private/qzipreader_p.h>

static QString sanitize(QString name) {
    static const QString illegal = "/\\:*?\"<>|";
    for (QChar &c : name)
        if (illegal.contains(c)) c = '-';
    return name.trimmed();
}

static QString uniqueDir(const QString &base, const QString &folder) {
    QString path = base + folder;
    int n = 2;
    while (QDir(path).exists())
        path = base + folder + " (" + QString::number(n++) + ")";
    return path;
}

InstallProgressDialog::InstallProgressDialog(const Version &version, const QString &name, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::InstallProgressDialog)
    , m_version(version)
    , m_name(name)
    , m_net(new QNetworkAccessManager(this))
    , m_watcher(new QFutureWatcher<bool>(this))
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/install.png"));
    setWindowTitle("Installing " + version.version);
    ui->nameLabel->setText("Installing KoGaMa " + version.version);
    ui->progressBar->setRange(0, 100);
    ui->progressBar->setValue(0);

    connect(m_watcher, &QFutureWatcher<bool>::finished, this, &InstallProgressDialog::onExtracted);

    startDownload();
}

InstallProgressDialog::~InstallProgressDialog()
{
    delete ui;
}

void InstallProgressDialog::startDownload() {
    if (m_urlIndex >= m_version.urls.size()) {
        ui->statusLabel->setText("Could not download this version, all sources failed");
        return;
    }

    QString temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_file.setFileName(QDir(temp).filePath("openkogama-" + m_version.sha256 + ".zip"));
    if (!m_file.open(QIODevice::WriteOnly)) {
        ui->statusLabel->setText("Cannot open temp file");
        return;
    }

    m_hash.reset();
    ui->progressBar->setValue(0);

    QUrl url(m_version.urls[m_urlIndex]);
    ui->statusLabel->setText("Downloading from " + url.host());

    m_reply = m_net->get(QNetworkRequest(url));
    connect(m_reply, &QNetworkReply::downloadProgress, this, &InstallProgressDialog::onProgress);
    connect(m_reply, &QNetworkReply::readyRead, this, &InstallProgressDialog::consume);
    connect(m_reply, &QNetworkReply::finished, this, &InstallProgressDialog::onFinished);
}

void InstallProgressDialog::consume() {
    QByteArray chunk = m_reply->readAll();
    m_file.write(chunk);
    m_hash.addData(chunk);
}

void InstallProgressDialog::onProgress(qint64 received, qint64 total) {
    if (total <= 0) total = m_version.zipSize;
    if (total <= 0) return;
    ui->progressBar->setValue(int(received * 100 / total));
}

void InstallProgressDialog::onFinished() {
    if (m_cancelled) return;

    consume();
    m_file.close();

    QString digest = QString::fromLatin1(m_hash.result().toHex());
    bool ok = m_reply->error() == QNetworkReply::NoError
              && digest.compare(m_version.sha256, Qt::CaseInsensitive) == 0;

    m_reply->deleteLater();
    m_reply = nullptr;

    if (ok) {
        ui->progressBar->setValue(100);
        extract();
        return;
    }

    m_urlIndex++;
    startDownload();
}

void InstallProgressDialog::extract() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/instances/";
    QString folder = sanitize(m_name);
    m_dest = uniqueDir(base, folder.isEmpty() ? "KoGaMa" : folder);
    QDir().mkpath(m_dest);

    ui->statusLabel->setText("Extracting...");
    ui->buttonBox->setEnabled(false);

    QString zipPath = m_file.fileName();
    QString dest = m_dest;
    m_watcher->setFuture(QtConcurrent::run([zipPath, dest]() {
        QZipReader zip(zipPath);
        bool ok = zip.extractAll(dest);
        zip.close();
        return ok;
    }));
}

void InstallProgressDialog::onExtracted() {
    if (!m_watcher->result()) {
        ui->statusLabel->setText("Extraction failed");
        ui->buttonBox->setEnabled(true);
        return;
    }

    QJsonObject meta;
    meta["name"] = m_name;
    meta["version"] = m_version.version;
    meta["unityVersion"] = m_version.unity;
    meta["il2cpp"] = m_version.backend == "IL2CPP";
    meta["sha256"] = m_version.sha256;
    meta["id"] = m_version.id;
    meta["installedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QFile f(m_dest + "/instance.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(meta).toJson(QJsonDocument::Indented));

    QFile::remove(m_file.fileName());

    ui->statusLabel->setText("Installed to " + m_dest);
    QTimer::singleShot(800, this, &QDialog::accept);
}

void InstallProgressDialog::reject() {
    if (m_watcher->isRunning()) return;

    m_cancelled = true;
    if (m_reply) m_reply->abort();
    if (m_file.isOpen()) m_file.close();
    QFile::remove(m_file.fileName());
    QDialog::reject();
}
