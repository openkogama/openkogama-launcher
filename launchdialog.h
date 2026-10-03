#ifndef LAUNCHDIALOG_H
#define LAUNCHDIALOG_H

#include <QDialog>
#include <QNetworkAccessManager>
#include <QProcess>

class AssetInstaller;

inline const QString ServerUrl = "http://127.0.0.1:8080";

namespace Ui {
class LaunchDialog;
}

class LaunchDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LaunchDialog(const QString &version, QWidget *parent = nullptr);
    ~LaunchDialog();

    QProcess *server() const { return m_server; }
    static void stopServer(QProcess *server);

protected:
    void reject() override;

private:
    void checkServer();
    void startServer();
    void waitForServer(int attempts);
    void fail(const QString &message);
    void setStep(const QString &status, int progress);

    Ui::LaunchDialog *ui;
    QNetworkAccessManager *m_net;
    QProcess *m_server = nullptr;
    AssetInstaller *m_assets;
    bool m_offline = false;
    bool m_cancelled = false;
};

#endif // LAUNCHDIALOG_H
