#pragma once

#include <QFrame>
#include <QPointer>

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QTimer;
class Tunnel;

class ShareBar : public QFrame
{
    Q_OBJECT

public:
    explicit ShareBar(QWidget *parent = nullptr);
    void share(int world, const QString &name, const QString &version);
    void setVersion(const QString &version);
    void stop();
    void restart();
    int players() const { return m_players; }
    int hostedWorld() const { return m_tunnel ? m_world : 0; }

signals:
    void hostedWorldChanged(int world);
    void settingsRequested();
    void listed(const QString &address, const QString &title);

private:
    void start();
    void newCode();
    void leaveList();
    void heard(const QString &word, const QString &rest);
    void ended();
    void refresh();
    void sendThumbnail(int tries);
    static QByteArray smallJpeg(const QByteArray &bytes);

    QLabel *m_text;
    QLabel *m_code;
    QLabel *m_info;
    QPushButton *m_copy;
    QPushButton *m_newCode;
    QPushButton *m_settings;
    QPushButton *m_stop;
    QTimer *m_slow;
    QTimer *m_upload;
    QNetworkAccessManager *m_net = nullptr;
    QPointer<Tunnel> m_tunnel;
    QString m_name;
    QString m_title;
    QString m_version;
    QString m_codeText;
    QString m_privateSecret;
    QString m_error;
    int m_world = 0;
    int m_players = 0;
    bool m_waiting = false;
    bool m_listed = false;
};
