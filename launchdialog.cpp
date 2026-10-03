#include "launchdialog.h"
#include "assetinstaller.h"
#include "webplayerruntime.h"
#include "ui_launchdialog.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QIcon>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTimer>

static constexpr unsigned long CreateNoWindow = 0x08000000;
static constexpr int PingAttempts = 200;
static constexpr int PingTimeout = 150;
static constexpr int AssetsEnd = 50;

static QNetworkRequest pingRequest() {
    QNetworkRequest request(QUrl(ServerUrl + "/ping"));
    request.setTransferTimeout(PingTimeout);
    return request;
}

LaunchDialog::LaunchDialog(const QString &version, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LaunchDialog)
    , m_net(new QNetworkAccessManager(this))
    , m_assets(new AssetInstaller(this))
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/launch.png"));
    setWindowTitle("Starting server");
    ui->nameLabel->setText("Starting OpenKogama server");
    ui->progressBar->setRange(0, 100);

    connect(m_assets, &AssetInstaller::progress, this, [this](qint64 done, qint64 total) {
        if (total > 0) setStep("Checking game assets...", int(done * AssetsEnd / total));
    });
    connect(m_assets, &AssetInstaller::finished, this, [this](bool ok) {
        if (m_cancelled) return;
        m_offline = ok;
        checkServer();
    });
    setStep("Checking game assets...", 0);
    m_assets->start(version);
}

void LaunchDialog::checkServer() {
    setStep("Checking server...", AssetsEnd);
    QNetworkReply *reply = m_net->get(pingRequest());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_cancelled) return;
        if (reply->error() == QNetworkReply::NoError)
            accept();
        else
            startServer();
    });
}

LaunchDialog::~LaunchDialog()
{
    delete ui;
}

void LaunchDialog::reject() {
    m_cancelled = true;
    m_assets->cancel();
    if (m_server) {
        stopServer(m_server);
        m_server = nullptr;
    }
    QDialog::reject();
}

void LaunchDialog::startServer() {
    QDir dir(WebPlayerRuntime::bundledDir("server"));
    if (!dir.exists("openkogama-server.exe")) {
        fail("Server not found in " + dir.path());
        return;
    }

    setStep("Starting server...", AssetsEnd + 5);
    m_server = new QProcess(parentWidget());
    m_server->setProgram(dir.filePath("openkogama-server.exe"));
    m_server->setWorkingDirectory(dir.path());
    if (m_offline) m_server->setArguments({"--offline"});
    m_server->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    connect(m_server, &QProcess::finished, m_server, &QObject::deleteLater);
    m_server->start();

    waitForServer(PingAttempts);
}

void LaunchDialog::waitForServer(int attempts) {
    if (m_cancelled) return;
    if (m_server->state() == QProcess::NotRunning || attempts == 0) {
        m_server->kill();
        m_server = nullptr;
        fail("Server did not start");
        return;
    }

    setStep("Waiting for server...", AssetsEnd + 10 + (90 - AssetsEnd) * (PingAttempts - attempts) / PingAttempts);
    QNetworkReply *reply = m_net->get(pingRequest());
    connect(reply, &QNetworkReply::finished, this, [this, reply, attempts]() {
        reply->deleteLater();
        if (m_cancelled) return;
        if (reply->error() == QNetworkReply::NoError)
            accept();
        else
            QTimer::singleShot(50, this, [this, attempts]() { waitForServer(attempts - 1); });
    });
}

void LaunchDialog::fail(const QString &message) {
    ui->statusLabel->setText(message);
    ui->progressBar->setValue(0);
    ui->buttonBox->setStandardButtons(QDialogButtonBox::Close);
}

void LaunchDialog::setStep(const QString &status, int progress) {
    ui->statusLabel->setText(status);
    ui->progressBar->setValue(qMax(ui->progressBar->value(), progress));
}

void LaunchDialog::stopServer(QProcess *server) {
    auto *net = new QNetworkAccessManager(server);
    net->get(QNetworkRequest(QUrl(ServerUrl + "/shutdown")));
    QTimer::singleShot(5000, server, [server]() { server->kill(); });
}
