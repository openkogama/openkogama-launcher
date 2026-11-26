#ifndef WORLDSDIALOG_H
#define WORLDSDIALOG_H

#include <QDialog>
#include <QIcon>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QUrl>
#include <functional>

class ArchiveBrowser;
class QStackedWidget;
class QTabBar;
struct ImportRequest;

namespace Ui {
class WorldsDialog;
}

class WorldsDialog : public QDialog
{
    Q_OBJECT

public:
    WorldsDialog(const QString &path, const QString &exe, QProcess *server, QWidget *parent = nullptr);
    ~WorldsDialog();
    void setInstance(const QString &path, const QString &exe, QProcess *server = nullptr);
    bool hasRunningGames() const;
    QWidget *worldsPage() const;
    QWidget *avatarsPage() const;
    QWidget *inventoryPage() const;
    void showWorlds(int select = -1);
    void showAvatars(int select = -1);
    void showInventory(int select = -1);
    void refreshAfterImport(const QString &kind, int select);
    void setArchiveThumbnail(int world, const QUrl &url);
    void setHostedWorld(int world);
    void reject() override;

signals:
    void hostRequested(int world, const QString &name, QProcess *client);
    void importRequested(const ImportRequest &request, const QUrl &thumbnailUrl);
    void clientForYearRequested(const QString &published, const QString &mapName);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadWorlds(int select = 0, bool refreshPictures = false);
    void filterWorlds();
    void updateWorldActions();
    void updateAvatarActions();
    void filterAvatars();
    void loadTemplates();
    void loadThumbnail(QListWidgetItem *item, int world);
    void checkRevision();
    void createWorld();
    void importWorld();
    void importWorlds(QStringList paths, QStringList failed, int lastId = 0);
    void findArchiveThumbnail(int world, const QByteArray &file);
    void downloadArchiveThumbnail(int world, const QUrl &url);
    void exportWorld();
    void renameWorld();
    void deleteWorld();
    void setPlatformer(bool on);
    void onWorldRenamed(QListWidgetItem *item);
    void showWorldMenu(const QPoint &pos);
    QProcess *launch(const QString &mode, int world);
    int selectedWorld() const;
    QString hostedMode(int world) const;
    void loadAvatars(int select = 0, bool refreshPictures = false);
    void loadAvatarPicture(QListWidgetItem *item, int avatar, const QString &version);
    void enqueueAvatarPreview(int avatar);
    void fetchNextAvatarPreview();
    void renderAvatarPreview(int avatar);
    void useAvatar();
    void editAvatar();
    void activateAvatar(int avatar, std::function<void()> done);
    void importAvatar();
    void importAvatars(QStringList paths, QStringList failed, int lastId = 0);
    void exportAvatar();
    void renameAvatar();
    void onAvatarRenamed(QListWidgetItem *item);
    void deleteAvatar();
    void showAvatarMenu(const QPoint &pos);
    int selectedAvatar() const;
    void loadInventory(int select = 0, bool refreshPictures = false);
    void enqueueItemPicture(int itemId);
    void fetchNextItemPicture();
    void loadItemPicture(int itemId);
    void filterInventory();
    void updateInventoryActions();
    void importModel();
    void importModels(QStringList paths, const QString &scale, QStringList failed, QStringList unscaled, int lastId = 0);
    void exportItem();
    void renameItem();
    void scaleItem();
    void onItemRenamed(QListWidgetItem *item);
    void deleteItem();
    void deleteBatch(const QString &endpoint, QList<int> ids, const QString &errorTitle, const QString &errorMessage, std::function<void()> onDone, int failed = 0);
    void exportBatch(const QString &endpoint, QList<QPair<int, QString>> items, const QString &dir, const QString &ext, int failed = 0);
    void showItemMenu(const QPoint &pos);
    int selectedItem() const;

    Ui::WorldsDialog *ui;
    QPointer<QWidget> m_owner;
    QString m_path;
    QString m_title;
    QString m_exe;
    QPointer<QProcess> m_server;
    QNetworkAccessManager *m_net;
    QNetworkAccessManager *m_mediaNet;
    QList<QPair<QString, QString>> m_templates;
    int m_revision = -1;
    int m_hostedWorld = 0;
    bool m_hasAvatarEditor = true;
    QList<QPointer<QProcess>> m_clients;
    QTabBar *m_worldsView = nullptr;
    QStackedWidget *m_worldsStack = nullptr;
    ArchiveBrowser *m_mapsArchive = nullptr;
    QTabBar *m_avatarsView = nullptr;
    QStackedWidget *m_avatarsStack = nullptr;
    ArchiveBrowser *m_avatarsArchive = nullptr;
    QTabBar *m_inventoryView = nullptr;
    QStackedWidget *m_inventoryStack = nullptr;
    ArchiveBrowser *m_modelsArchive = nullptr;
    QQueue<int> m_pendingModels;
    QSet<int> m_queuedModels;
    int m_activeModels = 0;
    int m_inventoryGeneration = 0;
    QQueue<int> m_pendingAvatars;
    QSet<int> m_queuedAvatars;
    int m_activeAvatars = 0;
    int m_avatarGeneration = 0;
};

#endif // WORLDSDIALOG_H
