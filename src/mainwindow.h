#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QModelIndex>
#include <QEvent>
#include <QObject>
#include <functional>
#include <QPointer>
#include <QSet>

class WorldsDialog;
class ShareBar;
class QAction;
class QNetworkAccessManager;
class ServersPage;
class QProcess;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    void openLink(const QString &link);
    void checkForUpdates();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onInstallTriggered();
    void onDiscordTriggered();
    void onLaunchTriggered();
    void showInstanceMenu(const QPoint &pos);
    void renameInstance();
    void onInstanceRenamed(QStandardItem *item);
    void duplicateInstance();
    void showInstanceFolder();
    void deleteInstance();

private:
    void loadInstances(const QString &selectPath = {});
    void updateInstanceActions();
    void filterInstances();
    void updatePlayPage();
    void onVersionChanged();
    void fetchVersions(std::function<void()> done = nullptr);
    bool launchInstance(const QString &path);
    bool prepareInstance(const QString &path, QString *executable);
    void joinServer(const QString &address);
    void joinCode(const QString &address);
    void offerVersion(const QStringList &clients, const QString &title, std::function<void()> then);
    void updateHostVersion();
    void editServerSettings();
    void waitForServerToStop(int attempts);
    bool hasRemoteGames() const;
    bool startLocalServer(const QString &version, QProcess **server);

    Ui::MainWindow *ui;
    QStandardItemModel *instances;
    QByteArray m_versionsJson;
    QString m_versionsError;

    QPointer<WorldsDialog> m_worlds;
    ShareBar *m_share;
    QAction *m_updates = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    ServersPage *m_servers;
    QList<QPointer<QProcess>> m_remoteClients;
    QPointer<QProcess> m_hostClient;
    QString m_activePath;
    bool m_installing = false;
    bool m_localReady = false;
    QSet<QString> m_checkedVersions;
};

#endif // MAINWINDOW_H
