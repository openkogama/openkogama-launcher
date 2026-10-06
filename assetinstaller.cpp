#include "assetinstaller.h"
#include "webplayerruntime.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

static constexpr unsigned long CreateNoWindow = 0x08000000;

AssetInstaller::AssetInstaller(QObject *parent)
    : QObject(parent)
{
}

QString AssetInstaller::versionOf(const QString &instancePath) {
    QFile file(QDir(instancePath).filePath("instance.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonObject info = QJsonDocument::fromJson(file.readAll()).object();
    QString version = info.value("version").toString();
    QString unity = info.value("unityVersion").toString();
    return version.isEmpty() || unity.isEmpty() ? version : version + "@" + unity;
}

void AssetInstaller::start(const QString &version) {
    QDir dir(WebPlayerRuntime::bundledDir("server"));
    if (version.isEmpty() || !dir.exists("openkogama-server.exe")) {
        emit finished(false);
        return;
    }

    m_process = new QProcess(this);
    m_process->setProgram(dir.filePath("openkogama-server.exe"));
    m_process->setArguments({"install", version});
    m_process->setWorkingDirectory(dir.path());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    connect(m_process, &QProcess::readyRead, this, &AssetInstaller::readOutput);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        m_process->deleteLater();
        m_process = nullptr;
        emit finished(status == QProcess::NormalExit && code == 0);
    });
    m_process->start();
}

void AssetInstaller::cancel() {
    if (!m_process) return;
    m_process->disconnect(this);
    m_process->kill();
    m_process->deleteLater();
    m_process = nullptr;
}

void AssetInstaller::readOutput() {
    if (!m_process) return;
    m_buffer += m_process->readAll();
    qsizetype end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        QList<QByteArray> parts = m_buffer.left(end).trimmed().split(' ');
        m_buffer.remove(0, end + 1);
        if (parts.size() == 3 && parts[0] == "progress")
            emit progress(parts[1].toLongLong(), parts[2].toLongLong());
    }
}
