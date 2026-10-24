#include "tunnel.h"
#include "paths.h"
#include "settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>

namespace {
constexpr unsigned long CreateNoWindow = 0x08000000;
constexpr int CodeLength = 6;
const QString Letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
}

QString Tunnel::newSecret()
{
    const QString letters = "abcdefghijklmnopqrstuvwxyz0123456789";
    QString secret;
    for (int i = 0; i < SecretLength; ++i)
        secret += letters[QRandomGenerator::system()->bounded(int(letters.size()))];
    return secret;
}

QString Tunnel::normalize(const QString &text)
{
    QString code;
    for (const QChar letter : text.trimmed().toUpper()) {
        if (letter == '-' || letter == ' ') continue;
        if (!Letters.contains(letter)) return {};
        code += letter;
    }
    return code.size() == CodeLength ? code : QString();
}

bool Tunnel::isAddress(const QString &address)
{
    return address.startsWith("p2p:") && !normalize(address.mid(4)).isEmpty();
}

QString Tunnel::address(const QString &code)
{
    return "p2p:" + code;
}

QString Tunnel::code(const QString &address)
{
    return normalize(address.mid(4));
}

QString Tunnel::display(const QString &code)
{
    return code.left(3) + "-" + code.mid(3);
}

QStringList Tunnel::clients(const QJsonObject &info)
{
    QStringList clients;
    const QJsonArray listed = info.value("clients").toArray();
    for (const QJsonValue &value : listed) {
        const QString client = value.toString().trimmed().left(64);
        if (!client.isEmpty()) clients.append(client);
    }
    const QString version = info.value("version").toString().trimmed().left(64);
    if (!info.contains("clients") && !version.isEmpty()) clients.append(version);
    return clients;
}

Tunnel *Tunnel::host(const QString &secret, const QString &info, bool listed, QObject *parent)
{
    QStringList arguments{"host", "--secret", secret, "--info", info,
        "--http", QString::number(LocalHttpPort), "--photon", QString::number(LocalPhotonPort)};
    if (listed) arguments.append("--public");
    return new Tunnel(arguments, parent);
}

Tunnel *Tunnel::join(const QString &code, QObject *parent)
{
    return new Tunnel({"join", code}, parent);
}

Tunnel *Tunnel::find(const QString &code, QObject *parent)
{
    return new Tunnel({"find", code}, parent);
}

Tunnel::Tunnel(const QStringList &arguments, QObject *parent)
    : QObject(parent), m_process(new QProcess(this))
{
    m_process->setProgram(QDir(Paths::server()).filePath("openkogama-server.exe"));
    QStringList all = QStringList{"tunnel"} + arguments + QStringList{"--parent", QString::number(QCoreApplication::applicationPid())};
    if (const QString rendezvous = Settings::rendezvous(); !rendezvous.isEmpty()) all += {"--rendezvous", rendezvous};
    m_process->setArguments(all);
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CreateNoWindow;
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_pending += m_process->readAllStandardOutput();
        for (qsizetype end = m_pending.indexOf('\n'); end >= 0; end = m_pending.indexOf('\n')) {
            const QString line = QString::fromUtf8(m_pending.left(end)).trimmed();
            m_pending.remove(0, end + 1);
            const qsizetype space = line.indexOf(' ');
            emit said(space < 0 ? line : line.left(space), space < 0 ? QString() : line.mid(space + 1));
        }
    });
    connect(m_process, &QProcess::finished, this, [this]() {
        if (m_ended) return;
        m_ended = true;
        emit ended();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_ended) return;
        m_ended = true;
        emit said("error", tr("Could not start %1").arg(m_process->program()));
        emit ended();
    });
    QMetaObject::invokeMethod(this, [this]() { m_process->start(); }, Qt::QueuedConnection);
}

void Tunnel::stop()
{
    if (m_process->state() != QProcess::NotRunning) m_process->kill();
}
