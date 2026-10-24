#pragma once

#include <QObject>
#include <QStringList>

class QJsonObject;
class QProcess;

class Tunnel : public QObject
{
    Q_OBJECT

public:
    static constexpr int SecretLength = 32;

    static QString newSecret();
    static QString normalize(const QString &text);
    static bool isAddress(const QString &address);
    static QString address(const QString &code);
    static QString code(const QString &address);
    static QString display(const QString &code);
    static QStringList clients(const QJsonObject &info);

    static Tunnel *host(const QString &secret, const QString &info, bool listed, QObject *parent);
    static Tunnel *join(const QString &code, QObject *parent);
    static Tunnel *find(const QString &code, QObject *parent);

    void stop();

signals:
    void said(const QString &word, const QString &rest);
    void ended();

private:
    Tunnel(const QStringList &arguments, QObject *parent);

    QProcess *m_process;
    QByteArray m_pending;
    bool m_ended = false;
};
