#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QModelIndex>
#include <QEvent>
#include <QObject>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onInstallTriggered();
    void onDiscordTriggered();
    void onLaunchTriggered();
    void showInstanceMenu(const QPoint &pos);
    void renameInstance();
    void onInstanceRenamed(QStandardItem *item);
    void duplicateInstance();
    void deleteInstance();

private:
    void loadInstances();
    void fetchVersions(std::function<void()> done = nullptr);
    void launchInstance(const QString &path);

    Ui::MainWindow *ui;
    QStandardItemModel *instances;
    QByteArray m_versionsJson;
    QString m_versionsError;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif // MAINWINDOW_H
