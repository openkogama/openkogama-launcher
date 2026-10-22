#include "launchdialog.h"
#include "assetinstaller.h"
#include "paths.h"
#include "ui_launchdialog.h"
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTimer>
#include <QUdpSocket>

static constexpr unsigned long CreateNoWindow = 0x08000000;
static constexpr int PingAttempts = 200;
static constexpr int PingTimeout = 150;
static constexpr int AssetsEnd = 50;
static constexpr int DefaultHttpPort = 8080;
static constexpr int DefaultPhotonPort = 5055;
static constexpr int PortBusy = 3;
static constexpr int PortTries = 50;
static constexpr int MinPort = 20000;
static constexpr int MaxPort = 32767;
static constexpr qint64 LogTail = 8192;

static QNetworkRequest pingRequest(const QString &path = "/ping") {
    QNetworkRequest request(QUrl(LocalServerUrl + path));
    request.setTransferTimeout(PingTimeout);
    return request;
}

static bool tcpFree(int port) {
    QTcpServer probe;
    return probe.listen(QHostAddress::Any, port);
}

static bool udpFree(int port) {
    QUdpSocket probe;
    return probe.bind(QHostAddress::Any, port, QAbstractSocket::DontShareAddress);
}

static int freePort(bool udp) {
    for (int attempt = 0; attempt < PortTries; ++attempt) {
        const int port = int(QRandomGenerator::global()->bounded(MinPort, MaxPort + 1));
        if (tcpFree(port) && (!udp || udpFree(port))) return port;
    }
    return 0;
}

static void choosePorts(bool retry, int busy) {
    const bool both = busy != LocalHttpPort && busy != LocalPhotonPort;
    int http = LocalHttpPort;
    if (!retry || both || busy == LocalHttpPort)
        http = !retry && tcpFree(DefaultHttpPort) ? DefaultHttpPort : freePort(false);
    int photon = LocalPhotonPort;
    if (!retry || both || busy == LocalPhotonPort)
        photon = !retry && tcpFree(DefaultPhotonPort) && udpFree(DefaultPhotonPort) ? DefaultPhotonPort : freePort(true);
    if (photon == http) photon = freePort(true);
    if (http > 0) LocalHttpPort = http;
    if (photon > 0) LocalPhotonPort = photon;
    LocalServerUrl = "http://127.0.0.1:" + QString::number(LocalHttpPort);
}

static QString lastServerError() {
    QDir logs(Paths::serverData() + "/logs");
    const QStringList files = logs.entryList({"server-*.log"}, QDir::Files, QDir::Time);
    if (files.isEmpty()) return {};
    QFile file(logs.filePath(files.first()));
    if (!file.open(QIODevice::ReadOnly)) return {};
    file.seek(qMax<qint64>(0, file.size() - LogTail));
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (auto line = lines.crbegin(); line != lines.crend(); ++line) {
        const qsizetype at = line->indexOf(" error ");
        if (at >= 0) return line->mid(at + 7).trimmed();
    }
    return {};
}

static int busyPort() {
    static const QRegularExpression busy("port (\\d+) is used by another program");
    const QRegularExpressionMatch match = busy.match(lastServerError());
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

LaunchDialog::LaunchDialog(const QString &version, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LaunchDialog)
    , m_net(new QNetworkAccessManager(this))
    , m_assets(new AssetInstaller(this))
{
    ui->setupUi(this);
    setWindowIcon(QIcon(":/launch.png"));
    setWindowTitle(tr("Starting server"));
    ui->nameLabel->setText(tr("Starting OpenKogama server"));
    ui->progressBar->setRange(0, 100);
    ui->statusLabel->setWordWrap(true);

    connect(m_assets, &AssetInstaller::progress, this, [this](qint64 done, qint64 total) {
        if (total > 0) setStep(tr("Checking game assets..."), int(done * AssetsEnd / total));
    });
    connect(m_assets, &AssetInstaller::finished, this, [this](bool ok) {
        if (m_cancelled) return;
        m_offline = ok;
        checkServer();
    });
    setStep(tr("Checking game assets..."), 0);
    m_assets->start(version);
}

void LaunchDialog::checkServer() {
    setStep(tr("Checking server..."), AssetsEnd);
    QNetworkReply *reply = m_net->get(pingRequest("/api/server"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_cancelled) return;
        const QJsonObject server = QJsonDocument::fromJson(reply->readAll()).object();
        if (reply->error() == QNetworkReply::NoError && server.contains("title"))
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
    QDir dir(Paths::server());
    if (!dir.exists("openkogama-server.exe")) {
        fail(tr("Server not found in %1").arg(dir.path()));
        return;
    }

    setStep(tr("Starting server..."), AssetsEnd + 5);
    QDir().mkpath(Paths::serverData());
    choosePorts(m_retried, m_retried ? busyPort() : 0);
    QStringList arguments{"--http", QString::number(LocalHttpPort), "--photon", QString::number(LocalPhotonPort)};
    if (m_offline) arguments.append("--offline");
    m_exitCode = 0;
    m_server = new QProcess(parentWidget());
    m_server->setProgram(dir.filePath("openkogama-server.exe"));
    m_server->setWorkingDirectory(Paths::serverData());
    m_server->setArguments(arguments);
    m_server->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    connect(m_server, &QProcess::finished, this, [this](int exitCode) { m_exitCode = exitCode; });
    connect(m_server, &QProcess::finished, m_server, &QObject::deleteLater);
    m_server->start();

    waitForServer(PingAttempts);
}

void LaunchDialog::waitForServer(int attempts) {
    if (m_cancelled) return;
    if (!m_server || m_server->state() == QProcess::NotRunning || attempts == 0) {
        failed();
        return;
    }

    setStep(tr("Waiting for server..."), AssetsEnd + 10 + (90 - AssetsEnd) * (PingAttempts - attempts) / PingAttempts);
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

void LaunchDialog::failed() {
    const bool running = m_server && m_server->state() != QProcess::NotRunning;
    if (m_server) m_server->kill();
    m_server = nullptr;
    if (!running && m_exitCode == PortBusy && !m_retried) {
        m_retried = true;
        startServer();
        return;
    }
    const QString reason = lastServerError();
    fail(reason.isEmpty() ? tr("Server did not start") : tr("Server did not start: %1").arg(reason));
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
    net->get(QNetworkRequest(QUrl(LocalServerUrl + "/shutdown")));
    QTimer::singleShot(5000, server, [server]() { server->kill(); });
}
