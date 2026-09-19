#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QUrl>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

struct Version {
    QString version;
    QString date;
    QString unity;
    QString backend;
    QString download;
    QString installed;
    QString sha;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    Ui::MainWindow *ui;
    QNetworkAccessManager m_net;
    void downloadFile(const QUrl &url, const QString &path);
    void loadVersions();
    void onVersionSelected(const QModelIndex &current, const QModelIndex &previous);
    QList<Version> versions;
};
#endif // MAINWINDOW_H
