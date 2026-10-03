#ifndef ASSETINSTALLER_H
#define ASSETINSTALLER_H

#include <QObject>
#include <QProcess>

class AssetInstaller : public QObject
{
    Q_OBJECT

public:
    explicit AssetInstaller(QObject *parent = nullptr);

    void start(const QString &version);
    void cancel();

    static QString versionOf(const QString &instancePath);

signals:
    void progress(qint64 done, qint64 total);
    void finished(bool ok);

private:
    void readOutput();

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
};

#endif // ASSETINSTALLER_H
