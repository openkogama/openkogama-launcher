#ifndef CONTENTIMPORTER_H
#define CONTENTIMPORTER_H

#include "links.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>

class QNetworkAccessManager;
class QNetworkReply;

class ContentImporter : public QObject
{
    Q_OBJECT

public:
    explicit ContentImporter(const QString &clientVersion, QObject *parent = nullptr);

    void start(const ImportRequest &request);
    void cancel();

signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok, const QString &error, int id);

private:
    void fetch();
    void downloaded();
    void upload();
    void uploaded();
    void finish(bool ok, const QString &error, int id);
    QByteArray avatarJson(const QByteArray &data) const;

    QNetworkAccessManager *m_net;
    QPointer<QNetworkReply> m_reply;
    ImportRequest m_request;
    QByteArray m_payload;
    QString m_clientVersion;
    QString m_lastError;
    int m_urlIndex = 0;
    bool m_cancelled = false;
    bool m_finished = false;
    bool m_retriedScale = false;
};

#endif // CONTENTIMPORTER_H
