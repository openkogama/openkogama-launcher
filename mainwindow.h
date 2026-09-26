#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QUrl>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    QStandardItemModel *instances;
    QByteArray m_versionsJson;
    void loadInstances();
    void onInstallTriggered();
    void onDiscordTriggered();
    void onLaunchTriggered();
    void showInstanceMenu(const QPoint &pos);
    void launchInstance(const QString &path);
    void renameInstance();
    void duplicateInstance();
    void deleteInstance();
    void onInstanceRenamed(QStandardItem *item);
};
#endif // MAINWINDOW_H
