#ifndef INSTALLDIALOG_H
#define INSTALLDIALOG_H

#include <QDialog>
#include <QNetworkAccessManager>
#include <QStandardItemModel>
#include <QUrl>

namespace Ui {
class InstallDialog;
}

struct Version {
    QString version;
    QString date;
    QString unity;
    QString backend;
    QString download;
    QString installed;
    QString sha;
};

class InstallDialog : public QDialog
{
    Q_OBJECT

public:
    explicit InstallDialog(QWidget *parent = nullptr);
    ~InstallDialog();

private:
    Ui::InstallDialog *ui;
    QStandardItemModel *model;
    QNetworkAccessManager *m_net;
    void downloadFile(const QUrl &url, const QString &path);
    void loadVersions();
    void onVersionSelected(const QModelIndex &current, const QModelIndex &previous);
    void onNetworkReply(QNetworkReply *reply);
    QList<Version> versions;
};

#endif // INSTALLDIALOG_H
