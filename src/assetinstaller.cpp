#include "assetinstaller.h"
#include "gameruntime.h"
#include "paths.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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
    QDir dir(Paths::server());
    if (version.isEmpty() || !GameRuntime::hasServer()) {
        emit finished(false);
        return;
    }

    QDir().mkpath(Paths::serverData());
    m_process = new QProcess(this);
    m_process->setProgram(GameRuntime::serverProgram());
    m_process->setArguments({"install", version});
    m_process->setWorkingDirectory(Paths::serverData());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    GameRuntime::prepare(m_process);
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
