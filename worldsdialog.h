#ifndef WORLDSDIALOG_H
#define WORLDSDIALOG_H

#include <QDialog>
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

private:
    void loadWorlds(int select = 0);
    void loadTemplates();
    void createWorld();
    void importWorld();
    void launch(const QString &mode, int world);
    int selectedWorld() const;

    Ui::WorldsDialog *ui;
    QString m_path;
    QString m_exe;
    QPointer<QProcess> m_server;
    QNetworkAccessManager *m_net;
    QList<QPair<QString, QString>> m_templates;
};

#endif // WORLDSDIALOG_H
