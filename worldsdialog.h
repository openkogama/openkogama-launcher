#ifndef WORLDSDIALOG_H
#define WORLDSDIALOG_H

#include <QDialog>
#include <QIcon>
#include <QListWidgetItem>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>

namespace Ui {
class WorldsDialog;
}

class WorldsDialog : public QDialog
{
    Q_OBJECT

public:
    WorldsDialog(const QString &path, const QString &exe, QProcess *server, QWidget *parent = nullptr);
    ~WorldsDialog();

protected:
    void reject() override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void loadWorlds(int select = 0);
    void loadTemplates();
    void loadThumbnail(QListWidgetItem *item, int world);
    void checkRevision();
    static QIcon thumbnail(const QPixmap &pixmap);
    void setupGrid(QListWidget *list);
    void fitGrid(QListWidget *list, const QSize &icon);
    void createWorld();
    void importWorld();
    void exportWorld();
    void renameWorld();
    void deleteWorld();
    void onWorldRenamed(QListWidgetItem *item);
    void showWorldMenu(const QPoint &pos);
    void launch(const QString &mode, int world);
    int selectedWorld() const;
    void loadAvatars(int select = 0);
    void loadAvatarPicture(QListWidgetItem *item, int avatar, const QString &version);
    void useAvatar();
    void importAvatar();
    void exportAvatar();
    void showAvatarMenu(const QPoint &pos);
    int selectedAvatar() const;

    Ui::WorldsDialog *ui;
    QPointer<QWidget> m_owner;
    QString m_path;
    QString m_title;
    QString m_exe;
    QPointer<QProcess> m_server;
    QNetworkAccessManager *m_net;
    QList<QPair<QString, QString>> m_templates;
    int m_revision = -1;
};

#endif // WORLDSDIALOG_H
